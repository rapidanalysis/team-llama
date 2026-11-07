// Xerxes Pi OLED display
//
// ./oled                 show system info
// ./oled qr [text]       show a QR code (default text is http://<ip>)
// add --preview to print the screen in the terminal instead

#include "qrcodegen.hpp"
#include "ssd1306.h"
#include "sysinfo.h"

#include <unistd.h>

#include <csignal>
#include <cstdio>
#include <string>

using qrcodegen::QrCode;

volatile std::sig_atomic_t running = 1;

void handleSignal(int) {
    running = 0;
}

void drawInfo(SSD1306& oled) {
    oled.drawText(0, 0, getHostname());
    oled.drawText(0, 2, "IP  " + getIP());
    oled.drawText(0, 3, "DNS " + getDNS());
    oled.drawText(0, 5, "CPU " + getCpuTemp());
}

void drawQR(SSD1306& oled, std::string text) {
    if (text.empty())
        text = "http://" + getIP();

    QrCode qr = QrCode::encodeText(text.c_str(), QrCode::Ecc::LOW);
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

int main(int argc, char** argv) {
    bool qrMode = false;
    bool preview = false;
    std::string qrText;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--preview")
            preview = true;
        else if (arg == "qr")
            qrMode = true;
        else
            qrText = arg;
    }

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    SSD1306 oled;
    if (!preview && !oled.begin("/dev/i2c-1", 0x3C)) {
        fprintf(stderr, "Failed to initialise display\n");
        return 1;
    }

    // Startup screen
    oled.clear();
    oled.drawText(37, 3, "Xerxes Pi");
    oled.drawText(31, 5, "starting...");
    preview ? oled.printToTerminal() : oled.display();
    sleep(2);

    while (running) {
        oled.clear();
        if (qrMode)
            drawQR(oled, qrText);
        else
            drawInfo(oled);

        if (preview) {
            printf("\033[H\033[2J");  // clear the terminal
            oled.printToTerminal();
        } else {
            oled.display();
        }
        sleep(2);
    }

    // Clear the screen on Ctrl+C
    oled.clear();
    oled.display();
    oled.off();
    return 0;
}
