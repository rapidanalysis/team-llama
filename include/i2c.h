// Minimal I2C register access, equivalent to the Linux path of PiicoDev_Unified.
#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace xerxes {

class I2CError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class I2CBus {
public:
    virtual ~I2CBus() = default;

    // Single write transaction: [reg, data...]
    virtual void write_mem(uint8_t addr, uint8_t reg, const uint8_t* data, size_t len) = 0;

    // Combined transaction: write [reg], repeated start, read `len` bytes.
    virtual void read_mem(uint8_t addr, uint8_t reg, uint8_t* out, size_t len) = 0;
};

// Opens /dev/i2c-<bus>. Throws I2CError on failure.
std::unique_ptr<I2CBus> open_i2c_bus(int bus);

}  // namespace xerxes
