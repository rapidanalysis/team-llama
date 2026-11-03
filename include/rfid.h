// Driver for the Core Electronics PiicoDev RFID module (NXP MFRC522 over I2C).
// Port of the read-ID subset of PiicoDev_RFID.py, which is what Xerxes uses.
#pragma once

#include "i2c.h"

#include <cstdint>
#include <string>
#include <vector>

namespace xerxes {

struct TagReading {
    bool success = false;
    std::vector<uint8_t> id;  // UID bytes (4 for Mifare Classic, 7 for NTAG)
    std::string id_formatted; // e.g. "DC:BD:6A:03"
    std::string type;         // "classic", "ntag", or "" on failure

    // Same text Python prints for readID(detail=True), e.g.
    // {'success': True, 'id_integers': [220, 189, 106, 3], 'id_formatted': 'DC:BD:6A:03', 'type': 'classic'}
    std::string describe() const;
};

class RfidReader {
public:
    static constexpr uint8_t kDefaultAddress = 0x2C;

    RfidReader(I2CBus& bus, uint8_t address = kDefaultAddress);

    // Wakes a tag in the field and reads its UID. Equivalent of readTagID().
    TagReading read_tag();

    void reset();
    void antenna_on();

private:
    enum Status { OK = 1, NOTAGERR = 2, ERR = 3 };

    struct Transfer {
        Status status = ERR;
        std::vector<uint8_t> data;
        unsigned bits = 0;
    };

    void wreg(uint8_t reg, uint8_t val);
    void wfifo(uint8_t reg, const std::vector<uint8_t>& data);
    uint8_t rreg(uint8_t reg);
    void sflags(uint8_t reg, uint8_t mask);
    void cflags(uint8_t reg, uint8_t mask);

    Transfer to_card(uint8_t cmd, const std::vector<uint8_t>& send);
    std::vector<uint8_t> crc(const std::vector<uint8_t>& data);
    Status request(uint8_t mode);
    Status anticoll(uint8_t level, std::vector<uint8_t>& uid);
    bool select_tag(const std::vector<uint8_t>& uid, uint8_t level);
    TagReading read_uid();
    bool detect_tag();

    I2CBus& bus_;
    uint8_t address_;
};

}  // namespace xerxes
