#include "docker_manager.h"
#include <cctype>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <sys/wait.h>

// ── Internal helpers ─────────────────────────────────────────────────────────

namespace {

// Shell operators that are dangerous when a string lands inside a popen command.
// Single quotes are excluded here because we wrap validated strings in shell_quote().
static const char* SHELL_METACHARS = ";&|>$`\\!#*?()[]{}~\"\n\r";

bool has_metachar(const std::string& s) {
    for (char c : s) {
        if (std::strchr(SHELL_METACHARS, c) != nullptr) return true;
    }
    return false;
}

bool has_traversal(const std::string& s) {
    return s.find("..") != std::string::npos;
}

// Wraps a string in single quotes, escaping any internal single quotes.
// Safe to use for arguments that have already passed validation.
std::string shell_quote(const std::string& s) {
    std::string r = "'";
    for (char c : s) {
        if (c == '\'') r += "'\\''";
        else           r += c;
    }
    r += '\'';
    return r;
}

bool is_valid_port_number(const std::string& s) {
    if (s.empty() || s.size() > 5) return false;
    for (char c : s) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    int p = std::stoi(s);
    return p >= 1 && p <= 65535;
}

} // namespace

// ── docker_manager implementation ────────────────────────────────────────────

namespace docker_manager {

std::string validate(const DockerRunConfig& config) {

    // ── image (required) ────────────────────────────────────────────────────
    if (config.image.empty())
        return "Image name is required.";
    if (config.image.size() > 256)
        return "Image name is too long (max 256 characters).";
    if (!std::isalnum(static_cast<unsigned char>(config.image[0])))
        return "Image name must start with a letter or digit.";
    for (char c : config.image) {
        if (!std::isalnum(static_cast<unsigned char>(c)) &&
            c != '.' && c != '-' && c != '_' &&
            c != '/' && c != ':' && c != '@')
            return std::string("Image name contains invalid character '") + c + "'.";
    }

    // ── container name (optional) ────────────────────────────────────────────
    if (!config.name.empty()) {
        if (config.name.size() > 128)
            return "Container name is too long (max 128 characters).";
        if (!std::isalnum(static_cast<unsigned char>(config.name[0])) && config.name[0] != '_')
            return "Container name must start with a letter, digit, or underscore.";
        for (char c : config.name) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != '.')
                return std::string("Container name contains invalid character '") + c + "'.";
        }
    }

    // ── port mappings ────────────────────────────────────────────────────────
    // Accepted formats:
    //   host_port:container_port
    //   host_ip:host_port:container_port
    //   any of the above with /tcp or /udp appended
    for (const auto& p : config.ports) {
        std::string port_str = p;

        // Strip optional protocol suffix
        auto slash = port_str.rfind('/');
        if (slash != std::string::npos) {
            std::string proto = port_str.substr(slash + 1);
            if (proto != "tcp" && proto != "udp")
                return "Port '" + p + "': protocol must be tcp or udp.";
            port_str = port_str.substr(0, slash);
        }

        // Split into parts by ':'
        std::vector<std::string> parts;
        std::string cur;
        for (char c : port_str) {
            if (c == ':') { parts.push_back(cur); cur.clear(); }
            else          { cur += c; }
        }
        parts.push_back(cur);

        if (parts.size() < 2 || parts.size() > 3)
            return "Port '" + p + "' must be in host:container or ip:host:container format.";

        // Validate the IP portion if present
        if (parts.size() == 3) {
            for (char c : parts[0]) {
                if (!std::isdigit(static_cast<unsigned char>(c)) && c != '.')
                    return "Port '" + p + "' has an invalid host IP address.";
            }
        }

        std::string host_port      = (parts.size() == 3) ? parts[1] : parts[0];
        std::string container_port = parts.back();

        if (!is_valid_port_number(host_port))
            return "Port '" + p + "': host port must be a number between 1 and 65535.";
        if (!is_valid_port_number(container_port))
            return "Port '" + p + "': container port must be a number between 1 and 65535.";
    }

    // ── volume mounts ────────────────────────────────────────────────────────
    // Accepted format: /host/path:/container/path[:ro|:rw]
    for (const auto& v : config.volumes) {
        auto colon = v.find(':');
        if (colon == std::string::npos || colon == 0)
            return "Volume '" + v + "' must be in /host/path:/container/path format.";

        std::string host_path = v.substr(0, colon);
        std::string rest      = v.substr(colon + 1);

        // Strip optional :ro / :rw suffix
        std::string container_path;
        auto opt_colon = rest.rfind(':');
        if (opt_colon != std::string::npos) {
            std::string opts = rest.substr(opt_colon + 1);
            if (opts != "ro" && opts != "rw")
                return "Volume '" + v + "': mount option must be ro or rw.";
            container_path = rest.substr(0, opt_colon);
        } else {
            container_path = rest;
        }

        if (host_path.empty() || host_path[0] != '/')
            return "Volume host path '" + host_path + "' must be an absolute path (starts with /).";
        if (container_path.empty() || container_path[0] != '/')
            return "Volume container path '" + container_path + "' must be an absolute path (starts with /).";
        if (has_traversal(host_path))
            return "Volume host path '" + host_path + "' contains path traversal (../).";
        if (has_traversal(container_path))
            return "Volume container path '" + container_path + "' contains path traversal (../).";
        if (has_metachar(host_path))
            return "Volume host path '" + host_path + "' contains unsafe characters.";
        if (has_metachar(container_path))
            return "Volume container path '" + container_path + "' contains unsafe characters.";
    }

    // ── environment variables ────────────────────────────────────────────────
    // Format: KEY=value  (key: [A-Za-z_][A-Za-z0-9_]*, value: no shell metacharacters)
    for (const auto& e : config.env) {
        auto eq = e.find('=');
        if (eq == std::string::npos)
            return "Env var '" + e + "' must be in KEY=value format.";

        std::string key = e.substr(0, eq);
        std::string val = e.substr(eq + 1);

        if (key.empty())
            return "Env var has an empty key.";
        if (!std::isalpha(static_cast<unsigned char>(key[0])) && key[0] != '_')
            return "Env var key '" + key + "' must start with a letter or underscore.";
        for (char c : key) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
                return "Env var key '" + key + "' contains invalid character '" + c + "'.";
        }
        if (has_metachar(val))
            return "Env var value for '" + key + "' contains unsafe characters.";
    }

    // ── restart policy (optional) ────────────────────────────────────────────
    if (!config.restart.empty()) {
        static const char* valid[] = { "no", "always", "unless-stopped", "on-failure", nullptr };
        bool ok = false;
        for (int i = 0; valid[i]; ++i) if (config.restart == valid[i]) { ok = true; break; }
        if (!ok)
            return "Restart policy '" + config.restart + "' is invalid. Use: no, always, unless-stopped, on-failure.";
    }

    return ""; // all good
}

std::string build_command(const DockerRunConfig& config) {
    std::string cmd = "docker run";

    if (config.detach)
        cmd += " -d";

    if (!config.name.empty())
        cmd += " --name " + shell_quote(config.name);

    for (const auto& p : config.ports)
        cmd += " -p " + p;              // ports are digit-only — safe without quoting

    for (const auto& v : config.volumes)
        cmd += " -v " + shell_quote(v); // paths may contain spaces

    for (const auto& e : config.env)
        cmd += " -e " + shell_quote(e); // values may contain spaces or equals signs

    if (!config.restart.empty())
        cmd += " --restart " + config.restart; // enum-validated — safe without quoting

    cmd += " " + config.image; // character-allowlist validated — safe without quoting

    return cmd;
}

DockerRunResult run(const DockerRunConfig& config) {
    DockerRunResult result;

    std::string err = validate(config);
    if (!err.empty()) {
        result.error = err;
        return result;
    }

    // Merge stderr into stdout so the caller sees pull progress and error messages.
    std::string cmd = build_command(config) + " 2>&1";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        result.error = "Failed to start docker process.";
        return result;
    }

    char buffer[4096];
    std::string output;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
    int wait_status = pclose(pipe);
    int exit_code   = WIFEXITED(wait_status) ? WEXITSTATUS(wait_status) : -1;

    result.output  = output.empty() ? "[no output]" : output;
    result.success = (exit_code == 0);
    if (!result.success)
        result.error = "docker run exited with code " + std::to_string(exit_code);

    return result;
}

} // namespace docker_manager
