/**
 * test_rfid_driver.cpp - Tests for the PiicoDev RFID driver (src/rfid/rfid.cpp)
 *
 * Build and run:  make test
 * Reads tags from the simulated module and checks the results and every I2C
 * register access against those recorded from the original Python driver
 * (PiicoDev_RFID.py) reading the same tags.
 */

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../include/rfid.h"
#include "mfrc522_sim.h"

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
} while(0)

#define ASSERT(cond, msg) if (!(cond)) { \
    throw std::runtime_error(std::string(msg)); \
}

static const char* python_driver_trace();  // at the end of this file

// Returns the first line where the two texts differ, for a readable failure.
static std::string first_difference(const std::string& expected, const std::string& actual) {
    std::istringstream e(expected), a(actual);
    std::string el, al;
    for (int line = 1;; ++line) {
        bool he = static_cast<bool>(std::getline(e, el));
        bool ha = static_cast<bool>(std::getline(a, al));
        if (!he && !ha) return "texts differ only in line endings";
        if (!he || !ha || el != al)
            return "line " + std::to_string(line) + ": expected \"" + (he ? el : "<end>") +
                   "\", got \"" + (ha ? al : "<end>") + "\"";
    }
}

// Reads the same tags as the recording: a Mifare Classic, an NTAG, then another Classic.
static std::vector<std::string> g_results;
static std::string g_trace;

static void read_recorded_session() {
    using namespace rfid_sim;
    set_script({absent(2), present("DC:BD:6A:03"), absent(1), present("04:30:AD:00:64:1C:03"),
                present("11:22:33:44"), absent(3), end()});
    auto bus = xerxes::open_i2c_bus(1);
    xerxes::RfidReader reader(*bus);
    try {
        while (true) {
            xerxes::TagReading r = reader.read_tag();
            if (r.success) g_results.push_back(r.describe());
        }
    } catch (const xerxes::I2CError&) {
        // script finished
    }
    for (const std::string& line : trace()) g_trace += line + "\n";
}

TEST(reads_match_python_driver) {
    ASSERT(g_results.size() == 3, "Three tags read, got " + std::to_string(g_results.size()));
    ASSERT(g_results[0] == "{'success': True, 'id_integers': [220, 189, 106, 3], 'id_formatted': 'DC:BD:6A:03', 'type': 'classic'}",
           "Mifare Classic read like the Python driver");
    ASSERT(g_results[1] == "{'success': True, 'id_integers': [4, 48, 173, 0, 100, 28, 3], 'id_formatted': '04:30:AD:00:64:1C:03', 'type': 'ntag'}",
           "7-byte NTAG read like the Python driver");
    ASSERT(g_results[2] == "{'success': True, 'id_integers': [17, 34, 51, 68], 'id_formatted': '11:22:33:44', 'type': 'classic'}",
           "Second Mifare Classic read like the Python driver");
}

TEST(register_trace_matches_python_driver) {
    std::string expected = python_driver_trace();
    ASSERT(g_trace == expected, "I2C trace: " + first_difference(expected, g_trace));
}

TEST(no_tag_reads_as_failure) {
    rfid_sim::set_script({rfid_sim::absent(5), rfid_sim::end()});
    auto bus = xerxes::open_i2c_bus(1);
    xerxes::RfidReader reader(*bus);
    xerxes::TagReading r = reader.read_tag();
    ASSERT(!r.success && r.id.empty() && r.type.empty(), "Empty reader gives an unsuccessful read");
    ASSERT(r.describe() == "{'success': False, 'id_integers': [0], 'id_formatted': '', 'type': ''}",
           "Failed read is described like the Python driver");
}

TEST(wrong_address_throws) {
    rfid_sim::set_interactive();
    auto bus = xerxes::open_i2c_bus(1);
    bool threw = false;
    try {
        xerxes::RfidReader reader(*bus, 0x2D);
    } catch (const xerxes::I2CError&) {
        threw = true;
    }
    ASSERT(threw, "No module at the address reports an I2C error");
}

int main() {
    std::cout << "======================================\n";
    std::cout << "RFID Driver Tests\n";
    std::cout << "======================================\n\n";

    read_recorded_session();
    RUN_TEST(reads_match_python_driver);
    RUN_TEST(register_trace_matches_python_driver);
    RUN_TEST(no_tag_reads_as_failure);
    RUN_TEST(wrong_address_throws);

    std::cout << "\n======================================\n";
    std::cout << "Results: " << tests_passed << " passed, " << tests_failed << " failed\n";
    std::cout << "======================================\n";
    return tests_failed > 0 ? 1 : 0;
}

// Every I2C register access the Python driver made reading the three tags above.
// "W rr dd..." writes bytes dd to register rr; "R rr -> dd" reads dd from it.
static const char* python_driver_trace() {
    return R"TRACE(W 01 0F
W 2A 80
W 2B A9
W 2C 03
W 2D E8
W 15 40
W 11 3D
W 03 80
W 02 20
R 14 -> 00
R 14 -> 00
W 14 83
W 0D 07
W 01 00
W 04 7F
R 0A -> 00
W 0A 80
W 09 26
R 0D -> 07
W 0D 07
W 01 0C
R 0D -> 07
W 0D 87
R 04 -> 01
R 0D -> 07
W 0D 07
R 06 -> 00
W 0D 07
W 01 00
W 04 7F
R 0A -> 00
W 0A 80
W 09 26
R 0D -> 07
W 0D 07
W 01 0C
R 0D -> 07
W 0D 87
R 04 -> 01
R 0D -> 07
W 0D 07
R 06 -> 00
W 0D 07
W 01 00
W 04 7F
R 0A -> 00
W 0A 80
W 09 26
R 0D -> 07
W 0D 07
W 01 0C
R 0D -> 07
W 0D 87
R 04 -> 30
R 0D -> 07
W 0D 07
R 06 -> 00
R 0A -> 02
R 0C -> 00
R 09 -> 04
R 09 -> 00
W 0D 00
W 01 00
W 04 7F
R 0A -> 00
W 0A 80
W 09 93 20
R 0D -> 00
W 0D 00
W 01 0C
R 0D -> 00
W 0D 80
R 04 -> 30
R 0D -> 00
W 0D 00
R 06 -> 00
R 0A -> 05
R 0C -> 00
R 09 -> DC
R 09 -> BD
R 09 -> 6A
R 09 -> 03
R 09 -> 08
W 01 00
R 05 -> 00
W 05 00
R 0A -> 00
W 0A 80
W 09 93
W 09 70
W 09 DC
W 09 BD
W 09 6A
W 09 03
W 09 08
W 01 03
R 05 -> 04
W 01 00
R 22 -> 9F
R 21 -> 6B
W 01 00
W 04 7F
R 0A -> 07
W 0A 87
W 09 93 70 DC BD 6A 03 08 9F 6B
R 0D -> 00
W 0D 00
W 01 0C
R 0D -> 00
W 0D 80
R 04 -> 30
R 0D -> 00
W 0D 00
R 06 -> 00
R 0A -> 03
R 0C -> 00
R 09 -> 08
R 09 -> B6
R 09 -> DD
W 0D 07
W 01 00
W 04 7F
R 0A -> 00
W 0A 80
W 09 26
R 0D -> 07
W 0D 07
W 01 0C
R 0D -> 07
W 0D 87
R 04 -> 01
R 0D -> 07
W 0D 07
R 06 -> 00
W 0D 07
W 01 00
W 04 7F
R 0A -> 00
W 0A 80
W 09 26
R 0D -> 07
W 0D 07
W 01 0C
R 0D -> 07
W 0D 87
R 04 -> 30
R 0D -> 07
W 0D 07
R 06 -> 00
R 0A -> 02
R 0C -> 00
R 09 -> 44
R 09 -> 00
W 0D 00
W 01 00
W 04 7F
R 0A -> 00
W 0A 80
W 09 93 20
R 0D -> 00
W 0D 00
W 01 0C
R 0D -> 00
W 0D 80
R 04 -> 30
R 0D -> 00
W 0D 00
R 06 -> 00
R 0A -> 05
R 0C -> 00
R 09 -> 88
R 09 -> 04
R 09 -> 30
R 09 -> AD
R 09 -> 11
W 01 00
R 05 -> 04
W 05 00
R 0A -> 00
W 0A 80
W 09 93
W 09 70
W 09 88
W 09 04
W 09 30
W 09 AD
W 09 11
W 01 03
R 05 -> 04
W 01 00
R 22 -> 24
R 21 -> E3
W 01 00
W 04 7F
R 0A -> 07
W 0A 87
W 09 93 70 88 04 30 AD 11 24 E3
R 0D -> 00
W 0D 00
W 01 0C
R 0D -> 00
W 0D 80
R 04 -> 30
R 0D -> 00
W 0D 00
R 06 -> 00
R 0A -> 03
R 0C -> 00
R 09 -> 04
R 09 -> DA
R 09 -> 17
W 0D 00
W 01 00
W 04 7F
R 0A -> 00
W 0A 80
W 09 95 20
R 0D -> 00
W 0D 00
W 01 0C
R 0D -> 00
W 0D 80
R 04 -> 30
R 0D -> 00
W 0D 00
R 06 -> 00
R 0A -> 05
R 0C -> 00
R 09 -> 00
R 09 -> 64
R 09 -> 1C
R 09 -> 03
R 09 -> 7B
W 01 00
R 05 -> 04
W 05 00
R 0A -> 00
W 0A 80
W 09 95
W 09 70
W 09 00
W 09 64
W 09 1C
W 09 03
W 09 7B
W 01 03
R 05 -> 04
W 01 00
R 22 -> 53
R 21 -> AD
W 01 00
W 04 7F
R 0A -> 07
W 0A 87
W 09 95 70 00 64 1C 03 7B 53 AD
R 0D -> 00
W 0D 00
W 01 0C
R 0D -> 00
W 0D 80
R 04 -> 30
R 0D -> 00
W 0D 00
R 06 -> 00
R 0A -> 03
R 0C -> 00
R 09 -> 00
R 09 -> FE
R 09 -> 51
W 0D 07
W 01 00
W 04 7F
R 0A -> 00
W 0A 80
W 09 26
R 0D -> 07
W 0D 07
W 01 0C
R 0D -> 07
W 0D 87
R 04 -> 30
R 0D -> 07
W 0D 07
R 06 -> 00
R 0A -> 02
R 0C -> 00
R 09 -> 04
R 09 -> 00
W 0D 00
W 01 00
W 04 7F
R 0A -> 00
W 0A 80
W 09 93 20
R 0D -> 00
W 0D 00
W 01 0C
R 0D -> 00
W 0D 80
R 04 -> 30
R 0D -> 00
W 0D 00
R 06 -> 00
R 0A -> 05
R 0C -> 00
R 09 -> 11
R 09 -> 22
R 09 -> 33
R 09 -> 44
R 09 -> 44
W 01 00
R 05 -> 04
W 05 00
R 0A -> 00
W 0A 80
W 09 93
W 09 70
W 09 11
W 09 22
W 09 33
W 09 44
W 09 44
W 01 03
R 05 -> 04
W 01 00
R 22 -> 51
R 21 -> 9C
W 01 00
W 04 7F
R 0A -> 07
W 0A 87
W 09 93 70 11 22 33 44 44 51 9C
R 0D -> 00
W 0D 00
W 01 0C
R 0D -> 00
W 0D 80
R 04 -> 30
R 0D -> 00
W 0D 00
R 06 -> 00
R 0A -> 03
R 0C -> 00
R 09 -> 08
R 09 -> B6
R 09 -> DD
W 0D 07
W 01 00
W 04 7F
R 0A -> 00
W 0A 80
W 09 26
R 0D -> 07
W 0D 07
W 01 0C
R 0D -> 07
W 0D 87
R 04 -> 01
R 0D -> 07
W 0D 07
R 06 -> 00
W 0D 07
W 01 00
W 04 7F
R 0A -> 00
W 0A 80
W 09 26
R 0D -> 07
W 0D 07
W 01 0C
R 0D -> 07
W 0D 87
R 04 -> 01
R 0D -> 07
W 0D 07
R 06 -> 00
W 0D 07
W 01 00
W 04 7F
R 0A -> 00
W 0A 80
W 09 26
R 0D -> 07
W 0D 07
W 01 0C
R 0D -> 07
W 0D 87
R 04 -> 01
R 0D -> 07
W 0D 07
R 06 -> 00
W 0D 07
W 01 00
W 04 7F
R 0A -> 00
W 0A 80
W 09 26
R 0D -> 07
W 0D 07
W 01 0C
R 0D -> 07
W 0D 87
)TRACE";
}
