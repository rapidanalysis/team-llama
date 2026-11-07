#pragma once
#include <string>
#include <vector>
#include "json.hpp"

// OLED display (FR-3 to FR-6): 128x64 SSD1306 on I2C, behind /api/oled/*.
// Replaces the standalone `xerxes_oled` program. The panel has no memory of its
// own, so a worker thread redraws it every few seconds while it is on.
// Settings are saved to $XERXES_CONFIG_DIR/oled.conf (default ~/.xerxes).
// Each call returns the JSON body and HTTP status for the route to send.
// Errors have a "code" (e.g. "no_device") that the panel maps to lang.json text,
// and an English "error" for logs and curl.

namespace oled_manager {

    struct Reply {
        int http_status = 200;
        nlohmann::json body;
    };

    // Call once at startup. panel_port is the admin panel's HTTP port, for the QR code.
    void initialize(int panel_port);

    Reply status();

    // Off blanks the screen. Saved, so it stays that way after a restart.
    Reply set_enabled(bool enabled);

    // mode is "stats" or "qr". stats are keys from status().available_stats;
    // "stats" mode needs at least one.
    Reply update_display(const std::string& mode, const std::vector<std::string>& stats);

    // bus 0-1023, address 0x03-0x77.
    Reply update_settings(int bus, int address);

    // Re-initialises the display once to check wiring and settings.
    Reply probe();

    // Renders a screen without touching the display, so the panel can show changes
    // before they are saved. An empty mode uses the saved settings.
    // Returns { width, height, rows: [hex per row, leftmost pixel = top bit] }.
    Reply preview(const std::string& mode, const std::vector<std::string>& stats);

    // Splits "ip,dns" into keys. Doesn't validate them.
    std::vector<std::string> split_stats(const std::string& csv);

}
