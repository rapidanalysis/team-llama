#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <ctime>
#include <memory>

// ============================================================================
// COMMAND CATEGORIES
// ============================================================================
namespace command_cat {
    enum Category {
        SYSTEM,      // disk, memory, processes
        NETWORK,     // ip, port, connectivity
        DOCKER,      // container management
        CONTAINER,  // individual container ops
        ADMIN        // system administration
    };
}

// ============================================================================
// USER ROLES & PERMISSIONS
// ============================================================================
namespace user_role {
    enum Role {
        ADMIN,   // Full access
        GUEST,   // Limited - can view, limited commands
        VIEWER   // Read-only - status only
    };

    // Permission bitmask
    enum Permission {
        PERM_VIEW_STATUS  = 1 << 0,
        PERM_RUN_SYS     = 1 << 1,
        PERM_RUN_NETWORK = 1 << 2,
        PERM_RUN_DOCKER  = 1 << 3,
        PERM_CONTAINER    = 1 << 4,
        PERM_ADMIN       = 1 << 5
    };

    // Get permissions for a role
    int get_permissions(Role role);
    
    // Check if role has permission
    bool has_permission(Role role, Permission perm);
}

// ============================================================================
// COMMAND RESULT STRUCT
// ============================================================================
struct CommandResult {
    bool success;                    // Command execution status
    int exit_code;                   // Process exit code
    std::string output;              // stdout/stderr
    std::string error;               // Error message
    std::chrono::system_clock::time_point timestamp;
    std::string command;             // Command that was executed
    int execution_time_ms;          // Execution duration
    
    CommandResult() : success(false), exit_code(-1), execution_time_ms(0) {}
};

// ============================================================================
// COMMAND ALLOWLIST ENTRY
// ============================================================================
struct CommandEntry {
    std::string name;                // Command identifier
    std::string command;             // Shell command template
    command_cat::Category category; // Command category
    bool requires_admin;            // Requires admin role
    std::string description;         // Human-readable description
    int timeout_sec;                // Command timeout (default 30s)
    std::vector<std::string> allowed_params;  // Allowed parameter values
};

// ============================================================================
// COMMAND WRAPPER CLASS
// ============================================================================
class CommandWrapper {
public:
    // Singleton accessor
    static CommandWrapper& get_instance();
    
    // Initialize default commands
    void initialize_defaults();
    
    // Add a command to the allowlist
    bool add_command(const CommandEntry& entry);
    
    // Execute a command by name with optional parameters
    // Returns CommandResult with success/failure, output, timing
    CommandResult execute(const std::string& command_name, 
                          const std::string& params = "",
                          user_role::Role role = user_role::VIEWER);
    
    // Execute with timeout (milliseconds)
    CommandResult execute_with_timeout(const std::string& command_name,
                                        const std::string& params,
                                        user_role::Role role,
                                        int timeout_ms);
    
    // Sanitize output - escape potentially dangerous characters
    std::string sanitize_output(const std::string& output);
    
    // Check if command exists in allowlist
    bool is_allowed(const std::string& command_name);
    
    // Get command info
    const CommandEntry* get_command_info(const std::string& name);
    
    // Get all commands by category
    std::vector<std::string> get_commands_by_category(command_cat::Category cat);
    
    // Get execution log
    std::vector<std::string> get_audit_log();
    
    // Clear audit log
    void clear_audit_log();

private:
    CommandWrapper() = default;
    ~CommandWrapper() = default;
    
    // Allowlist storage
    std::map<std::string, CommandEntry> allowlist_;
    
    // Audit log
    std::vector<std::string> audit_log_;
    
    // Prevent copying
    CommandWrapper(const CommandWrapper&) = delete;
    CommandWrapper& operator=(const CommandWrapper&) = delete;
    
    // Build command with parameters
    std::string build_command(const CommandEntry& entry, const std::string& params);
    
    // Validate parameters against allowed list
    bool validate_params(const CommandEntry& entry, const std::string& params);
    
    // Log execution
    void log_execution(const std::string& cmd, bool success, int exit_code);
};

// ============================================================================
// LEGACY FUNCTIONS (Backward Compatibility)
// ============================================================================

// Rejects anything that isn't alphanumeric, a hyphen, or an underscore.
bool is_safe_identifier(const std::string& input, size_t max_length = 64);

// Asks the Linux kernel to verify if the string is a valid IPv4 address.
bool is_valid_ipv4(const std::string& ip_input);

// Executes a validated command via std::system()
int sys_exec(const std::string& cmd);

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

// Get current timestamp as formatted string
std::string get_timestamp();

// Validate that string contains only safe characters (no shell metacharacters)
bool is_shell_safe(const std::string& input);

#endif // UTILS_H
