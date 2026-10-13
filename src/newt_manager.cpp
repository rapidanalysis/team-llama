#include "newt_manager.h"
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

// Our own name, so we never remove a newt container someone else started on the device.
const std::string kContainer = "xerxes-newt";
// Pinned so an upstream release cannot change behaviour without a code change.
const std::string kImage = "fosrl/newt:1.18.1";
// Outside public/, so the web server never serves it.
const std::string kConfigDir = "config";
const std::string kEnvFile = "config/newt.env";

std::mutex& lock() {
    static std::mutex m;
    return m;
}

struct Capture {
    int exit_code;
    std::string output;
};

// Only ever called with commands built in this file from fixed strings.
Capture run(const std::string& cmd) {
    Capture c{-1, ""};
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return c;
    char buffer[512];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) c.output += buffer;
    int st = pclose(pipe);
    c.exit_code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    return c;
}

std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

// Letters, digits, hyphen and underscore. Pangolin IDs and secrets fit this.
bool is_token(const std::string& s) {
    if (s.empty() || s.size() > 128) return false;
    for (char c : s) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') return false;
    }
    return true;
}

// https://host[:port][/] and nothing else.
bool is_endpoint(const std::string& s) {
    const std::string scheme = "https://";
    if (s.size() > 255 || s.rfind(scheme, 0) != 0) return false;
    std::string rest = s.substr(scheme.size());
    if (!rest.empty() && rest.back() == '/') rest.pop_back();

    std::string host = rest;
    std::string port;
    auto colon = rest.find(':');
    if (colon != std::string::npos) {
        host = rest.substr(0, colon);
        port = rest.substr(colon + 1);
        if (port.empty() || port.size() > 5) return false;
        for (char c : port) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        int p = std::stoi(port);
        if (p < 1 || p > 65535) return false;
    }
    if (host.empty() || host.front() == '.' || host.front() == '-') return false;
    for (char c : host) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-') return false;
    }
    return true;
}

// Writes the env file readable by its owner only (0600).
bool write_env_file(const std::string& contents) {
    std::error_code ec;
    fs::create_directories(kConfigDir, ec);
    if (ec) return false;
    chmod(kConfigDir.c_str(), 0700);

    int fd = open(kEnvFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return false;
    fchmod(fd, 0600);  // in case the file already existed with wider permissions
    ssize_t n = write(fd, contents.data(), contents.size());
    close(fd);
    return n == static_cast<ssize_t>(contents.size());
}

// Reads one KEY=value line from the env file. Used for endpoint and ID only.
std::string read_env_value(const std::string& key) {
    std::ifstream in(kEnvFile);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind(key + "=", 0) == 0) return line.substr(key.size() + 1);
    }
    return "";
}

} // namespace

namespace newt_manager {

std::string validate(const Settings& s) {
    if (!is_endpoint(s.endpoint))
        return "Endpoint must look like https://pangolin.example.com (an optional :port is allowed).";
    if (!is_token(s.id))
        return "Site ID must be 1 to 128 letters, numbers, hyphens or underscores.";
    if (!is_token(s.secret))
        return "Site secret must be 1 to 128 letters, numbers, hyphens or underscores.";
    return "";
}

std::string env_file_contents(const Settings& s) {
    std::ostringstream out;
    out << "PANGOLIN_ENDPOINT=" << s.endpoint << "\n"
        << "NEWT_ID=" << s.id << "\n"
        << "NEWT_SECRET=" << s.secret << "\n";
    return out.str();
}

std::string build_run_command(const std::string& env_file) {
    // Default user-space networking: no --privileged, no NET_ADMIN, no /dev/net/tun.
    return "docker run -d --name " + kContainer +
           " --restart unless-stopped --env-file " + env_file +
           " " + kImage;
}

ActionResult up(const Settings& s) {
    std::lock_guard<std::mutex> guard(lock());

    std::string err = validate(s);
    if (!err.empty()) return {false, "", err};

    if (!write_env_file(env_file_contents(s)))
        return {false, "", "Could not write " + kEnvFile};

    // Replace any previous container so new settings take effect.
    run("docker rm -f " + kContainer + " >/dev/null 2>&1");

    Capture c = run(build_run_command(kEnvFile) + " 2>&1");
    if (c.exit_code != 0) {
        std::remove(kEnvFile.c_str());  // do not keep a secret for a tunnel that is not running
        return {false, trim(c.output), "docker run failed. Is Docker installed and running?"};
    }
    return {true, trim(c.output), ""};
}

ActionResult down() {
    std::lock_guard<std::mutex> guard(lock());

    Capture c = run("docker rm -f " + kContainer + " 2>&1");
    std::remove(kEnvFile.c_str());  // disabling also forgets the stored secret
    if (c.exit_code != 0)
        return {true, "Newt was not running. Stored settings removed.", ""};
    return {true, "Newt stopped. Stored settings removed.", ""};
}

StatusResult status() {
    std::lock_guard<std::mutex> guard(lock());

    StatusResult r;
    Capture c = run("docker inspect -f '{{.State.Status}}' " + kContainer + " 2>/dev/null");
    r.state = (c.exit_code == 0 && !trim(c.output).empty()) ? trim(c.output) : "not_installed";
    r.configured = fs::exists(kEnvFile);
    r.endpoint = read_env_value("PANGOLIN_ENDPOINT");
    r.id = read_env_value("NEWT_ID");
    return r;
}

}
