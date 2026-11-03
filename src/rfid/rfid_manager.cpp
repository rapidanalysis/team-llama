// Backend service for the PiicoDev RFID module (see include/rfid_manager.h).
// A single worker thread owns the I2C bus and only polls it while run mode, live
// scan or a scan is active. HTTP handlers just update `g` and notify the worker.
#include "rfid_manager.h"

#include "i2c.h"
#include "rfid.h"
#include "tags.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <mutex>
#include <poll.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>


namespace rfid_manager {
namespace {

using Clock = std::chrono::steady_clock;
using nlohmann::json;
using namespace std::chrono_literals;

// Poll timings match the original xerxes-rfid program.
constexpr auto kRunPollGap      = 10ms;
constexpr auto kLivePollGap     = 100ms;
constexpr auto kAfterTagPause   = 1000ms;  // stops one tap being read repeatedly
constexpr auto kRetryOpenGap    = 3s;
constexpr auto kScanTimeout     = 30s;
constexpr auto kLiveIdleTimeout = 20s;     // stop live scan once no browser polls events
constexpr const char* kVersion = "2.0.0";  // xerxes-rfid release this is based on
constexpr size_t kMaxEvents      = 300;
constexpr size_t kMaxOutputBytes = 16 * 1024;
constexpr size_t kMaxCommandLen  = 1024;

struct Event {
    uint64_t seq = 0;
    int64_t time_ms = 0;
    json body;
};

struct State {
    std::mutex m;
    std::condition_variable cv;

    // Persisted in rfid.conf
    int bus = 1;
    int address = 0x2C;
    std::string tags_path;
    bool run_enabled = false;

    bool live_active = false;
    Clock::time_point live_last_poll;

    std::string scan = "idle";  // idle | waiting | done | timeout | cancelled
    std::string scan_id, scan_type;
    Clock::time_point scan_deadline;

    std::string hw_state = "idle";  // idle | ok | error
    std::string hw_error;
    bool reopen = false;
    bool probe_requested = false;
    uint64_t probe_generation = 0;

    std::string busy_command;
    std::string last_tag;
    int64_t last_tag_ms = 0;

    std::deque<Event> events;
    uint64_t next_seq = 1;

    bool started = false;
} g;

std::mutex g_tags_mutex;

int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string hex2(int v) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "0x%02X", v & 0xFF);
    return buf;
}

std::string config_dir() {
    const char* custom = std::getenv("XERXES_CONFIG_DIR");
    if (custom && *custom) return custom;
    const char* home = std::getenv("HOME");
    return std::string(home && *home ? home : "/tmp") + "/.xerxes";
}

std::string settings_path() { return config_dir() + "/rfid.conf"; }
std::string default_tags_path() { return config_dir() + "/tags.json"; }

void ensure_config_dir() {
    std::string dir = config_dir();
    if (mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST)
        std::cerr << "[RFID] could not create " << dir << ": " << std::strerror(errno) << "\n";
}

void load_settings_locked() {
    g.tags_path = default_tags_path();
    FILE* f = std::fopen(settings_path().c_str(), "r");
    if (!f) return;
    char line[2048];
    while (std::fgets(line, sizeof line, f)) {
        std::string l = xerxes::trim(line);
        size_t eq = l.find('=');
        if (l.empty() || l[0] == '#' || eq == std::string::npos) continue;
        std::string k = l.substr(0, eq), v = l.substr(eq + 1);
        char* end = nullptr;
        if (k == "bus") {
            long n = std::strtol(v.c_str(), &end, 0);
            if (*end == '\0' && n >= 0 && n <= 1023) g.bus = static_cast<int>(n);
        } else if (k == "address") {
            long n = std::strtol(v.c_str(), &end, 0);
            if (*end == '\0' && n >= 0x03 && n <= 0x77) g.address = static_cast<int>(n);
        } else if (k == "tags") {
            if (!v.empty()) g.tags_path = v;
        } else if (k == "run") {
            g.run_enabled = (v == "1" || v == "true");
        }
    }
    std::fclose(f);
}

// Returns "" on success.
std::string save_settings_locked() {
    ensure_config_dir();
    std::string path = settings_path(), tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "w");
    if (!f) return "Could not write " + path + ": " + std::strerror(errno);
    std::fprintf(f, "# Xerxes RFID reader settings (managed by the web panel)\n"
                    "bus=%d\naddress=%s\ntags=%s\nrun=%d\n",
                 g.bus, hex2(g.address).c_str(), g.tags_path.c_str(), g.run_enabled ? 1 : 0);
    bool ok = std::fflush(f) == 0;
    ok = (std::fclose(f) == 0) && ok;
    if (!ok || std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        return "Could not write " + path;
    }
    return "";
}

void push_event_locked(json body) {
    Event e;
    e.seq = g.next_seq++;
    e.time_ms = now_ms();
    e.body = std::move(body);
    g.events.push_back(std::move(e));
    while (g.events.size() > kMaxEvents) g.events.pop_front();
}

// Logs each distinct error once rather than on every retry.
void set_hw_error_locked(const std::string& msg) {
    bool changed = g.hw_state != "error" || g.hw_error != msg;
    g.hw_state = "error";
    g.hw_error = msg;
    if (changed) {
        std::cout << "[RFID] " << msg << "\n" << std::flush;
        push_event_locked({{"kind", "error"}, {"message", msg}});
    }
}

// A missing file means no tags.
xerxes::TagStore load_tags(const std::string& path) {
    try {
        return xerxes::TagStore::load(path, /*require_tags_key=*/false);
    } catch (const xerxes::FileNotFound&) {
        return xerxes::TagStore{};
    }
}

// Temp file + rename, so a crash can't leave a half-written tags.json.
void save_tags(const xerxes::TagStore& tags, const std::string& path) {
    if (path == default_tags_path()) ensure_config_dir();
    std::string tmp = path + ".tmp";
    tags.save(tmp);
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        int err = errno;
        std::remove(tmp.c_str());
        throw xerxes::TagFileError("could not write '" + path + "': " + std::strerror(err));
    }
}

std::string current_tags_path() {
    std::lock_guard<std::mutex> lk(g.m);
    return g.tags_path;
}

// Like std::system() in `xerxes-rfid run`, but captures stdout+stderr.
// Returns the exit code, or 128+signal. Reading stops once the shell exits, so a
// backgrounded process that keeps the pipe open can't stall the reader.
int run_command(const std::string& command, std::string& output, bool& truncated) {
    output.clear();
    truncated = false;
    int fds[2];
    if (pipe(fds) != 0) {
        output = std::string("could not start command: ") + std::strerror(errno);
        return -1;
    }
    long max_fd = sysconf(_SC_OPEN_MAX);
    if (max_fd < 0 || max_fd > 65536) max_fd = 65536;

    pid_t pid = fork();
    if (pid < 0) {
        output = std::string("could not start command: ") + std::strerror(errno);
        close(fds[0]);
        close(fds[1]);
        return -1;
    }
    if (pid == 0) {
        // Only async-signal-safe calls after fork().
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) dup2(devnull, 0);
        dup2(fds[1], 1);
        dup2(fds[1], 2);
        for (long fd = 3; fd < max_fd; ++fd) close(static_cast<int>(fd));  // don't leak the HTTP socket
        signal(SIGPIPE, SIG_DFL);
        execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    close(fds[1]);

    int status = 0;
    bool exited = false;
    char buf[4096];
    while (true) {
        pollfd p{fds[0], POLLIN, 0};
        int r = poll(&p, 1, 200);
        if (r > 0) {
            ssize_t n = read(fds[0], buf, sizeof buf);
            if (n > 0) {
                size_t room = kMaxOutputBytes > output.size() ? kMaxOutputBytes - output.size() : 0;
                if (static_cast<size_t>(n) > room) truncated = true;
                output.append(buf, std::min(room, static_cast<size_t>(n)));
                continue;
            }
            if (n == 0 || (n < 0 && errno != EINTR && errno != EAGAIN)) break;
        }
        if (!exited && waitpid(pid, &status, WNOHANG) == pid) {
            exited = true;
            fcntl(fds[0], F_SETFL, O_NONBLOCK);
            ssize_t n;
            while ((n = read(fds[0], buf, sizeof buf)) > 0) {
                size_t room = kMaxOutputBytes > output.size() ? kMaxOutputBytes - output.size() : 0;
                if (static_cast<size_t>(n) > room) truncated = true;
                output.append(buf, std::min(room, static_cast<size_t>(n)));
            }
            break;
        }
    }
    close(fds[0]);
    if (!exited) {
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
}

void expire_locked() {
    auto now = Clock::now();
    if (g.scan == "waiting" && now >= g.scan_deadline) {
        g.scan = "timeout";
        g.cv.notify_all();
    }
    if (g.live_active && now - g.live_last_poll > kLiveIdleTimeout) {
        g.live_active = false;
        push_event_locked({{"kind", "info"}, {"message", "Live scan stopped (no browser watching)."}});
    }
}

bool consumers_locked() { return g.run_enabled || g.live_active || g.scan == "waiting"; }

// Not woken by unrelated notifications, so the pause after a tag is really taken.
void pause_locked(std::unique_lock<std::mutex>& lk, Clock::duration d) {
    g.cv.wait_for(lk, d, [] { return g.reopen || g.probe_requested || !consumers_locked(); });
}

void handle_tag_locked(std::unique_lock<std::mutex>& lk, const xerxes::TagReading& tag) {
    const std::string id = tag.id_formatted;
    g.last_tag = id;
    g.last_tag_ms = now_ms();

    // A tap answering a scan never runs the tag's command.
    if (g.scan == "waiting") {
        g.scan = "done";
        g.scan_id = id;
        g.scan_type = tag.type;
        std::cout << "[RFID] Scanned for registration: " << id << "\n" << std::flush;
        g.cv.notify_all();
        pause_locked(lk, kAfterTagPause);
        return;
    }

    bool live = g.live_active, run = g.run_enabled;
    std::string path = g.tags_path;

    // Read the file on every tap so edits apply without a restart.
    lk.unlock();
    std::string command, tags_error;
    bool known = false;
    try {
        std::lock_guard<std::mutex> tl(g_tags_mutex);
        xerxes::TagStore tags = load_tags(path);
        if (const std::string* c = tags.find(id)) {
            known = true;
            command = *c;
        }
    } catch (const xerxes::TagFileError& e) {
        tags_error = e.what();
    }
    lk.lock();

    if (live) {
        json ev = {{"kind", "live"}, {"id", id}, {"type", tag.type}, {"registered", known},
                   {"command", command}, {"detail", tag.describe()}};
        if (!tags_error.empty()) ev["tags_error"] = tags_error;
        push_event_locked(std::move(ev));
    }

    if (!run) {
        pause_locked(lk, kLivePollGap);
        return;
    }

    std::cout << "[RFID] " << id << "\n";
    if (!tags_error.empty()) {
        std::cout << "[RFID] Tag file error, no commands run: " << tags_error << "\n" << std::flush;
        push_event_locked({{"kind", "run"}, {"id", id}, {"registered", false},
                           {"tags_error", tags_error}});
        pause_locked(lk, kAfterTagPause);
        return;
    }
    if (!known) {
        std::cout << "[RFID] Tag unknown, no commands run\n" << std::flush;
        push_event_locked({{"kind", "run"}, {"id", id}, {"registered", false}});
        pause_locked(lk, kAfterTagPause);
        return;
    }

    std::cout << "[RFID] Tag known, running command: " << command << "\n" << std::flush;
    push_event_locked({{"kind", "run"}, {"id", id}, {"registered", true}, {"command", command}});
    g.busy_command = command;
    lk.unlock();

    std::string output;
    bool truncated = false;
    auto started = Clock::now();
    int code = run_command(command, output, truncated);
    auto took = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
    if (!output.empty()) std::cout << output << (output.back() == '\n' ? "" : "\n");
    std::cout << "[RFID] Command finished with exit code " << code << "\n" << std::flush;

    lk.lock();
    g.busy_command.clear();
    push_event_locked({{"kind", "result"}, {"id", id}, {"command", command}, {"exit_code", code},
                       {"output", output}, {"truncated", truncated}, {"duration_ms", took}});
    pause_locked(lk, kAfterTagPause);
}

void worker_main() {
    std::unique_ptr<xerxes::I2CBus> bus;
    std::unique_ptr<xerxes::RfidReader> reader;
    auto close_reader = [&] {
        reader.reset();
        bus.reset();
    };

    std::unique_lock<std::mutex> lk(g.m);
    while (true) {
        expire_locked();
        bool active = consumers_locked();

        if (g.reopen) {
            g.reopen = false;
            close_reader();
            if (g.hw_state == "ok") g.hw_state = "idle";
        }

        if (!active && !g.probe_requested) {
            if (reader) {
                close_reader();
                if (g.hw_state == "ok") g.hw_state = "idle";
            }
            g.cv.wait_for(lk, 1s);
            continue;
        }

        if (!reader) {
            int busno = g.bus, addr = g.address;
            lk.unlock();
            std::string err;
            try {
                bus = xerxes::open_i2c_bus(busno);
                reader = std::make_unique<xerxes::RfidReader>(*bus, static_cast<uint8_t>(addr));
            } catch (const std::exception& e) {
                err = e.what();
                close_reader();
            }
            lk.lock();
            if (err.empty()) {
                bool was_error = g.hw_state == "error";
                g.hw_state = "ok";
                g.hw_error.clear();
                std::cout << "[RFID] Reader ready on /dev/i2c-" << busno << " at " << hex2(addr) << "\n"
                          << std::flush;
                if (was_error) push_event_locked({{"kind", "info"}, {"message", "RFID module connected."}});
            } else {
                set_hw_error_locked(err);
            }
            if (g.probe_requested) {
                g.probe_requested = false;
                ++g.probe_generation;
                g.cv.notify_all();
            }
            if (!reader) g.cv.wait_for(lk, kRetryOpenGap, [] { return g.reopen || g.probe_requested; });
            continue;
        }

        if (g.probe_requested) {
            g.probe_requested = false;
            ++g.probe_generation;
            g.cv.notify_all();
            continue;
        }

        lk.unlock();
        xerxes::TagReading tag;
        std::string err;
        try {
            tag = reader->read_tag();
        } catch (const std::exception& e) {
            err = e.what();
        }
        lk.lock();

        if (!err.empty()) {
            close_reader();
            set_hw_error_locked(err);
            g.cv.wait_for(lk, kRetryOpenGap, [] { return g.reopen || g.probe_requested; });
            continue;
        }
        if (g.reopen) continue;  // settings changed mid-read

        if (tag.success)
            handle_tag_locked(lk, tag);
        else
            pause_locked(lk, (g.run_enabled || g.scan == "waiting") ? kRunPollGap : kLivePollGap);
    }
}

void ensure_worker_locked() {
    if (g.started) return;
    g.started = true;
    std::thread(worker_main).detach();
}

json tag_json(const std::string& id, const std::string& command) {
    return {{"id", id}, {"command", command}};
}

Reply error_reply(int code, const std::string& msg) {
    Reply r;
    r.http_status = code;
    r.body = {{"success", false}, {"error", msg}};
    return r;
}

Reply ok_reply(json extra = json::object()) {
    Reply r;
    r.body = std::move(extra);
    r.body["success"] = true;
    return r;
}

}  // namespace

std::string normalise_tag_id(const std::string& raw, std::string* id_out) {
    std::string s = xerxes::to_upper_ascii(xerxes::trim(raw));
    // Also accepts space/dash separators or none at all.
    std::string hex;
    for (char c : s) {
        if (c == ':' || c == ' ' || c == '-') continue;
        if (!std::isxdigit(static_cast<unsigned char>(c))) return "Tag IDs are hex bytes, e.g. DC:BD:6A:03.";
        hex += c;
    }
    if (hex.size() % 2 != 0 || hex.size() < 8 || hex.size() > 20)
        return "Tag IDs are 4 to 10 bytes long, e.g. DC:BD:6A:03 or 04:30:AD:00:64:1C:03.";
    std::string id;
    for (size_t i = 0; i < hex.size(); i += 2) {
        if (i) id += ':';
        id += hex.substr(i, 2);
    }
    if (id_out) *id_out = id;
    return "";
}

std::string validate_command(const std::string& command) {
    if (xerxes::trim(command).empty()) return "Command cannot be empty.";
    if (command.size() > kMaxCommandLen) return "Command is too long (1024 characters max).";
    for (unsigned char c : command)
        if (c < 0x20 || c == 0x7F) return "Command must be a single line.";
    return "";
}

void initialize() {
    std::lock_guard<std::mutex> lk(g.m);
    load_settings_locked();
    std::cout << "[RFID] tag file " << g.tags_path << ", /dev/i2c-" << g.bus << " at " << hex2(g.address)
              << ", run mode " << (g.run_enabled ? "on" : "off") << "\n";
    ensure_worker_locked();
}

Reply status() {
    std::lock_guard<std::mutex> lk(g.m);
    expire_locked();
    json scan = {{"state", g.scan}};
    if (g.scan == "waiting")
        scan["seconds_left"] = std::max<long>(0, static_cast<long>(std::chrono::duration_cast<std::chrono::seconds>(
                                                      g.scan_deadline - Clock::now()).count()));
    Reply r;
    r.body = {
        {"version", kVersion},
        {"bus", g.bus},
        {"address", hex2(g.address)},
        {"device", "/dev/i2c-" + std::to_string(g.bus)},
        {"tags_path", g.tags_path},
        {"default_tags_path", default_tags_path()},
        {"run_enabled", g.run_enabled},
        {"live_active", g.live_active},
        {"scan", scan},
        {"module", {{"state", g.hw_state}, {"error", g.hw_error}}},
        {"busy_command", g.busy_command},
        {"last_tag", g.last_tag},
        {"last_tag_ms", g.last_tag_ms},
        {"next_event", g.next_seq},
    };
    return r;
}

Reply set_run_enabled(bool enabled) {
    std::lock_guard<std::mutex> lk(g.m);
    ensure_worker_locked();
    if (g.run_enabled != enabled) {
        g.run_enabled = enabled;
        push_event_locked({{"kind", "info"},
                           {"message", enabled ? "Reader started: tag commands will run."
                                               : "Reader stopped: tag commands will not run."}});
        std::cout << "[RFID] run mode " << (enabled ? "on" : "off") << "\n" << std::flush;
    }
    std::string err = save_settings_locked();
    g.cv.notify_all();
    if (!err.empty()) return error_reply(500, err + " (the change applies until the backend restarts)");
    return ok_reply({{"run_enabled", g.run_enabled}});
}

Reply update_settings(int bus, int address, const std::string& tags_path_raw) {
    if (bus < 0 || bus > 1023) return error_reply(400, "I2C bus must be a number from 0 to 1023.");
    if (address < 0x03 || address > 0x77) return error_reply(400, "I2C address must be from 0x03 to 0x77.");
    std::string path = xerxes::trim(tags_path_raw);
    if (path.empty()) path = default_tags_path();
    for (unsigned char c : path)
        if (c < 0x20 || c == 0x7F) return error_reply(400, "Tag file path contains invalid characters.");
    if (path.size() > 512) return error_reply(400, "Tag file path is too long.");
    if (path[0] != '/') return error_reply(400, "Tag file path must be absolute, e.g. /root/.xerxes/tags.json.");
    if (path.size() < 6 || path.compare(path.size() - 5, 5, ".json") != 0)
        return error_reply(400, "Tag file must be a .json file.");

    std::lock_guard<std::mutex> lk(g.m);
    ensure_worker_locked();
    bool hw_changed = bus != g.bus || address != g.address;
    g.bus = bus;
    g.address = address;
    g.tags_path = path;
    if (hw_changed) {
        g.reopen = true;
        g.hw_state = "idle";
        g.hw_error.clear();
    }
    std::string err = save_settings_locked();
    g.cv.notify_all();
    std::cout << "[RFID] settings: /dev/i2c-" << bus << " at " << hex2(address) << ", tags " << path << "\n"
              << std::flush;
    if (!err.empty()) return error_reply(500, err);
    return ok_reply();
}

Reply probe() {
    std::unique_lock<std::mutex> lk(g.m);
    ensure_worker_locked();
    uint64_t gen = g.probe_generation;
    g.probe_requested = true;
    g.reopen = true;  // re-init so the result reflects the hardware now
    g.cv.notify_all();
    bool done = g.cv.wait_for(lk, 5s, [&] { return g.probe_generation != gen; });
    if (!done) return error_reply(504, "The reader did not respond in time.");
    if (g.hw_state == "error") return error_reply(502, g.hw_error);
    return ok_reply({{"message", "RFID module found on /dev/i2c-" + std::to_string(g.bus) + " at " +
                                     hex2(g.address) + "."}});
}

Reply list_tags() {
    std::string path = current_tags_path();
    try {
        std::lock_guard<std::mutex> tl(g_tags_mutex);
        xerxes::TagStore tags = load_tags(path);
        json list = json::array();
        for (const auto& e : tags.entries()) list.push_back(tag_json(e.first, e.second));
        return ok_reply({{"tags", list}, {"path", path}});
    } catch (const xerxes::TagFileError& e) {
        Reply r = error_reply(500, e.what());
        r.body["path"] = path;
        return r;
    }
}

Reply add_tag(const std::string& raw_id, const std::string& command) {
    std::string id, err = normalise_tag_id(raw_id, &id);
    if (err.empty()) err = validate_command(command);
    if (!err.empty()) return error_reply(400, err);
    std::string path = current_tags_path();
    try {
        std::lock_guard<std::mutex> tl(g_tags_mutex);
        xerxes::TagStore tags = load_tags(path);  // throws on a corrupt file, so it is never overwritten
        if (const std::string* existing = tags.find(id)) {
            Reply r = error_reply(409, "Tag already registered with command: \"" + *existing + "\"");
            r.body["id"] = id;
            r.body["existing_command"] = *existing;
            return r;
        }
        std::string cmd = xerxes::trim(command);
        tags.set(id, cmd);
        save_tags(tags, path);
        std::cout << "[RFID] Tag " << id << " added\n" << std::flush;
        return ok_reply({{"tag", tag_json(id, cmd)}, {"message", "Tag " + id + " added."}});
    } catch (const xerxes::TagFileError& e) {
        return error_reply(500, e.what());
    }
}

Reply edit_tag(const std::string& raw_id, const std::string& command) {
    std::string id, err = normalise_tag_id(raw_id, &id);
    if (err.empty()) err = validate_command(command);
    if (!err.empty()) return error_reply(400, err);
    std::string path = current_tags_path();
    try {
        std::lock_guard<std::mutex> tl(g_tags_mutex);
        xerxes::TagStore tags = load_tags(path);
        if (!tags.find(id)) return error_reply(404, "Tag " + id + " is not registered.");
        std::string cmd = xerxes::trim(command);
        tags.set(id, cmd);
        save_tags(tags, path);
        std::cout << "[RFID] Tag " << id << " updated\n" << std::flush;
        return ok_reply({{"tag", tag_json(id, cmd)}, {"message", "Tag " + id + " updated."}});
    } catch (const xerxes::TagFileError& e) {
        return error_reply(500, e.what());
    }
}

Reply delete_tag(const std::string& raw_id) {
    std::string id, err = normalise_tag_id(raw_id, &id);
    if (!err.empty()) return error_reply(400, err);
    std::string path = current_tags_path();
    try {
        std::lock_guard<std::mutex> tl(g_tags_mutex);
        xerxes::TagStore tags = load_tags(path);
        if (!tags.erase(id)) return error_reply(404, "Tag " + id + " is not registered.");
        save_tags(tags, path);
        std::cout << "[RFID] Tag " << id << " deleted\n" << std::flush;
        return ok_reply({{"message", "Tag " + id + " deleted."}});
    } catch (const xerxes::TagFileError& e) {
        return error_reply(500, e.what());
    }
}

Reply start_scan() {
    std::lock_guard<std::mutex> lk(g.m);
    ensure_worker_locked();
    g.scan = "waiting";
    g.scan_id.clear();
    g.scan_type.clear();
    g.scan_deadline = Clock::now() + kScanTimeout;
    g.cv.notify_all();
    return ok_reply({{"timeout_s", std::chrono::duration_cast<std::chrono::seconds>(kScanTimeout).count()}});
}

Reply scan_state() {
    std::string state, id, type, path;
    long left = 0;
    {
        std::lock_guard<std::mutex> lk(g.m);
        expire_locked();
        state = g.scan;
        id = g.scan_id;
        type = g.scan_type;
        path = g.tags_path;
        if (state == "waiting")
            left = std::max<long>(0, static_cast<long>(std::chrono::duration_cast<std::chrono::seconds>(
                                         g.scan_deadline - Clock::now()).count()));
    }
    Reply r = ok_reply({{"state", state}});
    if (state == "waiting") {
        r.body["seconds_left"] = left;
        std::lock_guard<std::mutex> lk(g.m);
        r.body["module"] = {{"state", g.hw_state}, {"error", g.hw_error}};
    }
    if (state == "done") {
        r.body["id"] = id;
        r.body["type"] = type;
        try {
            std::lock_guard<std::mutex> tl(g_tags_mutex);
            xerxes::TagStore tags = load_tags(path);
            const std::string* c = tags.find(id);
            r.body["registered"] = c != nullptr;
            if (c) r.body["existing_command"] = *c;
        } catch (const xerxes::TagFileError& e) {
            r.body["registered"] = false;
            r.body["tags_error"] = e.what();
        }
    }
    return r;
}

Reply cancel_scan() {
    std::lock_guard<std::mutex> lk(g.m);
    if (g.scan == "waiting") g.scan = "cancelled";
    g.cv.notify_all();
    return ok_reply();
}

Reply start_live() {
    std::lock_guard<std::mutex> lk(g.m);
    ensure_worker_locked();
    if (!g.live_active) {
        g.live_active = true;
        push_event_locked({{"kind", "info"}, {"message", "Live scan started."}});
    }
    g.live_last_poll = Clock::now();
    g.cv.notify_all();
    return ok_reply({{"next_event", g.next_seq}});
}

Reply stop_live() {
    std::lock_guard<std::mutex> lk(g.m);
    if (g.live_active) {
        g.live_active = false;
        push_event_locked({{"kind", "info"}, {"message", "Live scan stopped."}});
    }
    g.cv.notify_all();
    return ok_reply();
}

Reply events(uint64_t since) {
    std::lock_guard<std::mutex> lk(g.m);
    g.live_last_poll = Clock::now();
    expire_locked();
    json list = json::array();
    for (const Event& e : g.events) {
        if (e.seq <= since) continue;
        json item = e.body;
        item["seq"] = e.seq;
        item["time_ms"] = e.time_ms;
        list.push_back(std::move(item));
    }
    Reply r;
    r.body = {{"events", list},
              {"next", g.next_seq - 1},
              {"live_active", g.live_active},
              {"run_enabled", g.run_enabled},
              {"busy_command", g.busy_command},
              {"module", {{"state", g.hw_state}, {"error", g.hw_error}}},
              {"last_tag", g.last_tag}};
    return r;
}

}  // namespace rfid_manager
