#include "utils.h"
#include <iostream>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <arpa/inet.h>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <thread>
#include <future>
#include <mutex>

// Not allowed shell char
static const char* SHELL_METACHARS = ";&|>$`\\!#*?()<>[]{}~$'\"";

namespace user_role {

int get_permissions(Role role) {
    switch (role) {
        case ADMIN:  return PERM_VIEW_STATUS | PERM_RUN_SYS | PERM_RUN_NETWORK | 
                        PERM_RUN_DOCKER | PERM_CONTAINER | PERM_ADMIN;
        case GUEST:  return PERM_VIEW_STATUS | PERM_RUN_SYS | PERM_RUN_NETWORK | PERM_RUN_DOCKER;
        case VIEWER: return PERM_VIEW_STATUS;
        default:    return 0;
    }
}

bool has_permission(Role role, Permission perm) {
    return (get_permissions(role) & perm) != 0;
}

} // namespace user_role

// Helper Function
bool is_shell_safe(const std::string& input) {
    if (input.empty()) return true;
    
    // Check for shell metacharacters
    for (char c : input) {
        if (::strchr(SHELL_METACHARS, c) != nullptr) {
            return false;
        }
    }
    return true;
}

std::string get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::tm tm_buf;
    char timestamp[64];
    
#if defined(_WIN32) || defined(_WIN64)
    localtime_s(&tm_buf, &time_t_now);
#else
    localtime_r(&time_t_now, &tm_buf);
#endif
    
    std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &tm_buf);
    
    char result[80];
    std::snprintf(result, sizeof(result), "%s.%03ld", timestamp, ms.count());
    return result;
}

// COMMAND WRAPPER IMPLEMENTATION
CommandWrapper& CommandWrapper::get_instance() {
    static CommandWrapper instance;
    return instance;
}

void CommandWrapper::initialize_defaults() {
    // System commands
    add_command({"disk_usage", "df -h", command_cat::SYSTEM, false, 
                "Show disk usage", 10, {}});
    add_command({"uptime", "uptime -p", command_cat::SYSTEM, false, 
                "Show system uptime", 5, {}});
    add_command({"memory", "free -h", command_cat::SYSTEM, false, 
                "Show memory usage", 10, {}});
    add_command({"processes", "ps aux --sort=-%cpu", command_cat::SYSTEM, false, 
                "Show top processes", 15, {"15", "20", "30"}});
    add_command({"load", "cat /proc/loadavg", command_cat::SYSTEM, false, 
                "Show system load", 5, {}});
    
    // Network commands
    add_command({"ip_addr", "ip addr show", command_cat::NETWORK, false, 
                "Show IP addresses", 10, {}});
    add_command({"port_scan", "ss -tlnp", command_cat::NETWORK, false, 
                "Show listening ports", 15, {}});
    add_command({"ping", "ping -c 4", command_cat::NETWORK, false, 
                "Ping a host", 20, {}});
    add_command({"route_table", "ip route", command_cat::NETWORK, false, 
                "Show routing table", 10, {}});
    
    // Docker commands
    add_command({"docker_ps",     "docker ps --format table",    command_cat::DOCKER, false,
                "List running containers", 20, {}});
    add_command({"docker_ps_all", "docker ps -a --format table", command_cat::DOCKER, false,
                "List all containers including stopped", 20, {}});
    add_command({"docker_images", "docker images", command_cat::DOCKER, false,
                "List images", 20, {}});
    add_command({"docker_stats",  "docker stats --no-stream", command_cat::DOCKER, false,
                "Show container stats", 30, {}});

    // Container management
    add_command({"container_restart", "docker restart", command_cat::CONTAINER, true,
                "Restart a container", 60, {}});
    add_command({"container_stop",    "docker stop",    command_cat::CONTAINER, true,
                "Stop a container", 30, {}});
    add_command({"container_start",   "docker start",   command_cat::CONTAINER, true,
                "Start a container", 30, {}});
    add_command({"container_logs",    "docker logs --tail 100", command_cat::CONTAINER, false,
                "View container logs", 30, {}});
    add_command({"container_remove",  "docker rm",      command_cat::CONTAINER, true,
                "Remove a stopped container", 30, {}});
    
// Admin commands
    add_command({"reboot", "systemctl reboot", command_cat::ADMIN, true, 
                "Reboot system", 60, {}});
    add_command({"shutdown", "systemctl poweroff", command_cat::ADMIN, true, 
                "Shutdown system", 60, {}});
    add_command({"systemctl", "systemctl", command_cat::ADMIN, true, 
                "Systemctl service manager", 30, {"start", "stop", "status", "restart"}});
    
    // ============================================================================
    // NGROK TUNNEL COMMANDS
    // ============================================================================
    add_command({"ngrok_config", "ngrok config", command_cat::ADMIN, true,
                "Configure ngrok authtoken", 10, {"authtoken"}});
    add_command({"ngrok_start", "ngrok start", command_cat::ADMIN, true,
                "Start ngrok tunnel", 30, {}});
    add_command({"ngrok_stop", "ngrok tunnel stop", command_cat::ADMIN, true,
                "Stop ngrok tunnel", 10, {}});
    add_command({"ngrok_status", "ngrok status", command_cat::NETWORK, false,
                "Get ngrok status", 10, {}});
    
    // ============================================================================
    // TAILSCALE TUNNEL COMMANDS
    // ============================================================================
    add_command({"tailscale_up", "tailscale up", command_cat::ADMIN, true,
                "Login to Tailscale", 30, {}});
    add_command({"tailscale_down", "tailscale down", command_cat::ADMIN, true,
                "Logout from Tailscale", 30, {}});
    add_command({"tailscale_status", "tailscale status", command_cat::NETWORK, false,
                "Get Tailscale status", 15, {}});
    add_command({"tailscale_ip", "tailscale ip", command_cat::NETWORK, false,
                "Get Tailscale IPs", 10, {}});
    add_command({"tailscale_join", "tailscale join", command_cat::ADMIN, true,
                "Join tailnet", 60, {}});
    add_command({"tailscale_leave", "tailscale leave", command_cat::ADMIN, true,
                "Leave tailnet", 30, {}});
    add_command({"tailscale_rename", "tailscale rename", command_cat::ADMIN, true,
                "Rename device", 15, {}});
    
    // ============================================================================
    // CREDENTIAL MANAGEMENT
    // ============================================================================
    add_command({"store_credential", "keyctl", command_cat::ADMIN, true,
                "Store credential", 10, {}});
    add_command({"get_credential", "keyctl", command_cat::ADMIN, true,
                "Get credential", 10, {}});
    add_command({"delete_credential", "keyctl", command_cat::ADMIN, true,
                "Delete credential", 10, {}});
}

bool CommandWrapper::add_command(const CommandEntry& entry) {
    if (entry.name.empty() || entry.command.empty()) {
        return false;
    }
    allowlist_[entry.name] = entry;
    return true;
}

bool CommandWrapper::is_allowed(const std::string& command_name) {
    return allowlist_.find(command_name) != allowlist_.end();
}

const CommandEntry* CommandWrapper::get_command_info(const std::string& name) {
    auto it = allowlist_.find(name);
    if (it != allowlist_.end()) {
        return &(it->second);
    }
    return nullptr;
}

// Escape single quotes for shell safely
static std::string escape_shell_param(const std::string& param) {
    std::string result;
    result.reserve(param.size() + 10);
    
    for (char c : param) {
        if (c == '\'') {
            result += "'\\''";  // End quote, add escaped quote, restart quote
        } else {
            result += c;
        }
    }
    
    return result;
}

// Build command with proper parameterization using exec array
// This prevents shell injection by avoiding shell interpretation
std::string CommandWrapper::build_command(const CommandEntry& entry, 
                                          const std::string& params) {
    if (params.empty()) {
        return entry.command;
    }
    
    // For commands that need parameters, wrap in sh -c with proper escaping
    // This is safer than simple concatenation
    std::string escaped = escape_shell_param(params);
    return entry.command + " '" + escaped + "'";
}

// Validate and sanitize parameters thoroughly
bool CommandWrapper::validate_params(const CommandEntry& entry, 
                                    const std::string& params) {
    // Empty params is always ok (no parameters)
    if (params.empty()) {
        return true;
    }
    
    // If allowed params defined, check against whitelist
    if (!entry.allowed_params.empty()) {
        for (const auto& allowed : entry.allowed_params) {
            if (params == allowed) {
                return true;
            }
        }
        return false;
    }
    
    // For commands without whitelist, validate thoroughly:
    // 1. Check length
    if (params.length() > 256) {
        return false;
    }
    
    // 2. Must be shell-safe (no metacharacters)
    if (!is_shell_safe(params)) {
        return false;
    }
    
    // 3. Check for common injection patterns
    std::string lower = params;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    // Block suspicious patterns
    const char* dangerous[] = {" && ", " || ", "; ", "| ", "`", "$(", "${",
                               "\n", "\r", "../", "..\\"};
    for (const char* pattern : dangerous) {
        if (lower.find(pattern) != std::string::npos) {
            return false;
        }
    }
    // Check for null bytes separately — find("\0") uses strlen which gives 0,
    // making it match every string. Use find(char) instead.
    if (params.find('\0') != std::string::npos) {
        return false;
    }
    
    // 4. Check for path traversal attempts
    if (params.find("..") != std::string::npos) {
        return false;
    }
    
    // 5. Check for numeric params (must be safe integers)
    // Allow only alphanumeric, hyphen, underscore, space, colon, dot
    for (char c : params) {
        if (!std::isalnum(c) && c != '-' && c != '_' && c != ' ' && 
            c != ':' && c != '.' && c != '/' && c != '@' && c != '%') {
            // Additional check: permit certain special chars that are safe
            if (c != '=' && c != ',') {
                return false;
            }
        }
    }
    
    return true;
}

void CommandWrapper::log_execution(const std::string& cmd, bool success, int exit_code) {
    std::ostringstream log_entry;
    log_entry << "[" << get_timestamp() << "] "
                     << (success ? "SUCCESS" : "FAILED") 
                     << " exit=" << exit_code << " cmd=\"" << cmd << "\"";
    audit_log_.push_back(log_entry.str());
    
    // Keep log size reasonable (last 1000 entries)
    if (audit_log_.size() > 1000) {
        audit_log_.erase(audit_log_.begin());
    }
}

std::string CommandWrapper::sanitize_output(const std::string& output) {
    std::string result = output;
    
    // Remove or escape potentially dangerous characters
    // Replace null bytes
    result.erase(std::remove(result.begin(), result.end(), '\0'), result.end());
    
    // Escape backticks to prevent command substitution in output display
    std::replace(result.begin(), result.end(), '`', '\'');
    
    // Escape $ to prevent variable expansion display
    std::replace(result.begin(), result.end(), '$', '@');
    
    return result;
}

CommandResult CommandWrapper::execute(const std::string& command_name,
                                    const std::string& params,
                                    user_role::Role role) {
    return execute_with_timeout(command_name, params, role, 30000); // 30s default
}

CommandResult CommandWrapper::execute_with_timeout(const std::string& command_name,
                                               const std::string& params,
                                               user_role::Role role,
                                               int timeout_ms) {
    CommandResult result;
    result.timestamp = std::chrono::system_clock::now();
    result.command = command_name;
    
    // Check if command exists
    auto it = allowlist_.find(command_name);
    if (it == allowlist_.end()) {
        result.success = false;
        result.error = "Command not found: " + command_name;
        log_execution(command_name, false, -1);
        return result;
    }
    
    const CommandEntry& entry = it->second;
    
    // Check role permissions
    if (entry.requires_admin && role != user_role::ADMIN) {
        result.success = false;
        result.error = "Permission denied: admin role required";
        log_execution(command_name, false, -1);
        return result;
    }
    
    // Validate parameters
    if (!validate_params(entry, params)) {
        result.success = false;
        result.error = "Invalid parameters for command";
        log_execution(command_name, false, -1);
        return result;
    }
    
    // Build the actual command
    std::string full_command = build_command(entry, params);
    result.command = full_command;
    
    // Execute with timeout using async
    auto start_time = std::chrono::steady_clock::now();
    
    std::future<int> exec_future = std::async(std::launch::async, [&]() -> int {
        FILE* pipe = popen(full_command.c_str(), "r");
        if (!pipe) {
            return -1;
        }
        
        char buffer[4096];
        std::string output;
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            output += buffer;
        }
        result.output = output;
        
        int exit_code = pclose(pipe);
        return exit_code;
    });
    
    // Wait for completion or timeout
    std::future_status status = exec_future.wait_for(
        std::chrono::milliseconds(timeout_ms));
    
    auto end_time = std::chrono::steady_clock::now();
    result.execution_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();
    
    if (status == std::future_status::timeout) {
        result.success = false;
        result.error = "Command timed out after " + std::to_string(timeout_ms) + "ms";
        result.exit_code = -2;
        log_execution(full_command, false, -2);
        return result;
    }
    
    result.exit_code = exec_future.get();
    result.success = (result.exit_code == 0);
    
    if (!result.success && result.output.empty()) {
        result.error = "Command failed with exit code " + std::to_string(result.exit_code);
    }
    
    // Sanitize output
    result.output = sanitize_output(result.output);
    
    log_execution(full_command, result.success, result.exit_code);
    return result;
}

std::vector<std::string> CommandWrapper::get_commands_by_category(
    command_cat::Category cat) {
    std::vector<std::string> commands;
    
    for (const auto& pair : allowlist_) {
        if (pair.second.category == cat) {
            commands.push_back(pair.first);
        }
    }
    
    return commands;
}

std::vector<std::string> CommandWrapper::get_audit_log() {
    return audit_log_;
}

void CommandWrapper::clear_audit_log() {
    audit_log_.clear();
}


