/**
 * test_newt.cpp - Unit tests for newt_manager (FR-1 Newt client)
 *
 * Build and run:  make test
 * Covers the pure logic that needs no Docker: input validation (which also
 * guards against shell injection) and working out the tunnel state from
 * Newt's log output.
 */

#include <iostream>
#include <stdexcept>
#include <string>

#include "../include/newt_manager.h"

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

using newt_manager::validate;
using newt_manager::classify_logs;

static const std::string EP  = "https://pangolin.example.com";
// Made-up values in the same format Pangolin generates (never real credentials)
static const std::string ID  = "abc123def456ghi";
static const std::string SEC = "fakesecret0123456789abcdefghijklmnop";

// ── validate(): accepted input ────────────────────────────────────────────

TEST(validate_accepts_typical_values) {
    ASSERT(validate(EP, ID, SEC).empty(), "Typical Pangolin values should be valid");
    ASSERT(validate("https://pangolin.example.com:8443/", ID, SEC).empty(), "Port and trailing slash allowed");
    ASSERT(validate("http://192.168.1.20:3000", ID, SEC).empty(), "http and IP address allowed (local self-host)");
}

TEST(validate_empty_secret_only_when_allowed) {
    ASSERT(!validate(EP, ID, "").empty(), "Empty secret rejected by default");
    ASSERT(validate(EP, ID, "", true).empty(), "Empty secret allowed when reusing the saved one");
}

// ── validate(): rejected input ────────────────────────────────────────────

TEST(validate_rejects_missing_fields) {
    ASSERT(!validate("", ID, SEC).empty(), "Empty endpoint rejected");
    ASSERT(!validate(EP, "", SEC).empty(), "Empty Newt ID rejected");
}

TEST(validate_rejects_bad_endpoint) {
    ASSERT(!validate("pangolin.example.com", ID, SEC).empty(), "Missing scheme rejected");
    ASSERT(!validate("ftp://pangolin.example.com", ID, SEC).empty(), "Non-http scheme rejected");
    ASSERT(!validate("https://", ID, SEC).empty(), "Missing host rejected");
    ASSERT(!validate("https://a/../etc", ID, SEC).empty(), "Path traversal rejected");
    ASSERT(!validate("https://" + std::string(260, 'a'), ID, SEC).empty(), "Over-long endpoint rejected");
}

TEST(validate_blocks_shell_injection) {
    ASSERT(!validate("https://x.com;reboot", ID, SEC).empty(), "Semicolon in endpoint rejected");
    ASSERT(!validate("https://x.com$(id)", ID, SEC).empty(), "Command substitution in endpoint rejected");
    ASSERT(!validate("https://x.com x", ID, SEC).empty(), "Space in endpoint rejected");
    ASSERT(!validate(EP, "abc`id`", SEC).empty(), "Backticks in Newt ID rejected");
    ASSERT(!validate(EP, ID, "abc'def").empty(), "Quote in secret rejected");
    ASSERT(!validate(EP, ID, "abc\nNEWT_ID=evil").empty(), "Newline in secret rejected (env-file injection)");
}

TEST(validate_length_limits) {
    ASSERT(validate(EP, std::string(64, 'a'), SEC).empty(), "64-char Newt ID allowed");
    ASSERT(!validate(EP, std::string(65, 'a'), SEC).empty(), "65-char Newt ID rejected");
    ASSERT(validate(EP, ID, std::string(128, 'a')).empty(), "128-char secret allowed");
    ASSERT(!validate(EP, ID, std::string(129, 'a')).empty(), "129-char secret rejected");
}

// ── classify_logs(): tunnel state from Newt's log (real log lines) ───────

TEST(classify_connected) {
    std::string log =
        "INFO: 2026/10/20 08:25:03 Websocket connected\n"
        "INFO: 2026/10/20 08:25:04 Connecting to endpoint: pangolin.weburban.com\n"
        "INFO: 2026/10/20 08:25:04 Tunnel connection to server established successfully!\n"
        "INFO: 2026/10/20 08:25:05 Client connectivity setup. Ready to accept connections from clients!\n";
    std::string detail;
    ASSERT(classify_logs(log, &detail) == "connected", "Established tunnel should be 'connected'");
    ASSERT(detail.find("established") != std::string::npos, "Detail should be the deciding line");
}

TEST(classify_auth_failed) {
    std::string log =
        "ERROR: 2026/10/20 07:28:22 Failed to connect: failed to get token: failed to get token with "
        "status code: 400, body: {\"message\":\"No newt found with that newtId\"}. Retrying in 3s...\n";
    ASSERT(classify_logs(log, nullptr) == "auth_failed", "Token failure should be 'auth_failed'");
}

TEST(classify_connecting_when_no_result_yet) {
    ASSERT(classify_logs("INFO: Newt version 1.18.1\nINFO: Websocket connected\n", nullptr) == "connecting",
           "No result yet should be 'connecting'");
    ASSERT(classify_logs("", nullptr) == "connecting", "Empty log should be 'connecting'");
}

TEST(classify_latest_event_wins) {
    std::string reconnected =
        "ERROR: Failed to connect: dial tcp: i/o timeout\n"
        "INFO: Tunnel connection to server established successfully!\n";
    ASSERT(classify_logs(reconnected, nullptr) == "connected", "Recovery after an error should be 'connected'");

    std::string dropped =
        "INFO: Tunnel connection to server established successfully!\n"
        "ERROR: Failed to connect: dial tcp: i/o timeout\n";
    ASSERT(classify_logs(dropped, nullptr) == "error", "Losing the server after connecting should be 'error'");
}

TEST(classify_ignores_unrelated_errors) {
    std::string log =
        "INFO: Tunnel connection to server established successfully!\n"
        "WARN: health check for target 10.0.0.5:80 failed\n";
    ASSERT(classify_logs(log, nullptr) == "connected", "Unrelated warnings should not drop the state");
}

int main() {
    std::cout << "======================================\n";
    std::cout << "Testing newt_manager (Newt client)\n";
    std::cout << "======================================\n\n";

    std::cout << "--- validate() ---\n";
    RUN_TEST(validate_accepts_typical_values);
    RUN_TEST(validate_empty_secret_only_when_allowed);
    RUN_TEST(validate_rejects_missing_fields);
    RUN_TEST(validate_rejects_bad_endpoint);
    RUN_TEST(validate_blocks_shell_injection);
    RUN_TEST(validate_length_limits);

    std::cout << "\n--- classify_logs() ---\n";
    RUN_TEST(classify_connected);
    RUN_TEST(classify_auth_failed);
    RUN_TEST(classify_connecting_when_no_result_yet);
    RUN_TEST(classify_latest_event_wins);
    RUN_TEST(classify_ignores_unrelated_errors);

    std::cout << "\n======================================\n";
    std::cout << "Results: " << tests_passed << " passed, " << tests_failed << " failed\n";
    std::cout << "======================================\n";
    return tests_failed == 0 ? 0 : 1;
}
