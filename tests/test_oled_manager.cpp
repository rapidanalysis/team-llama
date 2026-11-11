/**
 * test_oled_manager.cpp - Tests for oled_manager (FR-3 to FR-6 OLED display, I2C tab)
 *
 * Build and run:  make test
 * Runs the display service with no display attached, so it also checks that
 * the error is reported and the settings are kept (FR-6). Checks the replies
 * the /api/oled routes send, oled.conf, and the preview.
 * Uses a temporary config dir, so it never touches ~/.xerxes.
 */

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <sstream>
#include <streambuf>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

#include "../include/oled_manager.h"

static int tests_passed = 0;
static int tests_failed = 0;

// The service logs to std::cout from its own thread, so std::cout is discarded
// during the tests and results are printed through `out` instead.
struct NullBuf : std::streambuf {
    int overflow(int c) override { return c; }
};
static NullBuf g_null;
static std::ostream out(std::cout.rdbuf());

#define TEST(name) void test_##name()
#define RUN_TEST(name) do { \
    out << "Running " #name "... " << std::flush; \
    try { \
        test_##name(); \
        out << "PASSED\n"; \
        tests_passed++; \
    } catch (const std::exception& e) { \
        out << "FAILED: " << e.what() << "\n"; \
        tests_failed++; \
    } \
} while(0)

#define ASSERT(cond, msg) if (!(cond)) { \
    throw std::runtime_error(std::string(msg)); \
}

using nlohmann::json;
namespace om = oled_manager;

static std::string g_dir;

static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::vector<std::string> keys(std::initializer_list<const char*> list) {
    std::vector<std::string> v;
    for (const char* k : list) v.push_back(k);
    return v;
}

// Number of lit pixels in a preview, optionally only in columns [x0, x1)
static int lit_pixels(const json& rows, int x0 = 0, int x1 = 128) {
    int n = 0;
    for (const auto& row : rows) {
        std::string hex = row.get<std::string>();
        for (int x = x0; x < x1; x++) {
            int byte = std::stoi(hex.substr((x / 8) * 2, 2), nullptr, 16);
            if (byte & (0x80 >> (x % 8))) n++;
        }
    }
    return n;
}

// ---------------------------------------------------------------------------

// Written before initialize(), so this checks oled.conf is loaded
TEST(loads_saved_settings) {
    om::Reply r = om::status();
    ASSERT(r.http_status == 200, "status should be 200");
    ASSERT(r.body["enabled"] == false, "enabled=0 should load as off");
    ASSERT(r.body["mode"] == "qr", "mode should load as qr");
    ASSERT(r.body["stats"] == json(keys({"ip", "dns"})), "stats should load in display order");
    ASSERT(r.body["address"] == "0x3D", "address should load as 0x3D");
    ASSERT(r.body["device"] == "/dev/i2c-1", "device should be /dev/i2c-1");
    ASSERT(r.body["available_stats"].size() == 7, "there should be 7 stats to pick from");
}

TEST(split_stats) {
    ASSERT(om::split_stats("ip, dns,,cpu_temp ") == keys({"ip", "dns", "cpu_temp"}), "should split and trim");
    ASSERT(om::split_stats("").empty(), "empty string should give no stats");
}

TEST(rejects_bad_display_settings) {
    ASSERT(om::update_display("banner", keys({"ip"})).body["code"] == "invalid_mode", "unknown mode");
    ASSERT(om::update_display("stats", keys({"ip", "rm -rf /"})).body["code"] == "invalid_stat", "unknown stat");
    om::Reply r = om::update_display("stats", {});
    ASSERT(r.http_status == 400 && r.body["code"] == "no_stats", "stats mode needs a stat");
    ASSERT(om::status().body["mode"] == "qr", "rejected changes should not apply");
}

TEST(saves_display_settings) {
    om::Reply r = om::update_display("stats", keys({"cpu_temp", "hostname", "ip", "ip"}));
    ASSERT(r.http_status == 200 && r.body["success"] == true, "valid settings should save");
    ASSERT(r.body["stats"] == json(keys({"hostname", "ip", "cpu_temp"})), "stats should be ordered, no duplicates");
    std::string conf = read_file(g_dir + "/oled.conf");
    ASSERT(conf.find("mode=stats\n") != std::string::npos, "oled.conf should have the mode");
    ASSERT(conf.find("stats=hostname,ip,cpu_temp\n") != std::string::npos, "oled.conf should have the stats");

    // QR mode doesn't need stats
    ASSERT(om::update_display("qr", {}).http_status == 200, "qr mode without stats should save");
}

TEST(saves_hardware_settings) {
    ASSERT(om::update_settings(-1, 0x3C).body["code"] == "invalid_bus", "negative bus");
    ASSERT(om::update_settings(1, 0x78).body["code"] == "invalid_address", "address above 0x77");
    ASSERT(om::update_settings(1, 0x3C).http_status == 200, "valid settings should save");
    ASSERT(om::status().body["address"] == "0x3C", "address should update");
    ASSERT(read_file(g_dir + "/oled.conf").find("address=0x3C\n") != std::string::npos, "oled.conf should have the address");
}

TEST(power_is_saved) {
    ASSERT(om::set_enabled(true).body["enabled"] == true, "should turn on");
    ASSERT(read_file(g_dir + "/oled.conf").find("enabled=1\n") != std::string::npos, "oled.conf should say enabled=1");
    ASSERT(om::set_enabled(false).body["enabled"] == false, "should turn off");
}

// FR-6: with no display, the probe gives an error code and the settings stay.
// Uses bus 1023 so it fails the same way on a Pi with a display attached.
TEST(missing_display_reports_error_and_keeps_settings) {
    om::update_display("stats", keys({"ip", "dns"}));
    om::update_settings(1023, 0x3C);
    om::Reply r = om::probe();
    ASSERT(r.http_status == 502, "probe without a display should fail, got " + std::to_string(r.http_status));
    std::string code = r.body["code"];
    ASSERT(code == "no_bus" || code == "no_device" || code == "unsupported", "unexpected error code " + code);

    json s = om::status().body;
    ASSERT(s["display"]["state"] == "error" && s["display"]["code"] == code, "status should report the error");
    ASSERT(s["mode"] == "stats" && s["stats"] == json(keys({"ip", "dns"})), "settings should be kept");
    om::update_settings(1, 0x3C);
}

TEST(preview_draws_without_a_display) {
    om::Reply r = om::preview("stats", keys({"hostname", "ip"}));
    ASSERT(r.http_status == 200, "preview should work without a display");
    ASSERT(r.body["width"] == 128 && r.body["height"] == 64, "preview should be 128x64");
    ASSERT(r.body["rows"].size() == 64, "preview should have 64 rows");
    ASSERT(r.body["rows"][0].get<std::string>().size() == 32, "each row should be 32 hex digits");
    ASSERT(lit_pixels(r.body["rows"]) > 0, "stats preview should have text");

    // The QR code (or "No network") is drawn on the left half
    om::Reply qr = om::preview("qr", {});
    ASSERT(lit_pixels(qr.body["rows"], 0, 64) > 0, "qr preview should draw on the left");

    ASSERT(om::preview("stats", {}).body["rows"].size() == 64, "empty preview is allowed");
    ASSERT(lit_pixels(om::preview("stats", {}).body["rows"]) == 0, "no stats should be a blank screen");
    ASSERT(om::preview("stats", keys({"nope"})).body["code"] == "invalid_stat", "preview validates stats");
}

TEST(preview_without_mode_uses_saved_settings) {
    om::update_display("stats", keys({"ip"}));
    om::Reply saved = om::preview("", {});
    ASSERT(saved.body["mode"] == "stats" && saved.body["stats"] == json(keys({"ip"})), "should use saved settings");
    ASSERT(saved.body["rows"] == om::preview("stats", keys({"ip"})).body["rows"], "should match the same draft");
}

// ---------------------------------------------------------------------------

int main() {
    char tmpl[] = "/tmp/xerxes_oled_test_XXXXXX";
    if (!mkdtemp(tmpl)) {
        out << "could not create a temp dir\n";
        return 1;
    }
    g_dir = tmpl;
    setenv("XERXES_CONFIG_DIR", g_dir.c_str(), 1);
    std::ofstream(g_dir + "/oled.conf") << "enabled=0\nmode=qr\nstats=dns,ip\nbus=1\naddress=0x3D\n";

    std::cout.rdbuf(&g_null);
    om::initialize(8000);

    out << "\n=== oled_manager tests ===\n\n";
    RUN_TEST(loads_saved_settings);
    RUN_TEST(split_stats);
    RUN_TEST(rejects_bad_display_settings);
    RUN_TEST(saves_display_settings);
    RUN_TEST(saves_hardware_settings);
    RUN_TEST(power_is_saved);
    RUN_TEST(missing_display_reports_error_and_keeps_settings);
    RUN_TEST(preview_draws_without_a_display);
    RUN_TEST(preview_without_mode_uses_saved_settings);

    out << "\n" << tests_passed << " passed, " << tests_failed << " failed\n";
    std::remove((g_dir + "/oled.conf").c_str());
    rmdir(g_dir.c_str());
    return tests_failed == 0 ? 0 : 1;
}
