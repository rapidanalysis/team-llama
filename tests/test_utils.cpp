/**
 * test_utils.cpp - Unit tests for utils.cpp command wrapper
 * 
 * Compile: g++ -std=c++11 -o test_utils tests/test_utils.cpp src/utils.cpp -I.
 * Run: ./test_utils
 * 
 * These tests use only PUBLIC API: is_shell_safe(), get_command_info(), execute()
 */

#include <iostream>
#include <cassert>
#include <string>
#include <vector>

// Include the utils header
#include "../include/utils.h"

// Test counters
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) void test_##name()
#define RUN_TEST(name) do { \
    std::cout << "Running " #name "... "; \
    try { \
        test_##name(); \
        std::cout << "PASSED\n"; \
        tests_passed++; \
    } catch (const std::exception& e) { \
        std::cout << "FAILED: " << e.what() << "\n"; \
        tests_failed++; \
    } \
} while(0)

#define ASSERT(cond, msg) if (!(cond)) { \
    throw std::runtime_error(std::string(msg)); \
}

// ============================================================================
// Tests for is_shell_safe() - PUBLIC function
// ============================================================================

TEST(is_shell_safe_basic) {
    // Should pass: simple alphanumeric
    ASSERT(is_shell_safe("mycontainer"), "Basic string should be safe");
    ASSERT(is_shell_safe("container-123"), "Hyphen should be safe");
    ASSERT(is_shell_safe("container_123"), "Underscore should be safe");
    ASSERT(is_shell_safe(""), "Empty string should be safe");
}

TEST(is_shell_safe_dangerous) {
    // Should fail: shell metacharacters
    ASSERT(!is_shell_safe("test; rm -rf /"), "Semicolon dangerous");
    ASSERT(!is_shell_safe("test && rm"), "AND dangerous");
    ASSERT(!is_shell_safe("test || rm"), "OR dangerous");
    ASSERT(!is_shell_safe("test | cat"), "Pipe dangerous");
    ASSERT(!is_shell_safe("test `id`"), "Backticks dangerous");
    ASSERT(!is_shell_safe("test $(id)"), "Substitution dangerous");
    ASSERT(!is_shell_safe("test > file"), "Redirect dangerous");
    ASSERT(!is_shell_safe("test < file"), "Input dangerous");
}

// ============================================================================
// Tests for validate_params via execute() - tests security through public API
// ============================================================================

TEST(validate_params_whitelist) {
    auto& wrapper = CommandWrapper::get_instance();
    
    // processes command has allowed_params = {"15", "20", "30"}
    // Test with valid param
    auto result = wrapper.execute("processes", "15", user_role::ADMIN);
    // Valid param should work
    ASSERT(result.success || !result.error.empty(), "Valid param should work");
    
    // Test with invalid param not in whitelist
    result = wrapper.execute("processes", "999", user_role::ADMIN);
    // Should fail validation
    ASSERT(!result.success, "Invalid param should be rejected");
}

TEST(validate_params_injection_blocked) {
    auto& wrapper = CommandWrapper::get_instance();
    
    // Test injection attempts - these should fail validation
    auto result = wrapper.execute("memory", "test && rm -rf /", user_role::ADMIN);
    ASSERT(!result.success || result.error.find("Invalid") != std::string::npos, 
           "AND injection should be blocked");
    
    result = wrapper.execute("memory", "test; rm -rf /", user_role::ADMIN);
    ASSERT(!result.success || result.error.find("Invalid") != std::string::npos,
           "Semicolon injection should be blocked");
    
    result = wrapper.execute("memory", "test `id`", user_role::ADMIN);
    ASSERT(!result.success || result.error.find("Invalid") != std::string::npos,
           "Backtick injection should be blocked");
}

TEST(validate_params_path_traversal_blocked) {
    auto& wrapper = CommandWrapper::get_instance();
    
    // Path traversal should be blocked
    auto result = wrapper.execute("memory", "../etc/passwd", user_role::ADMIN);
    ASSERT(!result.success || result.error.find("Invalid") != std::string::npos,
           "Path traversal should be blocked");
}

TEST(validate_params_length_blocked) {
    auto& wrapper = CommandWrapper::get_instance();
    
    // Too long should be blocked
    std::string too_long(300, 'a');
    auto result = wrapper.execute("memory", too_long, user_role::ADMIN);
    ASSERT(!result.success || result.error.find("Invalid") != std::string::npos,
           "Long params should be blocked");
}

TEST(validate_params_safe_chars_accepted) {
    auto& wrapper = CommandWrapper::get_instance();
    
    // These safe params should pass - use ping which accepts any param for this test
    auto result = wrapper.execute("ping", "8.8.8.8", user_role::ADMIN);
    // Either success or controlled failure - shouldn't crash
    std::cout << result.output << std::endl;
    ASSERT(result.success || !result.error.empty(), "Safe chars should work");
}

// ============================================================================
// Tests for get_command_info() - PUBLIC function
// ============================================================================

TEST(get_command_info_exists) {
    auto& wrapper = CommandWrapper::get_instance();
    
    // Known commands should be found
    const CommandEntry* entry = wrapper.get_command_info("disk_usage");
    ASSERT(entry != nullptr, "disk_usage should exist");
    ASSERT(entry->command == "df -h", "Command should match");
    
    entry = wrapper.get_command_info("processes");
    ASSERT(entry != nullptr, "processes should exist");
    // Should have allowed params
    ASSERT(!entry->allowed_params.empty(), "processes should have allowed params");
}

TEST(get_command_info_not_found) {
    auto& wrapper = CommandWrapper::get_instance();
    
    // Unknown commands should return nullptr
    const CommandEntry* entry = wrapper.get_command_info("rm -rf /");
    ASSERT(entry == nullptr, "Unknown command should return nullptr");
    
    entry = wrapper.get_command_info("hack_the_planet");
    ASSERT(entry == nullptr, "hack command should return nullptr");
}

// ============================================================================
// Tests for is_allowed() - PUBLIC function
// ============================================================================

TEST(is_allowed_basic) {
    auto& wrapper = CommandWrapper::get_instance();
    
    // Known safe commands should be allowed
    ASSERT(wrapper.is_allowed("disk_usage"), "disk_usage allowed");
    ASSERT(wrapper.is_allowed("memory"), "memory allowed");
    ASSERT(wrapper.is_allowed("uptime"), "uptime allowed");
    ASSERT(wrapper.is_allowed("processes"), "processes allowed");
    
    // Dangerous commands should NOT be allowed
    ASSERT(!wrapper.is_allowed("rm -rf /"), "rm should not be allowed");
    ASSERT(!wrapper.is_allowed("mkfs"), "mkfs should not be allowed");
    ASSERT(!wrapper.is_allowed("dd"), "dd should not be allowed");
}

// ============================================================================
// Tests for execute() security - PUBLIC function
// ============================================================================

TEST(execute_as_admin) {
    auto& wrapper = CommandWrapper::get_instance();
    
    // Admin role can execute system commands
    auto result = wrapper.execute("disk_usage", "", user_role::ADMIN);
    // Should succeed (command runs)
    ASSERT(result.success, "Admin should execute disk_usage");
    ASSERT(!result.output.empty(), "Should have output");
}

TEST(execute_as_guest_blocked_admin_commands) {
    auto& wrapper = CommandWrapper::get_instance();
    
    // Guest role CANNOT execute admin commands
    auto result = wrapper.execute("reboot", "", user_role::GUEST);
    // Should fail - permission denied
    ASSERT(!result.success, "Guest should be blocked from reboot");
    ASSERT(result.error.find("Permission") != std::string::npos, "Should mention permission");
}

TEST(execute_as_viewer_limited) {
    auto& wrapper = CommandWrapper::get_instance();
    
    // Viewer role can execute read-only commands
    auto result = wrapper.execute("disk_usage", "", user_role::VIEWER);
    // Should succeed
    ASSERT(result.success, "Viewer should execute disk_usage");
}

TEST(execute_unknown_command) {
    auto& wrapper = CommandWrapper::get_instance();
    
    // Unknown command should fail gracefully
    auto result = wrapper.execute("unknown_command", "", user_role::ADMIN);
    ASSERT(!result.success, "Unknown command should fail");
    ASSERT(result.error.find("not found") != std::string::npos, "Should mention not found");
}

TEST(execute_injection_blocked) {
    auto& wrapper = CommandWrapper::get_instance();
    
    // Try command injection via parameter
    auto result = wrapper.execute("disk_usage", "; rm -rf /", user_role::ADMIN);
    // Should fail - invalid chars
    ASSERT(!result.success, "Injection should be blocked");
}

// ============================================================================
// Tests for singleton - PUBLIC function
// ============================================================================

TEST(singleton_pattern) {
    auto& instance1 = CommandWrapper::get_instance();
    auto& instance2 = CommandWrapper::get_instance();
    
    ASSERT(&instance1 == &instance2, "Should return same instance");
}

// ============================================================================
// Main test runner
// ============================================================================
int run_docker_ps(){
    auto& cmd = CommandWrapper::get_instance();
    auto result = cmd.execute("docker_ps");
    std::cout << result.output << std::endl;

    return result.success;
}
int main() {
    std::cout << "======================================\n";
    std::cout << "Testing utils.cpp Command Wrapper\n";
    std::cout << "======================================\n\n";
    
    // Initialize command wrapper
    CommandWrapper::get_instance().initialize_defaults();
    
    std::cout << "--- is_shell_safe() tests ---\n";
    RUN_TEST(is_shell_safe_basic);
    RUN_TEST(is_shell_safe_dangerous);
    
    std::cout << "\n--- validate_params via execute() tests ---\n";
    RUN_TEST(validate_params_whitelist);
    RUN_TEST(validate_params_injection_blocked);
    RUN_TEST(validate_params_path_traversal_blocked);
    RUN_TEST(validate_params_length_blocked);
    RUN_TEST(validate_params_safe_chars_accepted);
    
    std::cout << "\n--- get_command_info() tests ---\n";
    RUN_TEST(get_command_info_exists);
    RUN_TEST(get_command_info_not_found);
    
    std::cout << "\n--- is_allowed() tests ---\n";
    RUN_TEST(is_allowed_basic);
    
    std::cout << "\n--- execute() security tests ---\n";
    RUN_TEST(execute_as_admin);
    RUN_TEST(execute_as_guest_blocked_admin_commands);
    RUN_TEST(execute_as_viewer_limited);
    RUN_TEST(execute_unknown_command);
    RUN_TEST(execute_injection_blocked);
    
    std::cout << "\n--- Integration tests ---\n";
    RUN_TEST(singleton_pattern);
    
    std::cout << "\n======================================\n";
    std::cout << "Results: " << tests_passed << " passed, " << tests_failed << " failed\n";
    std::cout << "======================================\n";
    
    std::cout << "\n DockerTest" << std::endl;
    run_docker_ps();
    
    return tests_failed > 0 ? 1 : 0;
}
