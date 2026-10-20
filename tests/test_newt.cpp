/**
 * test_newt.cpp - Unit tests for newt_manager input validation and command building
 *
 * Run: make test
 *
 * These tests do not start Docker. They cover validate(), env_file_contents()
 * and build_run_command(), which decide what reaches the shell.
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
} while (0)

#define ASSERT_TRUE(cond) do { if (!(cond)) throw std::runtime_error("Assertion failed: " #cond); } while (0)

// Example values from the docker-compose.yml in the Newt repository, not real credentials.
static newt_manager::Settings good() {
    return {"https://pangolin.example.com", "2ix2t8xk22ubpfy", "nnisrfsdfc7prqsp9ewo1dvtvci50j5uiqotez00dgap0ii2"};
}

static bool ok(const newt_manager::Settings& s) { return newt_manager::validate(s).empty(); }

TEST(valid_settings_accepted) {
    ASSERT_TRUE(ok(good()));
    auto s = good(); s.endpoint = "https://pangolin.example.com/";      ASSERT_TRUE(ok(s));
    s = good();      s.endpoint = "https://10.0.0.5:8443";              ASSERT_TRUE(ok(s));
    s = good();      s.id = "site_01-a";                                ASSERT_TRUE(ok(s));
}

TEST(endpoint_must_be_https) {
    auto s = good(); s.endpoint = "http://pangolin.example.com";        ASSERT_TRUE(!ok(s));
    s = good();      s.endpoint = "pangolin.example.com";               ASSERT_TRUE(!ok(s));
    s = good();      s.endpoint = "";                                   ASSERT_TRUE(!ok(s));
}

TEST(endpoint_rejects_path_and_bad_port) {
    auto s = good(); s.endpoint = "https://pangolin.example.com/admin"; ASSERT_TRUE(!ok(s));
    s = good();      s.endpoint = "https://pangolin.example.com:0";     ASSERT_TRUE(!ok(s));
    s = good();      s.endpoint = "https://pangolin.example.com:70000"; ASSERT_TRUE(!ok(s));
    s = good();      s.endpoint = "https://pangolin.example.com:";      ASSERT_TRUE(!ok(s));
    s = good();      s.endpoint = "https://";                           ASSERT_TRUE(!ok(s));
}

TEST(endpoint_injection_blocked) {
    const char* bad[] = {
        "https://a.com;rm -rf /", "https://a.com$(id)", "https://a.com`id`",
        "https://a.com|cat", "https://a.com\nNEWT_SECRET=x", "https://a.com x"
    };
    for (auto b : bad) { auto s = good(); s.endpoint = b; ASSERT_TRUE(!ok(s)); }
}

TEST(id_and_secret_injection_blocked) {
    const char* bad[] = { "", "a b", "a;b", "a$b", "a'b", "a\"b", "a\nb", "a=b", "../x" };
    for (auto b : bad) {
        auto s = good(); s.id = b;     ASSERT_TRUE(!ok(s));
        s = good();      s.secret = b; ASSERT_TRUE(!ok(s));
    }
}

TEST(id_and_secret_length_limit) {
    auto s = good(); s.secret = std::string(128, 'a'); ASSERT_TRUE(ok(s));
    s.secret = std::string(129, 'a');                  ASSERT_TRUE(!ok(s));
    s = good(); s.id = std::string(129, 'b');          ASSERT_TRUE(!ok(s));
}

TEST(env_file_has_three_lines) {
    std::string env = newt_manager::env_file_contents(good());
    ASSERT_TRUE(env == "PANGOLIN_ENDPOINT=https://pangolin.example.com\n"
                       "NEWT_ID=2ix2t8xk22ubpfy\n"
                       "NEWT_SECRET=nnisrfsdfc7prqsp9ewo1dvtvci50j5uiqotez00dgap0ii2\n");
}

TEST(run_command_has_no_secret_and_no_privileges) {
    std::string cmd = newt_manager::build_run_command("config/newt.env");
    ASSERT_TRUE(cmd.find("--env-file config/newt.env") != std::string::npos);
    ASSERT_TRUE(cmd.find("--name xerxes-newt ") != std::string::npos);
    ASSERT_TRUE(cmd.find("fosrl/newt:latest") != std::string::npos);
    ASSERT_TRUE(cmd.find("--restart unless-stopped") != std::string::npos);
    ASSERT_TRUE(cmd.find("NEWT_SECRET") == std::string::npos);
    ASSERT_TRUE(cmd.find("--privileged") == std::string::npos);
    ASSERT_TRUE(cmd.find("NET_ADMIN") == std::string::npos);
}

int main() {
    std::cout << "=== newt_manager tests ===\n";
    RUN_TEST(valid_settings_accepted);
    RUN_TEST(endpoint_must_be_https);
    RUN_TEST(endpoint_rejects_path_and_bad_port);
    RUN_TEST(endpoint_injection_blocked);
    RUN_TEST(id_and_secret_injection_blocked);
    RUN_TEST(id_and_secret_length_limit);
    RUN_TEST(env_file_has_three_lines);
    RUN_TEST(run_command_has_no_secret_and_no_privileges);
    std::cout << "Results: " << tests_passed << " passed, " << tests_failed << " failed\n";
    return tests_failed == 0 ? 0 : 1;
}
