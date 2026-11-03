#pragma once
#include <cstdint>
#include <string>
#include "json.hpp"

// RFID reader (FR-12): PiicoDev RFID module on I2C, behind /api/rfid/*.
// Replaces the standalone `xerxes-rfid` program and keeps its tags.json format.
// Settings are saved to $XERXES_CONFIG_DIR/rfid.conf (default ~/.xerxes).
// Each call returns the JSON body and HTTP status for the route to send.

namespace rfid_manager {

    struct Reply {
        int http_status = 200;
        nlohmann::json body;
    };

    // Call once at startup. Resumes run mode if it was left on.
    void initialize();

    Reply status();

    // Run mode: tapping a registered tag runs its command (as the backend user).
    Reply set_run_enabled(bool enabled);

    // bus 0-1023, address 0x03-0x77, absolute tags_path ending in .json.
    Reply update_settings(int bus, int address, const std::string& tags_path);

    // Initialises the module once to check wiring and settings.
    Reply probe();

    // IDs are case-insensitive.
    Reply list_tags();
    Reply add_tag(const std::string& id, const std::string& command);   // 409 if already registered
    Reply edit_tag(const std::string& id, const std::string& command);  // 404 if not registered
    Reply delete_tag(const std::string& id);

    // Waits for one tap so the UI can register it. Poll scan_state().
    Reply start_scan();
    Reply scan_state();
    Reply cancel_scan();

    // Stops by itself if nobody polls events() for a while.
    Reply start_live();
    Reply stop_live();

    // Events newer than `since`. Returns { events: [...], next: N }.
    Reply events(uint64_t since);

    // Return "" when valid, otherwise a message for the user.
    std::string normalise_tag_id(const std::string& raw, std::string* id_out);
    std::string validate_command(const std::string& command);

}
