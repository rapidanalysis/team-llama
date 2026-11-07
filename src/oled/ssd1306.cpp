#include "ssd1306.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

#ifdef __linux__
#include <linux/i2c-dev.h>
#endif

// 5x7 font (ASCII 32-127)
static const uint8_t font[96][5] = {
    {0,0,0,0,0},                 // space
    {0,0,95,0,0},                // !
    {0,7,0,7,0},                 // "
    {20,127,20,127,20},          // #
    {36,42,127,42,18},           // $
    {35,19,8,100,98},            // %
    {54,73,85,34,80},            // &
    {0,5,3,0,0},                 // '
    {0,28,34,65,0},              // (
    {0,65,34,28,0},              // )
    {20,8,62,8,20},              // *
    {8,8,62,8,8},                // +
    {0,80,48,0,0},               // ,
    {8,8,8,8,8},                 // -
    {0,96,96,0,0},               // .
    {32,16,8,4,2},               // /

    {62,81,73,69,62},            // 0
    {0,66,127,64,0},             // 1
    {66,97,81,73,70},            // 2
    {33,65,69,75,49},            // 3
    {24,20,18,127,16},           // 4
    {39,69,69,69,57},            // 5
    {60,74,73,73,48},            // 6
    {1,113,9,5,3},               // 7
    {54,73,73,73,54},            // 8
    {6,73,73,41,30},             // 9

    {0,54,54,0,0},               // :
    {0,86,54,0,0},               // ;
    {8,20,34,65,0},              // <
    {20,20,20,20,20},            // =
    {0,65,34,20,8},              // >
    {2,1,81,9,6},                // ?
    {50,73,121,65,62},           // @

    {126,17,17,17,126},          // A
    {127,73,73,73,54},           // B
    {62,65,65,65,34},            // C
    {127,65,65,34,28},           // D
    {127,73,73,73,65},           // E
    {127,9,9,9,1},               // F
    {62,65,73,73,122},           // G
    {127,8,8,8,127},             // H
    {0,65,127,65,0},             // I
    {32,64,65,63,1},             // J
    {127,8,20,34,65},            // K
    {127,64,64,64,64},           // L
    {127,2,12,2,127},            // M
    {127,4,8,16,127},            // N
    {62,65,65,65,62},            // O
    {127,9,9,9,6},               // P
    {62,65,81,33,94},            // Q
    {127,9,25,41,70},            // R
    {70,73,73,73,49},            // S
    {1,1,127,1,1},               // T
    {63,64,64,64,63},            // U
    {31,32,64,32,31},            // V
    {63,64,56,64,63},            // W
    {99,20,8,20,99},             // X
    {7,8,112,8,7},               // Y
    {97,81,73,69,67},            // Z (90)

    {0,127,65,65,0},   // [
    {2,4,8,16,32},     // backslash
    {0,65,65,127,0},   // ]
    {4,2,1,2,4},       // ^
    {64,64,64,64,64},  // _
    {6,9,9,6,0},       // ` (drawn as a degree sign)

    {0, 32, 84, 84, 120},   // a (97)
    {127, 72, 68, 68, 56},  // b
    {56, 68, 68, 68, 32},   // c
    {56, 68, 68, 72, 127},  // d
    {56, 84, 84, 84, 24},   // e
    {8, 126, 9, 1, 2},      // f
    {12, 82, 82, 82, 62},   // g
    {127, 8, 4, 4, 120},    // h
    {0, 68, 125, 64, 0},    // i
    {32, 64, 68, 61, 0},    // j
    {127, 16, 40, 68, 0},   // k
    {0, 65, 127, 64, 0},    // l
    {124, 4, 24, 4, 120},   // m
    {124, 8, 4, 4, 120},    // n
    {56, 68, 68, 68, 56},   // o
    {126, 18, 18, 18, 12},  // p
    {12, 18, 18, 18, 126},  // q
    {124, 8, 4, 4, 8},      // r
    {72, 84, 84, 84, 32},   // s
    {4, 63, 68, 64, 32},    // t
    {60, 64, 64, 32, 124},  // u
    {28, 32, 64, 32, 28},   // v
    {60, 64, 48, 64, 60},   // w
    {68, 40, 16, 40, 68},   // x
    {12, 80, 80, 80, 60},   // y
    {68, 100, 84, 76, 68},  // z (122)
};

SSD1306::~SSD1306() {
    if (fd >= 0)
        close(fd);
}

bool SSD1306::begin(const char* device, int address) {
#ifdef __linux__
    fd = open(device, O_RDWR);
    if (fd < 0)
        return false;

    if (ioctl(fd, I2C_SLAVE, address) < 0) {
        close(fd);
        fd = -1;
        return false;
    }

    // Standard init sequence for 128x64 panels
    const uint8_t init[] = {
        0xAE,        // display off
        0x20, 0x00,  // horizontal addressing
        0x40,        // start line 0
        0xA1,        // segment remap
        0xC8,        // COM scan direction
        0x81, 0x7F,  // contrast
        0xA6,        // normal display
        0xA8, 0x3F,  // multiplex 64
        0xA4,        // show RAM
        0xD3, 0x00,  // no offset
        0xD5, 0x80,  // clock
        0xD9, 0xF1,  // pre-charge
        0xDA, 0x12,  // COM pins
        0xDB, 0x40,  // VCOMH
        0x8D, 0x14,  // charge pump on
        0xAF         // display on
    };
    for (uint8_t c : init)
        command(c);

    clear();
    display();
    return true;
#else
    (void)device;
    (void)address;
    return false;
#endif
}

void SSD1306::display() {
    if (fd < 0)
        return;

    for (int page = 0; page < 8; page++) {
        command(0xB0 + page);
        command(0x00);
        command(0x10);

        uint8_t data[1 + WIDTH];
        data[0] = 0x40;  // 0x40 = data follows
        memcpy(&data[1], &buffer[page * WIDTH], WIDTH);
        write(fd, data, sizeof(data));
    }
}

void SSD1306::off() {
    command(0xAE);
}

void SSD1306::printToTerminal() {
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            bool on = buffer[x + (y / 8) * WIDTH] & (1 << (y % 8));
            printf(on ? "#" : " ");
        }
        printf("\n");
    }
}

void SSD1306::clear() {
    memset(buffer, 0, sizeof(buffer));
}

void SSD1306::setPixel(int x, int y, bool on) {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT)
        return;

    if (on)
        buffer[x + (y / 8) * WIDTH] |= (1 << (y % 8));
    else
        buffer[x + (y / 8) * WIDTH] &= ~(1 << (y % 8));
}

// Each character is 5 columns plus 1 blank column
void SSD1306::drawText(int x, int page, const std::string& text) {
    for (char c : text) {
        if (c < 32 || c > 127)
            c = '?';
        for (int col = 0; col < 5; col++) {
            if (x + col < WIDTH && page < 8)
                buffer[page * WIDTH + x + col] = font[c - 32][col];
        }
        x += 6;
    }
}

void SSD1306::command(uint8_t cmd) {
    if (fd < 0)
        return;
    uint8_t buf[2] = {0x00, cmd};  // 0x00 = command follows
    write(fd, buf, 2);
}
