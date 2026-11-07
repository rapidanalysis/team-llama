#pragma once

#include "ssd1306.h"

#include <string>
#include <vector>

// What the OLED shows. Used for both the real screen and the admin panel
// preview, so the preview always matches the display.

// Stat keys the admin panel can pick from, in the order they are drawn
const std::vector<std::string>& availableStats();
bool isStat(const std::string& key);

// Draws one stat per line (hostname has no label)
void drawStats(SSD1306& oled, const std::vector<std::string>& stats);

// Draws a QR code for `url` on the left and a hint on the right.
// An empty url means there is no network yet.
void drawQR(SSD1306& oled, const std::string& url);
