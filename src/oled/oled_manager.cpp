// Backend service for the SSD1306 OLED (see include/oled_manager.h).
// A single worker thread owns the display and redraws it every kRefreshGap while
// it is on. HTTP handlers just update `g` and notify the worker. If the display
// can't be reached the settings are kept and the worker keeps retrying, then
// shows them again once the display is back (FR-6).
#include "oled_manager.h"

#include "screens.h"
#include "ssd1306.h"
#include "sysinfo.h"

#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>


namespace oled_manager {
namespace {

using nlohmann::json;

const std::chrono::seconds kRefreshGap(2);    // same as the original xerxes_oled loop
const std::chrono::seconds kRetryOpenGap(3);
const std::chrono::seconds kProbeTimeout(5);
const int kDefaultBus = 1;
const int kDefaultAddress = 0x3C;

struct State {
    std::mutex m;
    std::condition_variable cv;

    // Persisted in oled.conf
    bool enabled = true;
    std::string mode = "stats";  // stats | qr
    std::vector<std::string> stats;
    int bus = kDefaultBus;
    int address = kDefaultAddress;

    int panel_port = 8000;

    std::string hw_state = "idle";  // idle | ok | error
    std::string hw_code;            // no_bus | no_device | write_failed | unsupported
    std::string hw_error;
    bool redraw = false;            // settings changed, so don't wait for the next refresh
    bool reopen = false;
    bool probe_requested = false;
    uint64_t probe_generation = 0;

    bool started = false;
} g;

std::string hex2(int v) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "0x%02X", v & 0xFF);
    return buf;
}

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string device_path(int bus) { return "/dev/i2c-" + std::to_string(bus); }

std::string config_dir() {
    const char* custom = std::getenv("XERXES_CONFIG_DIR");
    if (custom && *custom) return custom;
    const char* home = std::getenv("HOME");
    return std::string(home && *home ? home : "/tmp") + "/.xerxes";
}

std::string settings_path() { return config_dir() + "/oled.conf"; }

void ensure_config_dir() {
    std::string dir = config_dir();
    if (mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST)
        std::cerr << "[OLED] could not create " << dir << ": " << std::strerror(errno) << "\n";
}

// What the original xerxes_oled program showed
std::vector<std::string> default_stats() { return {"hostname", "ip", "dns", "cpu_temp"}; }

// Puts valid keys in display order without duplicates. Returns "" or an error code.
std::string normalise_stats(const std::vector<std::string>& in, std::vector<std::string>* out) {
    for (const std::string& key : in)
        if (!isStat(key)) return "invalid_stat";
    out->clear();
    for (const std::string& key : availableStats())
        for (const std::string& want : in)
            if (want == key) {
                out->push_back(key);
                break;
            }
    return "";
}

std::string join_stats(const std::vector<std::string>& stats) {
    std::string s;
    for (const std::string& key : stats) s += (s.empty() ? "" : ",") + key;
    return s;
}

void load_settings_locked() {
    g.stats = default_stats();
    FILE* f = std::fopen(settings_path().c_str(), "r");
    if (!f) return;
    char line[1024];
    while (std::fgets(line, sizeof line, f)) {
        std::string l = trim(line);
        size_t eq = l.find('=');
        if (l.empty() || l[0] == '#' || eq == std::string::npos) continue;
        std::string k = l.substr(0, eq), v = l.substr(eq + 1);
        char* end = nullptr;
        if (k == "enabled") {
            g.enabled = (v == "1" || v == "true");
        } else if (k == "mode") {
            if (v == "stats" || v == "qr") g.mode = v;
        } else if (k == "stats") {
            std::vector<std::string> stats;
            if (normalise_stats(split_stats(v), &stats).empty()) g.stats = stats;
        } else if (k == "bus") {
            long n = std::strtol(v.c_str(), &end, 0);
            if (*end == '\0' && n >= 0 && n <= 1023) g.bus = static_cast<int>(n);
        } else if (k == "address") {
            long n = std::strtol(v.c_str(), &end, 0);
            if (*end == '\0' && n >= 0x03 && n <= 0x77) g.address = static_cast<int>(n);
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
    std::fprintf(f, "# Xerxes OLED display settings (managed by the web panel)\n"
                    "enabled=%d\nmode=%s\nstats=%s\nbus=%d\naddress=%s\n",
                 g.enabled ? 1 : 0, g.mode.c_str(), join_stats(g.stats).c_str(), g.bus,
                 hex2(g.address).c_str());
    bool ok = std::fflush(f) == 0;
    ok = (std::fclose(f) == 0) && ok;
    if (!ok || std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        return "Could not write " + path;
    }
    return "";
}

// Logs each distinct error once rather than on every retry.
void set_hw_error_locked(const std::string& code, const std::string& msg) {
    bool changed = g.hw_state != "error" || g.hw_code != code || g.hw_error != msg;
    g.hw_state = "error";
    g.hw_code = code;
    g.hw_error = msg;
    if (changed) std::cout << "[OLED] " << msg << " (" << code << ")\n" << std::flush;
}

// "" while there is no IPv4 address, so the QR screen can say so
std::string panel_url(int port) {
    std::string ip = getIP();
    if (ip == "No IP") return "";
    return "http://" + ip + (port == 80 ? "" : ":" + std::to_string(port));
}

void render(SSD1306& oled, const std::string& mode, const std::vector<std::string>& stats, int port) {
    oled.clear();
    if (mode == "qr")
        drawQR(oled, panel_url(port));
    else
        drawStats(oled, stats);
}

// Fills code and msg when the display can't be used.
void open_display(SSD1306& oled, int bus, int addr, std::string& code, std::string& msg) {
#ifdef __linux__
    std::string dev = device_path(bus);
    if (access(dev.c_str(), F_OK) != 0) {
        code = "no_bus";
        msg = "could not open " + dev + ": " + std::strerror(errno) + " (is I2C enabled?)";
        return;
    }
    errno = 0;
    if (!oled.begin(dev.c_str(), addr)) {
        code = "no_device";
        msg = "no response from the display at " + hex2(addr) + " on " + dev +
              (errno ? std::string(": ") + std::strerror(errno) : "") + ", check wiring";
    }
#else
    (void)oled;
    (void)bus;
    (void)addr;
    code = "unsupported";
    msg = "the display needs Linux i2c-dev, only the preview works on this system";
#endif
}

// Blanks the screen and turns it off before letting go of it. Does I2C writes,
// so call it unlocked.
void release(std::unique_ptr<SSD1306>& oled) {
    if (!oled) return;
    oled->clear();
    oled->display();
    oled->off();
    oled.reset();
}

void finish_probe_locked() {
    if (!g.probe_requested) return;
    g.probe_requested = false;
    ++g.probe_generation;
    g.cv.notify_all();
}

void worker_main() {
    std::unique_ptr<SSD1306> oled;

    std::unique_lock<std::mutex> lk(g.m);
    while (true) {
        bool wanted = g.enabled || g.probe_requested;

        if (oled && (g.reopen || !wanted)) {
            lk.unlock();
            release(oled);
            lk.lock();
            if (g.hw_state == "ok") g.hw_state = "idle";
            continue;
        }
        g.reopen = false;

        if (!wanted) {
            g.cv.wait_for(lk, std::chrono::seconds(1), [] { return g.enabled || g.probe_requested; });
            continue;
        }

        if (!oled) {
            int bus = g.bus, addr = g.address;
            lk.unlock();
            std::unique_ptr<SSD1306> fresh(new SSD1306());
            std::string code, msg;
            open_display(*fresh, bus, addr, code, msg);
            if (code.empty()) oled = std::move(fresh);
            lk.lock();
            if (oled) {
                bool was_error = g.hw_state == "error";
                g.hw_state = "ok";
                g.hw_code.clear();
                g.hw_error.clear();
                std::cout << "[OLED] Display ready on " << device_path(bus) << " at " << hex2(addr)
                          << (was_error ? ", showing the saved settings again" : "") << "\n" << std::flush;
            } else {
                set_hw_error_locked(code, msg);
            }
            finish_probe_locked();
            if (!oled) g.cv.wait_for(lk, kRetryOpenGap, [] { return g.reopen || g.probe_requested; });
            continue;
        }

        std::string mode = g.mode;
        std::vector<std::string> stats = g.stats;
        int port = g.panel_port, bus = g.bus;
        g.redraw = false;
        lk.unlock();
        render(*oled, mode, stats, port);
        errno = 0;
        bool ok = oled->display();
        std::string msg;
        if (!ok) {
            msg = "lost contact with the display on " + device_path(bus) +
                  (errno ? std::string(": ") + std::strerror(errno) : "");
            oled.reset();  // no point blanking a display we can't reach
        }
        lk.lock();

        if (!ok) {
            set_hw_error_locked("write_failed", msg);
            g.cv.wait_for(lk, kRetryOpenGap, [] { return g.reopen || g.probe_requested; });
            continue;
        }
        g.cv.wait_for(lk, kRefreshGap, [] { return g.redraw || g.reopen || g.probe_requested || !g.enabled; });
    }
}

void ensure_worker_locked() {
    if (g.started) return;
    g.started = true;
    std::thread(worker_main).detach();
}

Reply error_reply(int http_status, const std::string& code, const std::string& msg) {
    Reply r;
    r.http_status = http_status;
    r.body = {{"success", false}, {"code", code}, {"error", msg}};
    return r;
}

Reply ok_reply(json extra = json::object()) {
    Reply r;
    r.body = std::move(extra);
    r.body["success"] = true;
    return r;
}

Reply save_failed(const std::string& err) {
    return error_reply(500, "save_failed", err + " (the change applies until the backend restarts)");
}

}  // namespace

std::vector<std::string> split_stats(const std::string& csv) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= csv.size()) {
        size_t comma = csv.find(',', start);
        if (comma == std::string::npos) comma = csv.size();
        std::string key = trim(csv.substr(start, comma - start));
        if (!key.empty()) out.push_back(key);
        start = comma + 1;
    }
    return out;
}

void initialize(int panel_port) {
    std::lock_guard<std::mutex> lk(g.m);
    g.panel_port = panel_port;
    load_settings_locked();
    std::cout << "[OLED] " << device_path(g.bus) << " at " << hex2(g.address) << ", "
              << (g.enabled ? "showing " + (g.mode == "qr" ? std::string("QR code") : join_stats(g.stats))
                            : std::string("off"))
              << "\n";
    ensure_worker_locked();
}

Reply status() {
    int port;
    {
        std::lock_guard<std::mutex> lk(g.m);
        port = g.panel_port;
    }
    std::string url = panel_url(port);  // reads the network, so not under the lock

    std::lock_guard<std::mutex> lk(g.m);
    Reply r;
    r.body = {
        {"enabled", g.enabled},
        {"mode", g.mode},
        {"stats", g.stats},
        {"available_stats", availableStats()},
        {"bus", g.bus},
        {"address", hex2(g.address)},
        {"device", device_path(g.bus)},
        {"default_bus", kDefaultBus},
        {"default_address", hex2(kDefaultAddress)},
        {"panel_url", url},
        {"display", {{"state", g.hw_state}, {"code", g.hw_code}, {"error", g.hw_error}}},
    };
    return r;
}

Reply set_enabled(bool enabled) {
    std::lock_guard<std::mutex> lk(g.m);
    ensure_worker_locked();
    if (g.enabled != enabled) {
        g.enabled = enabled;
        g.redraw = true;
        std::cout << "[OLED] display " << (enabled ? "on" : "off") << "\n" << std::flush;
    }
    std::string err = save_settings_locked();
    g.cv.notify_all();
    if (!err.empty()) return save_failed(err);
    return ok_reply({{"enabled", g.enabled}});
}

Reply update_display(const std::string& mode, const std::vector<std::string>& stats_in) {
    if (mode != "stats" && mode != "qr") return error_reply(400, "invalid_mode", "Mode must be \"stats\" or \"qr\".");
    std::vector<std::string> stats;
    if (!normalise_stats(stats_in, &stats).empty())
        return error_reply(400, "invalid_stat", "Unknown stat. See available_stats in /api/oled/status.");
    if (mode == "stats" && stats.empty()) return error_reply(400, "no_stats", "Pick at least one stat to show.");

    std::lock_guard<std::mutex> lk(g.m);
    ensure_worker_locked();
    g.mode = mode;
    g.stats = stats;  // kept in QR mode too, so switching back restores them
    g.redraw = true;
    std::string err = save_settings_locked();
    g.cv.notify_all();
    std::cout << "[OLED] showing " << (mode == "qr" ? std::string("QR code") : join_stats(stats)) << "\n"
              << std::flush;
    if (!err.empty()) return save_failed(err);
    return ok_reply({{"mode", g.mode}, {"stats", g.stats}});
}

Reply update_settings(int bus, int address) {
    if (bus < 0 || bus > 1023) return error_reply(400, "invalid_bus", "I2C bus must be a number from 0 to 1023.");
    if (address < 0x03 || address > 0x77)
        return error_reply(400, "invalid_address", "I2C address must be from 0x03 to 0x77.");

    std::lock_guard<std::mutex> lk(g.m);
    ensure_worker_locked();
    if (bus != g.bus || address != g.address) {
        g.bus = bus;
        g.address = address;
        g.reopen = true;
        g.hw_state = "idle";
        g.hw_code.clear();
        g.hw_error.clear();
    }
    std::string err = save_settings_locked();
    g.cv.notify_all();
    std::cout << "[OLED] settings: " << device_path(bus) << " at " << hex2(address) << "\n" << std::flush;
    if (!err.empty()) return save_failed(err);
    return ok_reply();
}

Reply probe() {
    std::unique_lock<std::mutex> lk(g.m);
    ensure_worker_locked();
    uint64_t gen = g.probe_generation;
    g.probe_requested = true;
    g.reopen = true;  // re-init so the result reflects the hardware now
    g.cv.notify_all();
    bool done = g.cv.wait_for(lk, kProbeTimeout, [&] { return g.probe_generation != gen; });
    if (!done) return error_reply(504, "probe_timeout", "The display did not respond in time.");
    if (g.hw_state == "error") return error_reply(502, g.hw_code, g.hw_error);
    return ok_reply({{"device", device_path(g.bus)}, {"address", hex2(g.address)}});
}

Reply preview(const std::string& mode_in, const std::vector<std::string>& stats_in) {
    std::string mode = mode_in;
    std::vector<std::string> stats;
    int port;
    {
        std::lock_guard<std::mutex> lk(g.m);
        port = g.panel_port;
        if (mode.empty()) {
            mode = g.mode;
            stats = g.stats;
        }
    }
    if (!mode_in.empty()) {
        if (mode != "stats" && mode != "qr")
            return error_reply(400, "invalid_mode", "Mode must be \"stats\" or \"qr\".");
        if (!normalise_stats(stats_in, &stats).empty())
            return error_reply(400, "invalid_stat", "Unknown stat. See available_stats in /api/oled/status.");
    }

    SSD1306 screen;  // never begin()'d, so it only draws into its buffer
    render(screen, mode, stats, port);

    // The buffer is in pages (each byte is 8 pixels down), the panel wants rows
    static const char* digits = "0123456789abcdef";
    const uint8_t* buf = screen.getBuffer();
    json rows = json::array();
    for (int y = 0; y < SSD1306::HEIGHT; y++) {
        std::string hex;
        for (int xb = 0; xb < SSD1306::WIDTH / 8; xb++) {
            int byte = 0;
            for (int bit = 0; bit < 8; bit++) {
                int x = xb * 8 + bit;
                if (buf[x + (y / 8) * SSD1306::WIDTH] & (1 << (y % 8))) byte |= 0x80 >> bit;
            }
            hex += digits[byte >> 4];
            hex += digits[byte & 0x0F];
        }
        rows.push_back(hex);
    }
    // Copies, because json takes a reference and the class constants have no definition to point to
    int width = SSD1306::WIDTH, height = SSD1306::HEIGHT;
    return ok_reply({{"mode", mode}, {"stats", stats}, {"width", width}, {"height", height}, {"rows", rows}});
}

}  // namespace oled_manager
