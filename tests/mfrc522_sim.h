/**
 * mfrc522_sim.h - Simulated PiicoDev RFID module (NXP MFRC522) for the RFID tests
 *
 * Linking tests/mfrc522_sim.cpp provides xerxes::open_i2c_bus(), so the RFID
 * code under test talks to this simulator instead of /dev/i2c-N.
 *
 * Tags are presented either from a fixed script (set_script) or one tap at a
 * time (tap). A tapped tag stays on the reader until its UID has been read once.
 */
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace rfid_sim {

    enum class StepKind { Absent, Present, End };

    struct Step {
        StepKind kind;
        int count;        // Absent: number of wake-up requests with no tag
        std::string uid;  // Present: e.g. "DC:BD:6A:03"
    };

    inline Step absent(int n) { return {StepKind::Absent, n, ""}; }
    inline Step present(const std::string& uid) { return {StepKind::Present, 0, uid}; }
    inline Step end() { return {StepKind::End, 0, ""}; }

    // Scripted mode: once the script runs out, every bus access fails with I2CError.
    void set_script(const std::vector<Step>& steps);

    // Interactive mode: no tag present until tap() is called.
    void set_interactive();
    void tap(const std::string& uid);

    // Every register access, e.g. "W 01 0F" or "R 04 -> 30".
    std::vector<std::string> trace();

}
