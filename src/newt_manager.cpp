#include "newt_manager.h"

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <map>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace newt_manager {

namespace {

const char* CONTAINER = "xerxes-newt";
const char* IMAGE     = "fosrl/newt:latest";

// ── helpers ───────────────────────────────────────────────────────────────

bool contains(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
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

// Config lives outside the web root and the repo so the secret can't be
// served or committed by accident.
std::string config_dir() {
    const char* custom = std::getenv("XERXES_CONFIG_DIR");
    if (custom && *custom) return custom;
    const char* home = std::getenv("HOME");
    return std::string(home && *home ? home : "/tmp") + "/.xerxes";
}

std::string config_path() { return config_dir() + "/newt.env"; }

std::map<std::string, std::string> load_config() {
    std::map<std::string, std::string> cfg;
    FILE* f = std::fopen(config_path().c_str(), "r");
    if (!f) return cfg;
    char line[1024];
    while (std::fgets(line, sizeof(line), f)) {
        std::string l = trim(line);
        size_t eq = l.find('=');
        if (eq == std::string::npos || l.empty() || l[0] == '#') continue;
        cfg[l.substr(0, eq)] = l.substr(eq + 1);
    }
    std::fclose(f);
    return cfg;
}

// Writes the env file with mode 0600. Returns "" on success.
std::string save_config(const std::string& endpoint,
                        const std::string& newt_id,
                        const std::string& secret) {
    std::string dir = config_dir();
    if (mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST)
        return "Could not create config directory " + dir + ".";

    std::string path = config_path();
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return "Could not write " + path + ".";
    fchmod(fd, 0600);  // tighten permissions if the file already existed

    std::string body = "PANGOLIN_ENDPOINT=" + endpoint + "\n"
                       "NEWT_ID="           + newt_id  + "\n"
                       "NEWT_SECRET="       + secret   + "\n";
    ssize_t n = write(fd, body.data(), body.size());
    close(fd);
    if (n != static_cast<ssize_t>(body.size())) return "Could not write " + path + ".";
    return "";
}

std::string docker_error_message(const std::string& output, int code) {
    if (contains(output, "permission denied"))
        return "Docker permission denied: the backend user cannot use Docker. "
               "Run the backend as root or add its user to the docker group.";
    if (contains(output, "Cannot connect to the Docker daemon") ||
        contains(output, "daemon is running"))
        return "Docker is not running on this device.";
    if (contains(output, "command not found") || contains(output, "not found: docker"))
        return "Docker is not installed on this device.";
    return "Docker command failed (exit code " + std::to_string(code) + ").";
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

// ── log classification ────────────────────────────────────────────────────

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

// ── actions ───────────────────────────────────────────────────────────────

Result enable(const std::string& endpoint_in,
              const std::string& newt_id_in,
              const std::string& secret_in) {
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

    err = save_config(endpoint, newt_id, secret);
    if (!err.empty()) { r.error = err; return r; }

    // Replace any previous instance so there is only ever one Newt per device.
    int code = 0;
    run_capture(std::string("docker rm -f ") + CONTAINER, &code);

    std::string cmd = std::string("docker run -d --name ") + CONTAINER +
                      " --restart unless-stopped --env-file " + shell_quote(config_path()) +
                      " " + IMAGE;
    r.output  = run_capture(cmd, &code);
    r.success = (code == 0);
    if (!r.success) r.error = docker_error_message(r.output, code);
    return r;
}

Result disable() {
    Result r;
    int code = 0;
    r.output = run_capture(std::string("docker rm -f ") + CONTAINER, &code);
    if (code == 0 || contains(r.output, "No such container")) {
        r.success = true;
        r.output  = "Newt stopped.";
    } else {
        r.error = docker_error_message(r.output, code);
    }
    return r;
}

Status status() {
    Status s;
    auto cfg     = load_config();
    s.endpoint   = cfg["PANGOLIN_ENDPOINT"];
    s.newt_id    = cfg["NEWT_ID"];
    s.has_secret = !cfg["NEWT_SECRET"].empty();

    int code = 0;
    std::string out = trim(run_capture(
        std::string("docker inspect -f '{{.State.Status}}' ") + CONTAINER, &code));

    if (code != 0) {
        if (contains(out, "No such object") || contains(out, "No such container")) {
            s.container = "absent";
            s.state     = s.newt_id.empty() ? "not_configured" : "stopped";
        } else {
            s.container = "unknown";
            s.state     = "docker_unavailable";
            s.detail    = docker_error_message(out, code);
        }
        return s;
    }

    s.container = out;
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
    return run_capture(std::string("docker logs --tail ") + std::to_string(lines) + " " + CONTAINER, &code);
}

} // namespace newt_manager
