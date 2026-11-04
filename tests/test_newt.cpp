/**
 * test_newt.cpp - Unit tests for newt_manager (FR-1 Newt client)
 *
 * Build and run:  make test
 * Covers the pure logic that needs no Docker: input validation (which also
 * guards against shell injection), working out the tunnel state from Newt's
 * log output, and the update / Docker-access / conflict helpers.
 */

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unistd.h>

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
using newt_manager::parse_version;
using newt_manager::update_needed;
using newt_manager::auto_update_due;
using newt_manager::classify_docker_output;
using newt_manager::docker_fixes;
using newt_manager::parse_containers;
using newt_manager::docker_command_for;

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

// ── parse_version(): running version from the container log ─────────────

TEST(parse_version_from_log) {
    std::string log =
        "INFO: 2026/10/20 08:25:03 Newt version 1.18.1\n"
        "INFO: 2026/10/20 08:25:03 Websocket connected\n";
    ASSERT(parse_version(log) == "1.18.1", "Version should be read from the startup line");
    ASSERT(parse_version("INFO: Newt version: v1.19.0-rc.1\n") == "1.19.0-rc.1", "'v' prefix and pre-release handled");
    ASSERT(parse_version("INFO: Server version 1.21.1\nINFO: Websocket connected\n").empty(),
           "Pangolin's server version must not be taken as Newt's");
    ASSERT(parse_version("").empty(), "No log gives no version");
    ASSERT(parse_version("INFO: Newt version 1.18.1\nINFO: Newt version 1.19.0\n") == "1.19.0",
           "Latest start-up line wins after a restart");
}

// ── update decisions ─────────────────────────────────────────────────────

TEST(update_needed_rules) {
    ASSERT(!update_needed("sha256:aaa", "sha256:aaa", "", false), "Same image: nothing to do");
    ASSERT(update_needed("sha256:aaa", "sha256:bbb", "", false), "Different image: update");
    ASSERT(!update_needed("", "sha256:bbb", "", false), "No running container: nothing to update");
    ASSERT(!update_needed("sha256:aaa", "", "", false), "Pull failed / unknown latest: don't update");
    ASSERT(!update_needed("sha256:aaa", "sha256:bbb", "sha256:bbb", true),
           "Automatic update skips a version that was rolled back");
    ASSERT(update_needed("sha256:aaa", "sha256:bbb", "sha256:bbb", false),
           "Manual Update Now can still install a skipped version");
}

TEST(auto_update_schedule) {
    const long long now = 1793700000LL, day = 24 * 60 * 60;
    ASSERT(auto_update_due(true, true, now, 0), "Never checked: due");
    ASSERT(!auto_update_due(true, true, now, now - day + 60), "Checked 23h59m ago: not due");
    ASSERT(auto_update_due(true, true, now, now - day), "Checked 24h ago: due");
    ASSERT(!auto_update_due(false, true, now, 0), "Turned off: never due");
    ASSERT(!auto_update_due(true, false, now, 0), "Newt not set up: never due");
    ASSERT(auto_update_due(true, true, now, now + day), "Clock moved back: check again rather than wait");
}

// Automatic updates can't be turned on before Newt is set up
static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

TEST(auto_update_needs_newt_set_up) {
    char dir[] = "/tmp/xerxes-newt-test-XXXXXX";
    ASSERT(mkdtemp(dir) != nullptr, "Could not create a temp config dir");
    setenv("XERXES_CONFIG_DIR", dir, 1);
    std::string settings = std::string(dir) + "/newt-update.conf";

    auto on = newt_manager::set_auto_update(true);
    ASSERT(!on.success, "Turning on before set-up is refused");
    ASSERT(on.code == "not_configured", "Refusal says Newt isn't set up");
    ASSERT(read_file(settings).find("AUTO_UPDATE=1") == std::string::npos, "Nothing saved when refused");

    ASSERT(newt_manager::set_auto_update(false).success, "Turning off is always allowed");

    std::ofstream(std::string(dir) + "/newt.env") << "NEWT_ID=" << ID << "\n";
    ASSERT(newt_manager::set_auto_update(true).success, "Allowed once Newt is set up");
    ASSERT(read_file(settings).find("AUTO_UPDATE=1") != std::string::npos, "Setting saved");

    std::remove(settings.c_str());
    std::remove((std::string(dir) + "/newt.env").c_str());
    rmdir(dir);
    unsetenv("XERXES_CONFIG_DIR");
}

// ── Docker access check (D-01) ───────────────────────────────────────────

TEST(classify_docker_output_states) {
    // Real message from the sponsor's Pi (18 Oct)
    ASSERT(classify_docker_output("permission denied while trying to connect to the Docker daemon socket at "
                                  "unix:///var/run/docker.sock: Get \"http://%2Fvar%2Frun%2Fdocker.sock/v1.47/version\": "
                                  "dial unix /var/run/docker.sock: connect: permission denied", 1) == "permission_denied",
           "Socket permission error");
    ASSERT(classify_docker_output("Cannot connect to the Docker daemon at unix:///var/run/docker.sock. "
                                  "Is the docker daemon running?", 1) == "daemon_down", "Daemon stopped");
    ASSERT(classify_docker_output("sh: 1: docker: not found", 127) == "not_installed", "Docker missing");
    ASSERT(classify_docker_output("sudo: a password is required", 1) == "sudo_denied", "No sudoers rule");
    ASSERT(classify_docker_output("26.1.5+dfsg1", 0) == "ok", "Server version printed: ok");
    ASSERT(classify_docker_output("something else went wrong", 1) == "error", "Unknown failure");
}

TEST(docker_fixes_for_each_state) {
    auto fixes = docker_fixes("permission_denied", "comp3850");
    ASSERT(fixes.size() == 3, "Three options for permission denied: service, docker group, sudoers");
    ASSERT(fixes[0].commands[0].find("deploy_systemd.sh") != std::string::npos, "First option is the systemd service");
    ASSERT(fixes[1].commands[0] == "sudo usermod -aG docker comp3850", "Docker group command names the user");
    ASSERT(docker_fixes("daemon_down", "x")[0].commands[0] == "sudo systemctl enable --now docker", "Start Docker");
    ASSERT(!docker_fixes("not_installed", "x").empty(), "Install instructions when Docker is missing");
    ASSERT(docker_fixes("sudo_denied", "x").size() == 1, "Only the sudoers fix when sudo refused");
    ASSERT(docker_fixes("ok", "x").empty(), "No fixes when Docker works");

    auto odd = docker_fixes("permission_denied", "bad;name");
    ASSERT(odd[1].commands[0].find("bad;name") == std::string::npos, "Unsafe user name never put in a command");
    ASSERT(odd[1].commands[0].find("<your-user>") != std::string::npos, "Placeholder used instead");
}

TEST(docker_command_allowlist) {
    ASSERT(docker_command_for(nullptr) == "docker", "Default is docker");
    ASSERT(docker_command_for("sudo -n docker") == "sudo -n docker", "sudo -n docker accepted");
    ASSERT(docker_command_for("rm -rf / ; docker") == "docker", "Anything else falls back to docker");
}

// ── other Newt instances (D-05) ──────────────────────────────────────────

TEST(parse_containers_finds_other_newts) {
    std::string ps =
        "xerxes-newt|fosrl/newt:latest|Up 2 hours\n"
        "newt|fosrl/newt:latest|Up 3 days\n"
        "nginx|nginx:latest|Up 5 minutes\n"
        "pangolin-site|fosrl/newt:1.18.1|Up 1 hour\n";
    auto found = parse_containers(ps);
    ASSERT(found.size() == 2, "Two other Newt containers expected");
    ASSERT(found[0].name == "newt" && found[0].kind == "container", "Manually started 'newt' container found");
    ASSERT(found[1].name == "pangolin-site", "Newt image under another name found");
    ASSERT(parse_containers("xerxes-newt|fosrl/newt:latest|Up\n").empty(), "Our own container is not a conflict");
    ASSERT(parse_containers("").empty(), "No containers, no conflicts");
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

    std::cout << "\n--- updates ---\n";
    RUN_TEST(parse_version_from_log);
    RUN_TEST(update_needed_rules);
    RUN_TEST(auto_update_schedule);
    RUN_TEST(auto_update_needs_newt_set_up);

    std::cout << "\n--- Docker access and other instances ---\n";
    RUN_TEST(classify_docker_output_states);
    RUN_TEST(docker_fixes_for_each_state);
    RUN_TEST(docker_command_allowlist);
    RUN_TEST(parse_containers_finds_other_newts);

    std::cout << "\n======================================\n";
    std::cout << "Results: " << tests_passed << " passed, " << tests_failed << " failed\n";
    std::cout << "======================================\n";
    return tests_failed == 0 ? 0 : 1;
}
