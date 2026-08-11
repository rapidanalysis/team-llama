#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "system_ops.h"
#include <stdio.h>
#include <iostream>
#include <map>
#include <string>
#include <algorithm>
#include <cctype>

namespace system_ops {

    // ----------------------------------------------------------------
    // capture()
    // Runs a hardcoded shell command and returns its stdout as a string.
    // IMPORTANT: This function must only ever be called with hardcoded
    // string literals — never with user-supplied input.
    // ----------------------------------------------------------------
    static std::string capture(const char* shell_command) {
        FILE* pipe = popen(shell_command, "r");
        if (!pipe) return "[error] Failed to open process.";

        char buffer[256];
        std::string result;
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            result += buffer;
        }
        pclose(pipe);

        if (result.empty()) return "[no output]";
        return result;
    }

    // Trims trailing whitespace and newlines from a string (for card values)
    static std::string trim(std::string s) {
        s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char c) {
            return !std::isspace(c);
        }).base(), s.end());
        return s;
    }

    // ----------------------------------------------------------------
    // ALLOWLIST
    // Maps the command name sent from the frontend to a fixed,
    // hardcoded shell command string. The user can only trigger
    // keys in this map — they cannot influence the command string.
    // ----------------------------------------------------------------
    static const std::map<std::string, const char*> ALLOWLIST = {
        // System diagnostics
        { "disk_usage",      "df -h" },
        { "uptime",          "uptime" },
        { "network_info",    "ip addr show" },
        { "memory",          "free -h" },
        { "processes",       "ps aux --sort=-%cpu | head -15" },
        { "port_scan",       "ss -tlnp" },
        // SMB / Samba file sharing (sudo required for service control)
        { "samba_start",     "sudo systemctl start smbd 2>&1 && echo 'smbd started successfully.' || echo 'Failed to start smbd.'" },
        { "samba_stop",      "sudo systemctl stop smbd 2>&1 && echo 'smbd stopped.' || echo 'Failed to stop smbd.'" },
        { "samba_status",    "SYSTEMD_COLORS=0 systemctl status smbd --no-pager --full 2>&1" },
        // Tailscale tunnel
        // tailscale up blocks waiting for browser auth — cap at 10 s so the
        // request doesn't time out in the browser. The auth URL will appear
        // in the output if authentication is still needed.
        { "tailscale_up",    "timeout 10 sudo tailscale up 2>&1 && echo 'Tailscale is up.' || echo '[note] tailscale up timed out — open the URL above in a browser to authenticate, then try again'" },
        { "tailscale_down",  "sudo tailscale down 2>&1" },
        { "tailscale_status","tailscale status 2>&1" },
        // ngrok tunnel (auth token must be configured in the VM first via: ngrok config add-authtoken <token>)
        { "ngrok_start",     "pkill ngrok 2>/dev/null; nohup ngrok http 8080 > /tmp/ngrok.log 2>&1 & echo 'ngrok tunnel starting on port 8080. Use Check Status to verify.'" },
        { "ngrok_stop",      "pkill ngrok 2>/dev/null && echo 'ngrok stopped.' || echo 'ngrok was not running.'" },
        // Show process state AND last 10 lines of the ngrok log so we can see tunnel URLs / errors
        { "ngrok_status",
          "if pgrep -x ngrok > /dev/null; then "
            "echo 'ngrok is running.'; echo ''; "
            "echo 'Log output:'; "
            "tail -10 /tmp/ngrok.log 2>/dev/null || echo '(no log file yet)'; "
          "else "
            "echo 'ngrok is not running.'; echo ''; "
            "echo 'Last log:'; tail -5 /tmp/ngrok.log 2>/dev/null || echo '(no log)'; "
          "fi" },
        // Tailscale extra info
        { "tailscale_ip",      "tailscale ip 2>&1" },
        // ngrok — per-port start commands (hardcoded, not user-influenced)
        // nohup prevents SIGHUP from killing ngrok when the popen() shell exits
        { "ngrok_start_8080",  "pkill ngrok 2>/dev/null; nohup ngrok http 8080 > /tmp/ngrok.log 2>&1 & echo 'ngrok tunnel starting on port 8080. Use Check Status to verify.'" },
        { "ngrok_start_80",    "pkill ngrok 2>/dev/null; nohup ngrok http 80   > /tmp/ngrok.log 2>&1 & echo 'ngrok tunnel starting on port 80. Use Check Status to verify.'"   },
        { "ngrok_start_3000",  "pkill ngrok 2>/dev/null; nohup ngrok http 3000 > /tmp/ngrok.log 2>&1 & echo 'ngrok tunnel starting on port 3000. Use Check Status to verify.'" },
        { "ngrok_start_5000",  "pkill ngrok 2>/dev/null; nohup ngrok http 5000 > /tmp/ngrok.log 2>&1 & echo 'ngrok tunnel starting on port 5000. Use Check Status to verify.'" },
        // ngrok URL — try local API first; also output any ngrok URL found in the log as a fallback.
        // The JS side will accept either a JSON "public_url" value or a raw https://...ngrok... URL.
        { "ngrok_url",
          "curl -s http://localhost:4040/api/tunnels 2>/dev/null; "
          "grep -oE 'https://[A-Za-z0-9_-]+\\.ngrok[A-Za-z0-9._-]*' /tmp/ngrok.log 2>/dev/null | tail -1" },
        // Power controls (use systemctl for consistent PATH via popen)
        { "reboot",            "sudo systemctl reboot 2>&1" },
        { "shutdown",          "sudo systemctl poweroff 2>&1" }
    };

    // ----------------------------------------------------------------
    // initialize()
    // Sets up the CommandWrapper singleton with the default command
    // catalogue. Call once at startup from main.cpp.
    // ----------------------------------------------------------------
    void initialize() {
        std::cout << "[system_ops] Initializing CommandWrapper...\n";
        CommandWrapper::get_instance().initialize_defaults();
        std::cout << "[system_ops] CommandWrapper initialized with default commands.\n";
    }

    // ----------------------------------------------------------------
    // execute_command()
    // Executes a command via the CommandWrapper with role-based access
    // control and timeout support. Returns a full CommandResult.
    // ----------------------------------------------------------------
    CommandResult execute_command(const std::string& command_name,
                                  const std::string& params,
                                  user_role::Role role) {
        auto& wrapper = CommandWrapper::get_instance();

        // Lazy initialisation — ensure defaults are loaded
        if (!wrapper.is_allowed("disk_usage")) {
            wrapper.initialize_defaults();
        }

        return wrapper.execute_with_timeout(command_name, params, role, 30000);
    }

    // ----------------------------------------------------------------
    // tailscale_up()
    // Runs `tailscale up` with an optional pre-validated auth key.
    // The authkey is validated by the router before this is called
    // (only alphanumeric, hyphens, underscores allowed).
    // ----------------------------------------------------------------
    std::string tailscale_up(const std::string& authkey) {
        std::string cmd;
        if (authkey.empty()) {
            cmd = "timeout 10 sudo tailscale up 2>&1 || echo '[note] tailscale up timed out — open the auth URL in a browser if prompted'";
        } else {
            cmd = "timeout 10 sudo tailscale up --authkey=" + authkey + " 2>&1 || echo '[note] tailscale up failed or timed out'";
        }
        return capture(cmd.c_str());
    }

    std::string run_command(const std::string& command_name) {
        auto it = ALLOWLIST.find(command_name);
        if (it == ALLOWLIST.end()) {
            return "[error] Unknown or disallowed command: " + command_name;
        }
        // Only the hardcoded string from the allowlist reaches the shell
        return capture(it->second);
    }

    // ----------------------------------------------------------------
    // Dashboard card helpers
    // Each calls a specific, hardcoded command and trims the output
    // to a short value suitable for displaying in a card.
    // ----------------------------------------------------------------

    std::string get_hostname() {
        return trim(capture("hostname"));
    }

    std::string get_uptime() {
        // uptime -p gives a clean human-readable string e.g. "up 2 hours, 5 minutes"
        return trim(capture("uptime -p"));
    }

    std::string get_disk_usage() {
        // Returns just the usage percentage of the root partition e.g. "42%"
        return trim(capture("df -h / | tail -1 | awk '{print $3 \"/\" $2}'"));
    }

    std::string get_ip_address() {
        // Returns the first assigned IP address
        return trim(capture("hostname -I | awk '{print $1}'"));
    }

    // ----------------------------------------------------------------
    // set_hostname()
    // Validates the proposed hostname strictly against RFC 1123 rules
    // before allowing it anywhere near the shell.
    // Only letters, numbers, and hyphens are permitted.
    // ----------------------------------------------------------------
    std::string set_hostname(const std::string& name) {
        if (name.empty() || name.length() > 63)
            return "[error] Hostname must be between 1 and 63 characters.";

        for (unsigned char c : name) {
            if (!std::isalnum(c) && c != '-')
                return "[error] Hostname may only contain letters, numbers, and hyphens.";
        }

        if (name.front() == '-' || name.back() == '-')
            return "[error] Hostname cannot start or end with a hyphen.";

        // Input is fully validated — safe to build the command.
        // We also update /etc/hosts so that sudo (and other tools) can resolve
        // the new hostname without "unable to resolve host" warnings.
        // The sed pattern replaces the 127.0.1.1 line which maps the hostname.
        std::string cmd =
            "{ sudo hostnamectl set-hostname " + name +
            " && sudo sed -i 's/^127\\.0\\.1\\.1[[:space:]]\\+.*/127.0.1.1\\t" + name + "/' /etc/hosts; } 2>&1";
        return capture(cmd.c_str());
    }

    // ----------------------------------------------------------------
    // get_service_status()
    // Returns the systemd active state of a known service.
    // Uses a hardcoded internal map — the caller cannot influence
    // which shell command runs.
    // ----------------------------------------------------------------
    std::string get_service_status(const std::string& service_name) {
        static const std::map<std::string, const char*> SERVICE_COMMANDS = {
            { "smbd",       "systemctl is-active smbd 2>/dev/null" },
            { "tailscaled", "tailscale status 2>/dev/null | grep -qE '^[0-9]' && echo active || echo inactive" },
            { "ngrok",      "pgrep -x ngrok > /dev/null && echo active || echo inactive" }
        };

        auto it = SERVICE_COMMANDS.find(service_name);
        if (it == SERVICE_COMMANDS.end()) return "unknown";
        return trim(capture(it->second));
    }

}
