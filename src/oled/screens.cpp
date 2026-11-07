#include "screens.h"

#include "qrcodegen.hpp"
#include "sysinfo.h"

using qrcodegen::QrCode;

const std::vector<std::string>& availableStats() {
    static const std::vector<std::string> stats = {
        "hostname", "ip", "dns", "cpu_temp", "uptime", "memory", "disk"};
    return stats;
}

bool isStat(const std::string& key) {
    for (const std::string& s : availableStats())
        if (s == key)
            return true;
    return false;
}

// Labels are 4 characters plus a space, so values line up and an IPv4
// address still fits in the 21 characters of a line
static std::string statLine(const std::string& key) {
    if (key == "hostname") return getHostname();
    if (key == "ip")       return "IP   " + getIP();
    if (key == "dns")      return "DNS  " + getDNS();
    if (key == "cpu_temp") return "CPU  " + getCpuTemp();
    if (key == "uptime")   return "UP   " + getUptime();
    if (key == "memory")   return "MEM  " + getMemory();
    if (key == "disk")     return "DISK " + getDisk();
    return "";
}

void drawStats(SSD1306& oled, const std::vector<std::string>& stats) {
    // Leave a blank line between stats when there's room for it
    int gap = stats.size() <= 4 ? 2 : 1;
    int page = 0;
    for (const std::string& key : stats) {
        if (page >= 8)
            break;
        oled.drawText(0, page, statLine(key));
        page += gap;
    }
}

void drawQR(SSD1306& oled, const std::string& url) {
    if (url.empty()) {
        oled.drawText(0, 2, "No network");
        oled.drawText(0, 4, "Connect a cable to");
        oled.drawText(0, 5, "show the QR code");
        return;
    }

    QrCode qr = QrCode::encodeText(url.c_str(), QrCode::Ecc::LOW);
    int size = qr.getSize();
    int scale = 64 / (size + 2);
    if (scale == 0) {
        oled.drawText(0, 0, "QR text too long");
        return;
    }

    // Scanners need dark squares on a light background, so light up the
    // left 64x64 area and turn off the pixels for the dark squares
    int offset = (64 - size * scale) / 2;
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++)
            oled.setPixel(x, y, true);

    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            if (qr.getModule(x, y)) {
                for (int dy = 0; dy < scale; dy++)
                    for (int dx = 0; dx < scale; dx++)
                        oled.setPixel(offset + x * scale + dx, offset + y * scale + dy, false);
            }
        }
    }

    oled.drawText(70, 2, "Scan to");
    oled.drawText(70, 3, "open the");
    oled.drawText(70, 4, "admin");
    oled.drawText(70, 5, "panel");
}
