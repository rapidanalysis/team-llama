#pragma once

#include <cstdint>
#include <string>

// Driver for a 128x64 SSD1306 OLED connected over I2C.
// Drawing goes into buffer first, display() sends it to the screen.
// The screen is split into 8 pages, each page is 8 pixel rows tall.
class SSD1306 {
public:
    static const int WIDTH = 128;
    static const int HEIGHT = 64;

    ~SSD1306();

    bool begin(const char* device, int address);
    void display();
    void off();

    // Draws the buffer in your local terminal, for testing without an oled
    void printToTerminal();

    void clear();
    void setPixel(int x, int y, bool on);
    void drawText(int x, int page, const std::string& text);

private:
    int fd = -1;
    uint8_t buffer[WIDTH * HEIGHT / 8] = {};

    void command(uint8_t cmd);
};
