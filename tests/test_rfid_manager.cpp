/**
 * test_rfid_manager.cpp - Tests for rfid_manager (FR-12 RFID reader, I2C tab)
 *
 * Build and run:  make test
 * Runs the reader service against the simulated module, taps tags and checks
 * the replies the /api/rfid routes send, tags.json, and the commands run.
 * Uses a temporary config dir, so it never touches ~/.xerxes.
 */

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <streambuf>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "../include/rfid_manager.h"
#include "mfrc522_sim.h"

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

namespace fs = std::filesystem;
using nlohmann::json;
namespace rm = rfid_manager;

static const std::string KNOWN = "DC:BD:6A:03";
static const std::string UNKNOWN = "11:22:33:44";
static const std::string NTAG = "04:30:AD:00:64:1C:03";

static std::string g_dir;
static std::string g_tags_path;

static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void write_file(const std::string& path, const std::string& text) {
    std::ofstream(path) << text;
}

static bool wait_until(const std::function<bool()>& done, double seconds = 8) {
    auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < end) {
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

// Collects events; wait() takes the first match and keeps the rest for later.
class Events {
public:
    void skip_all() {
        fetch();
        backlog_.clear();
    }

    json wait(const std::function<bool(const json&)>& match, const std::string& what) {
        json found;
        bool ok = wait_until([&] {
            fetch();
            for (size_t i = 0; i < backlog_.size(); ++i) {
                if (match(backlog_[i])) {
                    found = backlog_[i];
                    backlog_.erase(backlog_.begin() + static_cast<long>(i));
                    return true;
                }
            }
            return false;
        });
        if (!ok) throw std::runtime_error("timed out waiting for " + what);
        return found;
    }

private:
    void fetch() {
        json body = rm::events(since_).body;
        since_ = body["next"].get<uint64_t>();
        for (const json& e : body["events"]) backlog_.push_back(e);
    }

    uint64_t since_ = 0;
    std::vector<json> backlog_;
};

static Events g_events;

static auto result_for(const std::string& id) {
    return [id](const json& e) { return e["kind"] == "result" && e["id"] == id; };
}

static json wait_for_scan() {
    json r;
    bool ok = wait_until([&] {
        r = rm::scan_state().body;
        return r["state"] != "waiting";
    });
    if (!ok) throw std::runtime_error("scan never finished");
    return r;
}

// ── validation helpers ─────────────────────────────────────────────────────

TEST(tag_id_normalised) {
    std::string id;
    ASSERT(rm::normalise_tag_id("dc:bd:6a:03", &id).empty() && id == KNOWN, "Lower case accepted");
    ASSERT(rm::normalise_tag_id(" DC BD 6A 03 ", &id).empty() && id == KNOWN, "Spaces accepted");
    ASSERT(rm::normalise_tag_id("04-30-AD-00-64-1C-03", &id).empty() && id == NTAG, "Dashes accepted");
    ASSERT(rm::normalise_tag_id("DCBD6A03", &id).empty() && id == KNOWN, "No separators accepted");
}

TEST(tag_id_rejected) {
    ASSERT(!rm::normalise_tag_id("xyz", nullptr).empty(), "Non-hex rejected");
    ASSERT(!rm::normalise_tag_id("AA:BB", nullptr).empty(), "Too short rejected");
    ASSERT(!rm::normalise_tag_id("AA:BB:CC:D", nullptr).empty(), "Odd digit count rejected");
    ASSERT(!rm::normalise_tag_id("AA:BB:CC:DD:EE:FF:00:11:22:33:44", nullptr).empty(), "Too long rejected");
}

TEST(command_validated) {
    ASSERT(rm::validate_command("apt update").empty(), "Normal command accepted");
    ASSERT(!rm::validate_command("   ").empty(), "Blank rejected");
    ASSERT(!rm::validate_command("a\nb").empty(), "Multi-line rejected");
    ASSERT(!rm::validate_command(std::string(1025, 'a')).empty(), "Over 1024 characters rejected");
}

// ── status and probe ───────────────────────────────────────────────────────

TEST(starts_idle_with_defaults) {
    json st = rm::status().body;
    ASSERT(st["run_enabled"] == false, "Run mode off at start");
    ASSERT(st["module"]["state"] == "idle", "Module not opened until needed");
    ASSERT(st["tags_path"] == g_tags_path, "Default tag file in the config dir");
    ASSERT(st["bus"] == 1 && st["address"] == "0x2C", "Default bus and address");
}

TEST(probe_finds_module) {
    rm::Reply r = rm::probe();
    ASSERT(r.http_status == 200 && r.body["success"] == true, "Probe finds the simulated module");
}

// ── tags ───────────────────────────────────────────────────────────────────

TEST(no_tag_file_means_no_tags) {
    rm::Reply r = rm::list_tags();
    ASSERT(r.http_status == 200 && r.body["tags"].empty(), "Missing tags.json lists as empty");
}

TEST(add_tag) {
    rm::Reply r = rm::add_tag("dc bd 6a 03", "  echo hello  ");
    ASSERT(r.http_status == 200, "Add succeeds");
    ASSERT(r.body["tag"] == json({{"id", KNOWN}, {"command", "echo hello"}}), "ID normalised, command trimmed");
    json saved = json::parse(read_file(g_tags_path));
    ASSERT(saved == json::parse(R"({"tags": [{"id": "DC:BD:6A:03", "command": "echo hello"}]})"),
           "tags.json uses the xerxes-rfid format");
}

TEST(add_existing_tag_conflicts) {
    rm::Reply r = rm::add_tag(KNOWN, "echo other");
    ASSERT(r.http_status == 409, "Second add is a conflict");
    ASSERT(r.body["existing_command"] == "echo hello", "Conflict reports the current command");
}

TEST(add_rejects_bad_input) {
    ASSERT(rm::add_tag("xyz", "a").http_status == 400, "Bad ID rejected");
    ASSERT(rm::add_tag(UNKNOWN, "").http_status == 400, "Empty command rejected");
}

TEST(edit_unregistered_tag_not_found) {
    ASSERT(rm::edit_tag(UNKNOWN, "x").http_status == 404, "Edit of an unknown tag is 404");
}

// ── scanning a tag to register it ──────────────────────────────────────────

TEST(scan_new_tag) {
    rm::start_scan();
    json r = rm::scan_state().body;
    ASSERT(r["state"] == "waiting" && r["seconds_left"].get<int>() > 25, "Scan waits for a tag");
    rfid_sim::tap(NTAG);
    r = wait_for_scan();
    ASSERT(r["state"] == "done" && r["id"] == NTAG, "Scanned the 7-byte NTAG");
    ASSERT(r["type"] == "ntag" && r["registered"] == false, "NTAG is not registered yet");
}

TEST(scan_registered_tag) {
    rm::start_scan();
    rfid_sim::tap(KNOWN);
    json r = wait_for_scan();
    ASSERT(r["registered"] == true && r["existing_command"] == "echo hello", "Scan reports the existing command");
}

TEST(scan_cancel) {
    rm::start_scan();
    rm::cancel_scan();
    ASSERT(rm::scan_state().body["state"] == "cancelled", "Scan can be cancelled");
}

TEST(scan_never_runs_commands) {
    std::string marker = g_dir + "/ran.txt";
    ASSERT(rm::add_tag(NTAG, "echo ntag >> " + marker + "; echo done").http_status == 200, "Add NTAG");
    ASSERT(!fs::exists(marker), "Nothing has run the NTAG command yet");
    rm::start_scan();
    rfid_sim::tap(NTAG);
    wait_for_scan();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    ASSERT(!fs::exists(marker), "A tap answering a scan doesn't run the command");
}

// ── run mode ───────────────────────────────────────────────────────────────

TEST(run_mode_runs_command) {
    std::string marker = g_dir + "/ran.txt";
    ASSERT(rm::set_run_enabled(true).body["run_enabled"] == true, "Run mode on");
    g_events.skip_all();
    rfid_sim::tap(NTAG);
    json e = g_events.wait(result_for(NTAG), "NTAG result");
    ASSERT(e["exit_code"] == 0 && e["output"] == "done\n", "Command output captured");
    ASSERT(read_file(marker) == "ntag\n", "Command ran exactly once for one tap");
}

TEST(run_mode_unknown_tag) {
    rfid_sim::tap(UNKNOWN);
    json e = g_events.wait([](const json& e) { return e["kind"] == "run" && e["id"] == UNKNOWN; }, "unknown tag");
    ASSERT(e["registered"] == false, "Unknown tag runs nothing");
}

TEST(edit_applies_without_restart) {
    ASSERT(rm::edit_tag(NTAG, "echo out; echo err >&2; exit 3").http_status == 200, "Edit");
    rfid_sim::tap(NTAG);
    json e = g_events.wait(result_for(NTAG), "edited command result");
    ASSERT(e["exit_code"] == 3, "Exit code reported");
    ASSERT(e["output"] == "out\nerr\n", "stderr captured too");
}

TEST(background_command_does_not_block) {
    rm::edit_tag(NTAG, "sleep 30 & echo bg");
    auto start = std::chrono::steady_clock::now();
    rfid_sim::tap(NTAG);
    json e = g_events.wait(result_for(NTAG), "background command result");
    auto took = std::chrono::steady_clock::now() - start;
    ASSERT(e["output"] == "bg\n", "Output of the foreground part");
    ASSERT(took < std::chrono::seconds(5), "Reader isn't held up by the background job");
}

// ── live scan ──────────────────────────────────────────────────────────────

TEST(live_scan_reports_reads) {
    rm::start_live();
    rfid_sim::tap(KNOWN);
    json e = g_events.wait([](const json& e) { return e["kind"] == "live" && e["id"] == KNOWN; }, "live read");
    ASSERT(e["registered"] == true && e["command"] == "echo hello" && e["type"] == "classic",
           "Live read shows the registration");
    ASSERT(e["detail"] == "{'success': True, 'id_integers': [220, 189, 106, 3], "
                          "'id_formatted': 'DC:BD:6A:03', 'type': 'classic'}",
           "Detail matches xerxes-rfid");
    g_events.wait(result_for(KNOWN), "run mode alongside live scan");
}

TEST(live_scan_without_run_mode) {
    rm::set_run_enabled(false);
    rfid_sim::tap(UNKNOWN);
    json e = g_events.wait([](const json& e) { return e["kind"] == "live" && e["id"] == UNKNOWN; }, "live read");
    ASSERT(e["registered"] == false, "Unregistered tag shown");
    rm::stop_live();
    ASSERT(rm::status().body["live_active"] == false, "Live scan stopped");
}

// ── delete ─────────────────────────────────────────────────────────────────

TEST(delete_tag) {
    ASSERT(rm::delete_tag("04:30:ad:00:64:1c:03").http_status == 200, "Delete, ID case-insensitive");
    ASSERT(rm::delete_tag(NTAG).http_status == 404, "Deleting again is 404");
    json tags = rm::list_tags().body["tags"];
    ASSERT(tags.size() == 1 && tags[0]["id"] == KNOWN, "One tag left");
}

// ── corrupt tag file ───────────────────────────────────────────────────────

TEST(corrupt_tag_file_never_overwritten) {
    std::string before = read_file(g_tags_path);
    const std::string corrupt = R"({"tags": [ {"id": "AA:BB:CC:DD", )";
    write_file(g_tags_path, corrupt);
    rm::Reply r = rm::list_tags();
    ASSERT(r.http_status == 500 && r.body["error"].get<std::string>().find("not valid JSON") != std::string::npos,
           "List reports the parse error");
    ASSERT(rm::add_tag(UNKNOWN, "x").http_status == 500, "Add refuses");
    ASSERT(read_file(g_tags_path) == corrupt, "File untouched");
    write_file(g_tags_path, before);
}

// ── settings ───────────────────────────────────────────────────────────────

TEST(settings_rejected) {
    ASSERT(rm::update_settings(1, 0x80, "").http_status == 400, "Address out of range");
    ASSERT(rm::update_settings(-1, 0x2C, "").http_status == 400, "Bad bus");
    ASSERT(rm::update_settings(1, 0x2C, "relative.json").http_status == 400, "Relative path");
    ASSERT(rm::update_settings(1, 0x2C, "/etc/passwd").http_status == 400, "Not a .json file");
}

TEST(wrong_address_reported) {
    ASSERT(rm::update_settings(1, 0x2D, "").http_status == 200, "Save wrong address");
    rm::Reply r = rm::probe();
    ASSERT(r.http_status == 502 && r.body["error"].get<std::string>().find("NACK") != std::string::npos,
           "Probe reports no module");
    ASSERT(rm::status().body["module"]["state"] == "error", "Module state is error");
}

TEST(other_tag_file) {
    std::string other = g_dir + "/other.json";
    ASSERT(rm::update_settings(1, 0x2C, other).http_status == 200, "Save another tag file");
    ASSERT(rm::probe().http_status == 200, "Module found again at 0x2C");
    rm::Reply r = rm::list_tags();
    ASSERT(r.body["path"] == other && r.body["tags"].empty(), "Tag list follows the tag file");
    rm::update_settings(1, 0x2C, "");
    ASSERT(rm::status().body["tags_path"] == g_tags_path, "Empty path means the default");
}

TEST(settings_saved) {
    rm::set_run_enabled(true);
    std::string conf = read_file(g_dir + "/rfid.conf");
    ASSERT(conf.find("run=1") != std::string::npos, "Run mode saved");
    ASSERT(conf.find("address=0x2C") != std::string::npos && conf.find("bus=1") != std::string::npos,
           "Bus and address saved");
    ASSERT(conf.find("tags=" + g_tags_path) != std::string::npos, "Tag file saved");
}

int main() {
    out << "======================================\n";
    out << "RFID Manager Tests\n";
    out << "======================================\n\n";

    char tmpl[] = "/tmp/xerxes-rfid-test-XXXXXX";
    if (!mkdtemp(tmpl)) {
        out << "could not create a temporary directory\n";
        return 1;
    }
    g_dir = tmpl;
    g_tags_path = g_dir + "/tags.json";
    setenv("XERXES_CONFIG_DIR", g_dir.c_str(), 1);

    rfid_sim::set_interactive();
    std::cout.rdbuf(&g_null);
    rm::initialize();

    RUN_TEST(tag_id_normalised);
    RUN_TEST(tag_id_rejected);
    RUN_TEST(command_validated);
    RUN_TEST(starts_idle_with_defaults);
    RUN_TEST(probe_finds_module);
    RUN_TEST(no_tag_file_means_no_tags);
    RUN_TEST(add_tag);
    RUN_TEST(add_existing_tag_conflicts);
    RUN_TEST(add_rejects_bad_input);
    RUN_TEST(edit_unregistered_tag_not_found);
    RUN_TEST(scan_new_tag);
    RUN_TEST(scan_registered_tag);
    RUN_TEST(scan_cancel);
    RUN_TEST(scan_never_runs_commands);
    RUN_TEST(run_mode_runs_command);
    RUN_TEST(run_mode_unknown_tag);
    RUN_TEST(edit_applies_without_restart);
    RUN_TEST(background_command_does_not_block);
    RUN_TEST(live_scan_reports_reads);
    RUN_TEST(live_scan_without_run_mode);
    RUN_TEST(delete_tag);
    RUN_TEST(corrupt_tag_file_never_overwritten);
    RUN_TEST(settings_rejected);
    RUN_TEST(wrong_address_reported);
    RUN_TEST(other_tag_file);
    RUN_TEST(settings_saved);

    out << "\n======================================\n";
    out << "Results: " << tests_passed << " passed, " << tests_failed << " failed\n";
    out << "======================================\n" << std::flush;

    fs::remove_all(g_dir);
    // The reader thread is still running; exit without destroying the state it uses.
    std::_Exit(tests_failed > 0 ? 1 : 0);
}
