#pragma once
#include <string>
#include "utils.h"

namespace system_ops {

    // Initializes the CommandWrapper with default commands (call once at startup).
    void initialize();

    // Execute using CommandWrapper with role-based access control.
    // Returns a CommandResult with full execution details.
    CommandResult execute_command(const std::string& command_name,
                                  const std::string& params = "",
                                  user_role::Role role = user_role::GUEST);

    // Executes a predefined command by name, looked up from the hardcoded allowlist.
    // No user input ever reaches the shell — only the allowlist keys are accepted.
    // Returns the command's stdout output, or an error string if the name is unknown.
    std::string run_command(const std::string& command_name);


    // Returns a trimmed single-line value for dashboard cards.
    // Used by the /api/dashboard endpoint.
    std::string get_hostname();
    std::string get_uptime();
    std::string get_disk_usage();
    std::string get_ip_address();

    // Validates and sets a new system hostname.
    // Input is strictly validated before any shell interaction.
    std::string set_hostname(const std::string& name);

    // Returns the active/inactive status of a known service.
    // Only accepts service names from an internal hardcoded list.
    std::string get_service_status(const std::string& service_name);

    // Runs `tailscale up` with an optional auth key (pre-validated by caller).
    std::string tailscale_up(const std::string& authkey = "");

}
