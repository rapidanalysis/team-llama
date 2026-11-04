#include "newt_manager.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <mutex>
#include <pwd.h>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace newt_manager {

namespace {

const char* CONTAINER      = "xerxes-newt";
const char* IMAGE          = "fosrl/newt:latest";
// The image the container ran before the last update, kept so it can be
// restored. Tagging it also stops `docker image prune` from deleting it.
const char* PREVIOUS_IMAGE = "fosrl/newt:xerxes-previous";

const int DEFAULT_UPDATE_WAIT_SECONDS = 60;
const long long AUTO_UPDATE_PERIOD    = 24 * 60 * 60;   // once a day

// One Docker operation at a time: the daily updater and the web UI must not
// recreate the container at the same moment.
std::mutex& op_mutex()       { static std::mutex m; return m; }
// Guards read-modify-write of the update settings file.
std::mutex& settings_mutex() { static std::mutex m; return m; }

// ── helpers ───────────────────────────────────────────────────────────────

bool contains(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

std::string lower(std::string s) {
    for (size_t i = 0; i < s.size(); ++i)
        s[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
    return s;
}

bool all_alnum(const std::string& s) {
    for (char c : s)
        if (!std::isalnum(static_cast<unsigned char>(c))) return false;
    return true;
}

// Single-quotes a string for /bin/sh, escaping embedded single quotes.
std::string shell_quote(const std::string& s) {
    std::string r = "'";
    for (char c : s) {
        if (c == '\'') r += "'\\''";
        else           r += c;
    }
    r += '\'';
    return r;
}

// Runs a shell command, returns combined stdout+stderr and the exit code.
std::string run_capture(const std::string& cmd, int* exit_code) {
    std::string full = cmd + " 2>&1";
    FILE* pipe = popen(full.c_str(), "r");
    if (!pipe) {
        if (exit_code) *exit_code = -1;
        return "Failed to start process.";
    }
    std::string out;
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe) != nullptr) out += buf;
    int st = pclose(pipe);
    if (exit_code) *exit_code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    return out;
}

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// "docker" or "sudo -n docker", from $XERXES_DOCKER.
std::string docker() { return docker_command_for(std::getenv("XERXES_DOCKER")); }

std::string current_user() {
    struct passwd* pw = getpwuid(geteuid());
    return pw && pw->pw_name ? pw->pw_name : "";
}

// Config lives outside the web root and the repo so the secret can't be
// served or committed by accident.
std::string config_dir() {
    const char* custom = std::getenv("XERXES_CONFIG_DIR");
    if (custom && *custom) return custom;
    const char* home = std::getenv("HOME");
    if (home && *home) return std::string(home) + "/.xerxes";
    // systemd services may run without $HOME: use the account's home folder
    struct passwd* pw = getpwuid(geteuid());
    if (pw && pw->pw_dir && *pw->pw_dir) return std::string(pw->pw_dir) + "/.xerxes";
    return "/tmp/.xerxes";
}

std::string config_path()   { return config_dir() + "/newt.env"; }
std::string settings_path() { return config_dir() + "/newt-update.conf"; }

std::map<std::string, std::string> load_kv(const std::string& path) {
    std::map<std::string, std::string> kv;
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return kv;
    char line[1024];
    while (std::fgets(line, sizeof(line), f)) {
        std::string l = trim(line);
        size_t eq = l.find('=');
        if (eq == std::string::npos || l.empty() || l[0] == '#') continue;
        kv[l.substr(0, eq)] = l.substr(eq + 1);
    }
    std::fclose(f);
    return kv;
}

// Writes KEY=value lines with mode 0600. Returns "" on success.
std::string save_kv(const std::string& path, const std::string& body) {
    std::string dir = config_dir();
    if (mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST)
        return "Could not create config directory " + dir + ".";

    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return "Could not write " + path + ".";
    fchmod(fd, 0600);  // tighten permissions if the file already existed
    ssize_t n = write(fd, body.data(), body.size());
    close(fd);
    if (n != static_cast<ssize_t>(body.size())) return "Could not write " + path + ".";
    return "";
}

std::map<std::string, std::string> load_config() { return load_kv(config_path()); }

std::string save_config(const std::string& endpoint,
                        const std::string& newt_id,
                        const std::string& secret) {
    return save_kv(config_path(),
                   "PANGOLIN_ENDPOINT=" + endpoint + "\n"
                   "NEWT_ID="           + newt_id  + "\n"
                   "NEWT_SECRET="       + secret   + "\n");
}

bool is_configured() { return !load_config()["NEWT_ID"].empty(); }

// ── update settings ($CONFIG_DIR/newt-update.conf) ────────────────────────

std::map<std::string, std::string> load_settings() {
    std::lock_guard<std::mutex> g(settings_mutex());
    return load_kv(settings_path());
}

// Changes some settings and keeps the rest.
void update_settings(const std::map<std::string, std::string>& changes) {
    std::lock_guard<std::mutex> g(settings_mutex());
    auto kv = load_kv(settings_path());
    for (auto& c : changes) {
        if (c.second.empty()) kv.erase(c.first);
        else                  kv[c.first] = c.second;
    }
    std::string body;
    for (auto& e : kv) body += e.first + "=" + e.second + "\n";
    std::string err = save_kv(settings_path(), body);
    if (!err.empty()) std::cerr << "[Newt] " << err << "\n";
}

// Keeps a message to one short line for the settings file and the UI.
std::string one_line(const std::string& s) {
    std::string r;
    for (char c : s) r += (c == '\n' || c == '\r' || c == '=') ? ' ' : c;
    if (r.size() > 300) r = r.substr(0, 300);
    return r;
}

// Records the outcome of an update check so the UI can show it.
void record_check(const std::string& message, bool update_available) {
    std::map<std::string, std::string> c;
    c["LAST_CHECK"]       = std::to_string(static_cast<long long>(std::time(nullptr)));
    c["LAST_RESULT"]      = one_line(message);
    c["UPDATE_AVAILABLE"] = update_available ? "1" : "0";
    update_settings(c);
}

// ── Docker queries ────────────────────────────────────────────────────────

std::string access_message(const std::string& state, const std::string& user) {
    std::string who = user.empty() ? "the backend's user" : "'" + user + "'";
    if (state == "permission_denied")
        return "The backend runs as " + who + ", which isn't allowed to use Docker.";
    if (state == "sudo_denied")
        return "The backend is set to use 'sudo -n docker', but sudo asked for a password.";
    if (state == "daemon_down")   return "Docker is installed but not running.";
    if (state == "not_installed") return "Docker is not installed on this device.";
    if (state == "ok")            return "Docker is available.";
    return "Docker returned an error.";
}

DockerAccess docker_access_now() {
    DockerAccess a;
    int code = 0;
    std::string out = run_capture(docker() + " version --format '{{.Server.Version}}'", &code);
    a.state   = classify_docker_output(out, code);
    a.user    = current_user();
    a.message = access_message(a.state, a.user);
    a.detail  = trim(out);
    a.fixes   = docker_fixes(a.state, a.user);
    return a;
}

// Image ID the xerxes-newt container was created from; "" if no container.
std::string container_image_id() {
    int code = 0;
    std::string out = trim(run_capture(docker() + " inspect -f '{{.Image}}' " + CONTAINER, &code));
    return code == 0 ? out : "";
}

// ID of a local image; "" if it isn't on this device.
std::string image_id(const char* ref) {
    int code = 0;
    std::string out = trim(run_capture(docker() + " image inspect -f '{{.Id}}' " + ref, &code));
    return code == 0 ? out : "";
}

// Version from the start of the container's log (Newt prints it on startup).
std::string container_version() {
    int code = 0;
    return parse_version(run_capture(
        docker() + " logs " + CONTAINER + " 2>&1 | head -n 40", &code));
}

std::string container_state() {
    int code = 0;
    std::string out = trim(run_capture(
        docker() + " inspect -f '{{.State.Status}}' " + CONTAINER, &code));
    return code == 0 ? out : "absent";
}

// Tunnel state of a running container ("" if it isn't running).
std::string tunnel_state() {
    if (container_state() != "running") return "";
    return classify_logs(logs(200), nullptr);
}

Result start_container(const std::string& image) {
    Result r;
    int code = 0;
    // Replace any previous instance so there is only ever one Newt per device.
    run_capture(docker() + " rm -f " + CONTAINER, &code);
    std::string cmd = docker() + " run -d --name " + CONTAINER +
                      " --restart unless-stopped --env-file " + shell_quote(config_path()) +
                      " " + image;
    r.output  = run_capture(cmd, &code);
    r.success = (code == 0);
    if (!r.success) {
        std::string state = classify_docker_output(r.output, code);
        r.error = (state == "error")
            ? "Docker command failed (exit code " + std::to_string(code) + ")."
            : access_message(state, current_user());
    }
    return r;
}

// After an update, waits for the tunnel to come back. True if it connected.
bool wait_for_tunnel(int seconds) {
    for (int waited = 0; waited < seconds; waited += 3) {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        if (tunnel_state() == "connected") return true;
    }
    return false;
}

int update_wait_seconds() {
    const char* v = std::getenv("XERXES_NEWT_UPDATE_WAIT");
    int n = v ? std::atoi(v) : 0;
    return (n >= 3 && n <= 600) ? n : DEFAULT_UPDATE_WAIT_SECONDS;
}

// Image to start Newt from: the latest, unless the user rolled back from it.
std::string image_to_run() {
    std::string skipped = load_settings()["SKIP_IMAGE"];
    if (!skipped.empty() && image_id(IMAGE) == skipped && !image_id(PREVIOUS_IMAGE).empty())
        return PREVIOUS_IMAGE;
    return IMAGE;
}

std::string version_text(const std::string& v) { return v.empty() ? "" : " " + v; }

bool valid_user_name(const std::string& u) {
    if (u.empty() || u.size() > 32) return false;
    if (!(std::islower(static_cast<unsigned char>(u[0])) || u[0] == '_')) return false;
    for (char c : u)
        if (!(std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) ||
              c == '_' || c == '-'))
            return false;
    return true;
}

} // namespace

// ── validation ────────────────────────────────────────────────────────────

std::string validate(const std::string& endpoint,
                     const std::string& newt_id,
                     const std::string& secret,
                     bool allow_empty_secret) {
    // Endpoint: http(s)://host[:port][/path] using a strict character allowlist
    if (endpoint.empty())
        return "Pangolin endpoint is required.";
    if (endpoint.size() > 255)
        return "Pangolin endpoint is too long.";
    std::string rest;
    if (endpoint.rfind("https://", 0) == 0)      rest = endpoint.substr(8);
    else if (endpoint.rfind("http://", 0) == 0)  rest = endpoint.substr(7);
    else
        return "Pangolin endpoint must start with https:// (e.g. https://pangolin.example.com).";
    if (rest.empty() || !std::isalnum(static_cast<unsigned char>(rest[0])))
        return "Pangolin endpoint is missing a host name.";
    for (char c : rest) {
        if (!std::isalnum(static_cast<unsigned char>(c)) &&
            c != '.' && c != '-' && c != ':' && c != '/' && c != '_')
            return std::string("Pangolin endpoint contains an invalid character '") + c + "'.";
    }
    if (contains(rest, ".."))
        return "Pangolin endpoint is not a valid address.";

    // Newt ID and secret: letters and digits only (Pangolin generates these)
    if (newt_id.empty())
        return "Newt ID is required.";
    if (newt_id.size() > 64 || !all_alnum(newt_id))
        return "Newt ID must be up to 64 letters and digits.";

    if (secret.empty()) {
        if (!allow_empty_secret) return "Newt secret is required.";
    } else if (secret.size() > 128 || !all_alnum(secret)) {
        return "Newt secret must be up to 128 letters and digits.";
    }
    return "";
}

// ── pure helpers ──────────────────────────────────────────────────────────

std::string classify_logs(const std::string& log, std::string* detail) {
    std::string state = "connecting";
    std::string why;

    std::istringstream in(log);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty()) continue;

        if (contains(line, "Tunnel connection to server established")) {
            state = "connected";   why = line;
        } else if (contains(line, "failed to get token") || contains(line, "No newt found") ||
                   contains(line, "status code: 401")    || contains(line, "status code: 403")) {
            state = "auth_failed"; why = line;
        } else if (contains(line, "Failed to connect")) {
            state = "error";       why = line;
        }
        // Other ERROR/WARN lines are ignored: Newt logs transient issues
        // (e.g. health checks) without the tunnel going down.
    }
    if (detail) *detail = why;
    return state;
}

std::string parse_version(const std::string& log) {
    std::string version;
    std::istringstream in(log);
    std::string line;
    while (std::getline(in, line)) {
        size_t p = line.find("Newt version");
        if (p == std::string::npos) continue;
        p += 12;  // strlen("Newt version")
        while (p < line.size() && (line[p] == ' ' || line[p] == ':')) ++p;
        if (p < line.size() && (line[p] == 'v' || line[p] == 'V')) ++p;
        if (p >= line.size() || !std::isdigit(static_cast<unsigned char>(line[p]))) continue;
        size_t e = p;
        while (e < line.size() && (std::isalnum(static_cast<unsigned char>(line[e])) ||
                                   line[e] == '.' || line[e] == '-' || line[e] == '+'))
            ++e;
        version = line.substr(p, std::min<size_t>(e - p, 32));  // latest line wins
    }
    return version;
}

bool update_needed(const std::string& running_image,
                   const std::string& latest_image,
                   const std::string& skipped_image,
                   bool automatic) {
    if (running_image.empty() || latest_image.empty()) return false;
    if (running_image == latest_image) return false;
    if (automatic && latest_image == skipped_image) return false;
    return true;
}

bool auto_update_due(bool enabled, bool configured, long long now, long long last_check) {
    if (!enabled || !configured) return false;
    if (last_check <= 0 || last_check > now) return true;   // never checked, or clock went back
    return now - last_check >= AUTO_UPDATE_PERIOD;
}

std::string classify_docker_output(const std::string& output, int exit_code) {
    if (exit_code == 0) return "ok";
    std::string o = lower(output);
    if (contains(o, "a password is required") || contains(o, "is not allowed to run sudo") ||
        contains(o, "may not run sudo") || contains(o, "not in the sudoers"))
        return "sudo_denied";
    if (contains(o, "permission denied"))
        return "permission_denied";
    if (contains(o, "cannot connect to the docker daemon") || contains(o, "is the docker daemon running"))
        return "daemon_down";
    if (exit_code == 127 || contains(o, "command not found") || contains(o, "docker: not found") ||
        contains(o, "no such file or directory"))
        return "not_installed";
    return "error";
}

std::vector<Fix> docker_fixes(const std::string& state, const std::string& user_in) {
    std::string user = valid_user_name(user_in) ? user_in : "<your-user>";
    std::vector<Fix> fixes;

    Fix sudoers;
    sudoers.title = "Allow only the docker command through sudo";
    sudoers.why   = "Keeps the backend as an ordinary user, and the panel calls 'sudo -n docker'. "
                    "This is still root-level access to Docker, so use it only on a device you control.";
    sudoers.commands.push_back("echo '" + user + " ALL=(root) NOPASSWD: /usr/bin/docker' | "
                               "sudo tee /etc/sudoers.d/xerxes-docker");
    sudoers.commands.push_back("sudo chmod 0440 /etc/sudoers.d/xerxes-docker");
    sudoers.commands.push_back("XERXES_DOCKER='sudo -n docker' ./xerxes_backend");
    sudoers.after = "Stop the backend (Ctrl+C), then start it again with the last command.";

    if (state == "permission_denied") {
        Fix service;
        service.title = "Run the panel as a system service (recommended on the Xerxes Pi)";
        service.why   = "The service runs as root, so it can always use Docker, and it starts by itself after a reboot.";
        service.commands.push_back("sudo bash deploy_systemd.sh");
        service.after = "Run it from the folder that contains xerxes_backend and public/, then reload this page.";
        fixes.push_back(service);

        Fix group;
        group.title = "Add " + user + " to the docker group";
        group.why   = "Quickest on a development VM (install_deps.sh does this for the user it sets up). "
                      "Members of the docker group can effectively act as root, so add trusted users only.";
        group.commands.push_back("sudo usermod -aG docker " + user);
        group.after = "Log out and back in (or reboot), then start the backend again.";
        fixes.push_back(group);

        fixes.push_back(sudoers);
    } else if (state == "sudo_denied") {
        fixes.push_back(sudoers);
    } else if (state == "daemon_down") {
        Fix start;
        start.title = "Start Docker";
        start.why   = "Docker is installed, but its service is stopped.";
        start.commands.push_back("sudo systemctl enable --now docker");
        start.after = "Then click Check Again.";
        fixes.push_back(start);
    } else if (state == "not_installed") {
        Fix install;
        install.title = "Install Docker";
        install.why   = "Newt runs in a Docker container.";
        install.commands.push_back("sudo apt-get update");
        install.commands.push_back("sudo apt-get install -y docker.io");
        install.commands.push_back("sudo systemctl enable --now docker");
        install.after = "Then click Check Again to see whether the backend can use it.";
        fixes.push_back(install);
    }
    return fixes;
}

std::vector<Instance> parse_containers(const std::string& docker_ps_output) {
    std::vector<Instance> found;
    std::istringstream in(docker_ps_output);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        size_t a = line.find('|');
        if (a == std::string::npos) continue;
        size_t b = line.find('|', a + 1);
        std::string name   = line.substr(0, a);
        std::string image  = line.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
        std::string status = b == std::string::npos ? "" : line.substr(b + 1);
        if (name == CONTAINER) continue;
        if (!contains(lower(image), "newt") && !contains(lower(name), "newt")) continue;
        Instance i;
        i.kind   = "container";
        i.name   = name;
        i.detail = image + (status.empty() ? "" : ", " + status);
        found.push_back(i);
    }
    return found;
}

std::string docker_command_for(const char* env_value) {
    if (env_value && std::string(env_value) == "sudo -n docker") return "sudo -n docker";
    return "docker";
}

// ── checks ────────────────────────────────────────────────────────────────

DockerAccess docker_check() { return docker_access_now(); }

std::vector<Instance> find_other_instances() {
    std::vector<Instance> found;

    // The sponsor's Pi runs Newt natively (installed with get-newt.sh).
    int code = 0;
    std::string svc = trim(run_capture("systemctl is-active newt.service", &code));
    if (svc == "active" || svc == "activating" || svc == "reloading") {
        Instance i;
        i.kind   = "service";
        i.name   = "newt.service";
        i.detail = "installed directly on this device (systemd service, " + svc + ")";
        found.push_back(i);
    }

    std::string ps = run_capture(docker() + " ps --format '{{.Names}}|{{.Image}}|{{.Status}}'", &code);
    if (code == 0) {
        std::vector<Instance> containers = parse_containers(ps);
        found.insert(found.end(), containers.begin(), containers.end());
    }
    return found;
}

// ── actions ───────────────────────────────────────────────────────────────

Result enable(const std::string& endpoint_in,
              const std::string& newt_id_in,
              const std::string& secret_in,
              bool force) {
    std::lock_guard<std::mutex> guard(op_mutex());
    Result r;
    std::string endpoint = trim(endpoint_in);
    std::string newt_id  = trim(newt_id_in);
    std::string secret   = trim(secret_in);

    if (secret.empty()) {
        auto saved = load_config();
        secret = saved["NEWT_SECRET"];
        if (secret.empty()) { r.error = "Newt secret is required."; return r; }
    }

    std::string err = validate(endpoint, newt_id, secret);
    if (!err.empty()) { r.error = err; return r; }

    // D-01: find out now if Docker can't be used, and say how to fix it.
    DockerAccess access = docker_access_now();
    if (access.state != "ok") {
        r.code   = "docker_access";
        r.error  = access.message;
        r.output = access.detail;
        return r;
    }

    // D-05: two Newts on one Site keep disconnecting each other.
    if (!force) {
        r.conflicts = find_other_instances();
        if (!r.conflicts.empty()) {
            r.code  = "conflict";
            r.error = "Another copy of Newt is already running on this device.";
            return r;
        }
    }

    err = save_config(endpoint, newt_id, secret);
    if (!err.empty()) { r.error = err; return r; }

    return start_container(image_to_run());
}

Result disable() {
    std::lock_guard<std::mutex> guard(op_mutex());
    Result r;
    int code = 0;
    r.output = run_capture(docker() + " rm -f " + CONTAINER, &code);
    if (code == 0 || contains(r.output, "No such container")) {
        r.success = true;
        r.output  = "Newt stopped.";
    } else {
        std::string state = classify_docker_output(r.output, code);
        r.error = access_message(state, current_user());
        if (state != "error") r.code = "docker_access";
    }
    return r;
}

Status status() {
    Status s;
    auto cfg     = load_config();
    s.endpoint   = cfg["PANGOLIN_ENDPOINT"];
    s.newt_id    = cfg["NEWT_ID"];
    s.has_secret = !cfg["NEWT_SECRET"].empty();

    auto st            = load_settings();
    // On unless turned off, but always reported off until Newt is set up so
    // the UI never shows it on with nothing to update.
    s.auto_update      = !s.newt_id.empty() && st["AUTO_UPDATE"] != "0";
    s.last_check       = std::atoll(st["LAST_CHECK"].c_str());
    s.last_result      = st["LAST_RESULT"];
    s.update_available = st["UPDATE_AVAILABLE"] == "1";

    int code = 0;
    std::string out = trim(run_capture(
        docker() + " inspect -f '{{.State.Status}}' " + CONTAINER, &code));

    if (code != 0) {
        if (contains(out, "No such object") || contains(out, "No such container")) {
            s.container = "absent";
            s.state     = s.newt_id.empty() ? "not_configured" : "stopped";
            s.rollback_available = !image_id(PREVIOUS_IMAGE).empty();
        } else {
            s.container = "unknown";
            s.state     = "docker_unavailable";
            s.detail    = access_message(classify_docker_output(out, code), current_user());
        }
        return s;
    }

    s.container          = out;
    s.version            = container_version();
    // Only offer Roll Back if the kept image differs from the running one
    std::string previous = image_id(PREVIOUS_IMAGE);
    s.rollback_available = !previous.empty() && previous != container_image_id();
    if (out != "running") {
        s.state  = (out == "restarting") ? "error" : "stopped";
        s.detail = "Container is " + out + ".";
        return s;
    }

    s.state = classify_logs(logs(200), &s.detail);
    return s;
}

std::string logs(int lines) {
    if (lines < 1)   lines = 1;
    if (lines > 500) lines = 500;
    int code = 0;
    return run_capture(docker() + " logs --tail " + std::to_string(lines) + " " + CONTAINER, &code);
}

// ── updates ───────────────────────────────────────────────────────────────

Result check_update() {
    std::lock_guard<std::mutex> guard(op_mutex());
    Result r;
    if (!is_configured()) { r.error = "Set up Newt first, then check for updates."; return r; }

    DockerAccess access = docker_access_now();
    if (access.state != "ok") { r.code = "docker_access"; r.error = access.message; return r; }

    std::string running = container_image_id();
    int code = 0;
    std::string pull = run_capture(docker() + " pull " + IMAGE, &code);
    if (code != 0) {
        r.error  = "Could not download the latest Newt image. Check this device's internet connection.";
        r.output = pull;
        record_check("Last check failed: could not download the latest image.", false);
        return r;
    }
    std::string latest = image_id(IMAGE);

    bool available = false;
    std::string msg;
    if (running.empty()) {
        msg = "Newt isn't running. It will start on the newest version next time you enable it.";
    } else if (update_needed(running, latest, "", false)) {
        available = true;
        msg = "A newer version of Newt is available. Click Update Now to install it.";
        if (latest == load_settings()["SKIP_IMAGE"])
            msg += " Automatic updates skip it because it was rolled back before.";
    } else {
        msg = "Newt is up to date.";
    }
    record_check(msg, available);
    r.success = true;
    r.output  = msg;
    return r;
}

Result update(bool automatic) {
    std::lock_guard<std::mutex> guard(op_mutex());
    Result r;
    if (!is_configured()) { r.error = "Set up Newt first."; return r; }

    DockerAccess access = docker_access_now();
    if (access.state != "ok") { r.code = "docker_access"; r.error = access.message; return r; }

    std::string running = container_image_id();
    if (running.empty()) {
        if (automatic) {
            record_check("Automatic update skipped: Newt isn't running.", false);
            r.success = true;
            r.code    = "up_to_date";
            r.output  = "Newt isn't running, so there is nothing to update.";
        } else {
            r.error = "Newt isn't running. Enable it first; it always starts on the newest version.";
        }
        return r;
    }

    std::string before      = tunnel_state();
    std::string old_version = container_version();

    int code = 0;
    std::string pull = run_capture(docker() + " pull " + IMAGE, &code);
    if (code != 0) {
        r.error  = "Could not download the latest Newt image. Check this device's internet connection.";
        r.output = pull;
        record_check("Last update failed: could not download the latest image.", false);
        return r;
    }
    std::string latest  = image_id(IMAGE);
    std::string skipped = load_settings()["SKIP_IMAGE"];

    if (!update_needed(running, latest, skipped, automatic)) {
        bool newer = !latest.empty() && latest != running;
        std::string msg = newer
            ? "A newer version is available, but it was rolled back before, so it was not installed automatically."
            : "Newt is up to date.";
        record_check(msg, newer);
        r.success = true;
        r.code    = "up_to_date";
        r.output  = msg;
        return r;
    }

    // Keep the running image so we can go back to it.
    std::string tag_out = run_capture(docker() + " tag " + shell_quote(running) + " " + PREVIOUS_IMAGE, &code);
    if (code != 0) {
        r.error  = "Could not keep a copy of the current version, so the update was cancelled.";
        r.output = tag_out;
        record_check(r.error, true);
        return r;
    }

    Result started = start_container(IMAGE);
    if (!started.success) {
        start_container(PREVIOUS_IMAGE);   // put the old version back
        r.error  = "The new version would not start, so Newt was restarted on the previous version. " + started.error;
        r.output = started.output;
        r.code   = "rolled_back";
        update_settings(std::map<std::string, std::string>{{"SKIP_IMAGE", latest}});
        record_check(r.error, true);
        return r;
    }

    if (before != "connected") {
        // Nothing to compare against: the tunnel was not up before the update.
        std::string msg = "Updated Newt to the latest version. The tunnel was not connected before the "
                          "update, so check its status.";
        update_settings(std::map<std::string, std::string>{{"SKIP_IMAGE", ""}});
        record_check(msg, false);
        r.success = true;
        r.output  = msg;
        return r;
    }

    int wait = update_wait_seconds();
    if (wait_for_tunnel(wait)) {
        std::string new_version = container_version();
        std::string msg = "Updated Newt" +
            (old_version.empty() || new_version.empty() ? std::string("")
                                                        : " from " + old_version + " to " + new_version) +
            ". The tunnel is up.";
        update_settings(std::map<std::string, std::string>{{"SKIP_IMAGE", ""}});
        record_check(msg, false);
        r.success = true;
        r.output  = msg;
        return r;
    }

    // The new version didn't reconnect: go back to the one that worked.
    Result restored = start_container(PREVIOUS_IMAGE);
    update_settings(std::map<std::string, std::string>{{"SKIP_IMAGE", latest}});
    r.code  = "rolled_back";
    r.error = restored.success
        ? "The new version did not connect within " + std::to_string(wait) +
          " seconds, so Newt was rolled back to the previous version" + version_text(old_version) +
          ". Automatic updates will skip that new version."
        : "The new version did not connect, and restarting the previous version also failed: " + restored.error;
    r.output = restored.output;
    record_check(r.error, true);
    return r;
}

Result rollback() {
    std::lock_guard<std::mutex> guard(op_mutex());
    Result r;
    if (!is_configured()) { r.error = "Set up Newt first."; return r; }

    DockerAccess access = docker_access_now();
    if (access.state != "ok") { r.code = "docker_access"; r.error = access.message; return r; }

    std::string previous = image_id(PREVIOUS_IMAGE);
    if (previous.empty()) { r.error = "There is no previous version to roll back to yet."; return r; }

    std::string running = container_image_id();
    if (running == previous) {
        r.success = true;
        r.output  = "Newt is already running the previous version.";
        return r;
    }

    Result started = start_container(PREVIOUS_IMAGE);
    if (!started.success) return started;

    // Don't let the daily updater reinstall the version we just left.
    std::string from = running.empty() ? image_id(IMAGE) : running;
    update_settings(std::map<std::string, std::string>{{"SKIP_IMAGE", from}});
    record_check("Rolled back to the previous version. Automatic updates will skip the newer one.", true);
    r.success = true;
    r.output  = "Rolled back to the previous version of Newt. Automatic updates will skip the version you left.";
    return r;
}

Result set_auto_update(bool enabled) {
    Result r;
    // There is nothing to update until Newt is set up, so only allow turning
    // it on after that. Turning it off is always allowed.
    if (enabled && !is_configured()) {
        r.error = "Set up Newt first, then turn on automatic updates.";
        r.code  = "not_configured";
        return r;
    }
    update_settings(std::map<std::string, std::string>{{"AUTO_UPDATE", enabled ? "1" : "0"}});
    r.success = true;
    r.output  = enabled ? "Automatic updates are on. Newt is checked once a day."
                        : "Automatic updates are off.";
    return r;
}

void start_auto_updater() {
    static std::once_flag once;
    std::call_once(once, [] {
        std::thread([] {
            // Let the backend finish starting before the first check.
            std::this_thread::sleep_for(std::chrono::minutes(2));
            for (;;) {
                auto st = load_settings();
                bool enabled = st["AUTO_UPDATE"] != "0";
                long long last = std::atoll(st["LAST_CHECK"].c_str());
                if (auto_update_due(enabled, is_configured(),
                                    static_cast<long long>(std::time(nullptr)), last)) {
                    std::cout << "[Newt] daily update check" << std::endl;
                    Result r = update(true);
                    std::cout << "[Newt] " << (r.success ? r.output : r.error) << std::endl;
                }
                std::this_thread::sleep_for(std::chrono::hours(1));
            }
        }).detach();
    });
}

} // namespace newt_manager
