// Port of PiicoDev_RFID.py (Core Electronics), itself derived from
// semaf/MFRC522_I2C_Library, wendlers/micropython-mfrc522 and mxgxw/MFRC522-python.
// The register sequence is kept identical to the Python driver so that behaviour
// on the PiicoDev hardware matches what was tested.
#include "rfid.h"

#include "util.h"

#include <cstdio>

namespace xerxes {
namespace {

constexpr uint8_t REG_COMMAND = 0x01;
constexpr uint8_t REG_COM_I_EN = 0x02;
constexpr uint8_t REG_DIV_I_EN = 0x03;
constexpr uint8_t REG_COM_IRQ = 0x04;
constexpr uint8_t REG_DIV_IRQ = 0x05;
constexpr uint8_t REG_ERROR = 0x06;
constexpr uint8_t REG_FIFO_DATA = 0x09;
constexpr uint8_t REG_FIFO_LEVEL = 0x0A;
constexpr uint8_t REG_CONTROL = 0x0C;
constexpr uint8_t REG_BIT_FRAMING = 0x0D;
constexpr uint8_t REG_MODE = 0x11;
constexpr uint8_t REG_TX_CONTROL = 0x14;
constexpr uint8_t REG_TX_ASK = 0x15;
constexpr uint8_t REG_CRC_RESULT_MSB = 0x21;
constexpr uint8_t REG_CRC_RESULT_LSB = 0x22;
constexpr uint8_t REG_T_MODE = 0x2A;
constexpr uint8_t REG_T_PRESCALER = 0x2B;
constexpr uint8_t REG_T_RELOAD_HI = 0x2C;
constexpr uint8_t REG_T_RELOAD_LO = 0x2D;

constexpr uint8_t CMD_IDLE = 0x00;
constexpr uint8_t CMD_CALC_CRC = 0x03;
constexpr uint8_t CMD_TRANSCEIVE = 0x0C;
constexpr uint8_t CMD_MF_AUTHENT = 0x0E;
constexpr uint8_t CMD_SOFT_RESET = 0x0F;

constexpr uint8_t TAG_CMD_REQIDL = 0x26;
constexpr uint8_t TAG_CMD_ANTCOL1 = 0x93;
constexpr uint8_t TAG_CMD_ANTCOL2 = 0x95;
constexpr uint8_t TAG_CMD_ANTCOL3 = 0x97;

constexpr uint8_t CASCADE_TAG = 0x88;

}  // namespace

std::string TagReading::describe() const {
    std::string s = "{'success': ";
    s += success ? "True" : "False";
    s += ", 'id_integers': [";
    if (success) {
        for (size_t i = 0; i < id.size(); ++i) {
            if (i) s += ", ";
            s += std::to_string(id[i]);
        }
    } else {
        s += "0";  // the Python driver reports [0] for a failed read
    }
    s += "], 'id_formatted': '" + id_formatted + "', 'type': '" + type + "'}";
    return s;
}

RfidReader::RfidReader(I2CBus& bus, uint8_t address) : bus_(bus), address_(address) {
    reset();
    sleep_ms(50);
    wreg(REG_T_MODE, 0x80);
    wreg(REG_T_PRESCALER, 0xA9);
    wreg(REG_T_RELOAD_HI, 0x03);
    wreg(REG_T_RELOAD_LO, 0xE8);
    wreg(REG_TX_ASK, 0x40);
    wreg(REG_MODE, 0x3D);
    wreg(REG_DIV_I_EN, 0x80);  // CMOS logic for IRQ pin
    wreg(REG_COM_I_EN, 0x20);  // propagate RxIRq to the IRQ pin
    antenna_on();
}

void RfidReader::wreg(uint8_t reg, uint8_t val) { bus_.write_mem(address_, reg, &val, 1); }

void RfidReader::wfifo(uint8_t reg, const std::vector<uint8_t>& data) {
    bus_.write_mem(address_, reg, data.data(), data.size());
}

uint8_t RfidReader::rreg(uint8_t reg) {
    uint8_t v = 0;
    bus_.read_mem(address_, reg, &v, 1);
    return v;
}

void RfidReader::sflags(uint8_t reg, uint8_t mask) { wreg(reg, rreg(reg) | mask); }

void RfidReader::cflags(uint8_t reg, uint8_t mask) { wreg(reg, rreg(reg) & static_cast<uint8_t>(~mask)); }

void RfidReader::reset() { wreg(REG_COMMAND, CMD_SOFT_RESET); }

void RfidReader::antenna_on() {
    // The Python driver tests `~(TxControl & 0x03)`, which is always truthy, so
    // the antenna bits are always (re)set. Keep the same register traffic.
    (void)rreg(REG_TX_CONTROL);
    sflags(REG_TX_CONTROL, 0x83);
}

RfidReader::Transfer RfidReader::to_card(uint8_t cmd, const std::vector<uint8_t>& send) {
    Transfer result;
    uint8_t irq_en = 0, wait_irq = 0;
    if (cmd == CMD_MF_AUTHENT) {
        irq_en = 0x12;
        wait_irq = 0x10;
    } else if (cmd == CMD_TRANSCEIVE) {
        irq_en = 0x77;
        wait_irq = 0x30;
    }

    wreg(REG_COMMAND, CMD_IDLE);        // stop any active command
    wreg(REG_COM_IRQ, 0x7F);            // clear all interrupt request bits
    sflags(REG_FIFO_LEVEL, 0x80);       // flush FIFO
    wfifo(REG_FIFO_DATA, send);         // load FIFO
    if (cmd == CMD_TRANSCEIVE) sflags(REG_BIT_FRAMING, 0x00);
    wreg(REG_COMMAND, cmd);
    if (cmd == CMD_TRANSCEIVE) sflags(REG_BIT_FRAMING, 0x80);  // StartSend

    int i = 20000;
    uint8_t n = 0;
    while (true) {
        n = rreg(REG_COM_IRQ);
        --i;
        if (n & wait_irq) break;
        if (n & 0x01) break;  // timer expired
        if (i == 0) break;
    }
    cflags(REG_BIT_FRAMING, 0x80);

    if (i) {
        if ((rreg(REG_ERROR) & 0x1B) == 0x00) {
            result.status = OK;
            if (n & irq_en & 0x01) {
                result.status = NOTAGERR;
            } else if (cmd == CMD_TRANSCEIVE) {
                int level = rreg(REG_FIFO_LEVEL);
                int last_bits = rreg(REG_CONTROL) & 0x07;
                int bits = last_bits != 0 ? (level - 1) * 8 + last_bits : level * 8;
                result.bits = bits < 0 ? 0 : static_cast<unsigned>(bits);
                if (level == 0)
                    level = 1;
                else if (level > 16)
                    level = 16;
                for (int k = 0; k < level; ++k) result.data.push_back(rreg(REG_FIFO_DATA));
            }
        } else {
            result.status = ERR;
        }
    }
    return result;
}

std::vector<uint8_t> RfidReader::crc(const std::vector<uint8_t>& data) {
    wreg(REG_COMMAND, CMD_IDLE);
    cflags(REG_DIV_IRQ, 0x04);
    sflags(REG_FIFO_LEVEL, 0x80);
    for (uint8_t c : data) wreg(REG_FIFO_DATA, c);
    wreg(REG_COMMAND, CMD_CALC_CRC);

    int i = 0xFF;
    while (true) {
        uint8_t n = rreg(REG_DIV_IRQ);
        --i;
        if (i == 0 || (n & 0x04)) break;
    }
    wreg(REG_COMMAND, CMD_IDLE);
    uint8_t lsb = rreg(REG_CRC_RESULT_LSB);
    uint8_t msb = rreg(REG_CRC_RESULT_MSB);
    return {lsb, msb};
}

RfidReader::Status RfidReader::request(uint8_t mode) {
    wreg(REG_BIT_FRAMING, 0x07);
    Transfer t = to_card(CMD_TRANSCEIVE, {mode});
    if (t.status != OK || t.bits != 0x10) return ERR;
    return OK;
}

RfidReader::Status RfidReader::anticoll(uint8_t level, std::vector<uint8_t>& uid) {
    wreg(REG_BIT_FRAMING, 0x00);
    Transfer t = to_card(CMD_TRANSCEIVE, {level, 0x20});
    uid = t.data;
    if (t.status != OK) return t.status;
    if (t.data.size() != 5) return ERR;
    uint8_t check = 0;
    for (int k = 0; k < 4; ++k) check ^= t.data[k];
    return check == t.data[4] ? OK : ERR;
}

bool RfidReader::select_tag(const std::vector<uint8_t>& uid, uint8_t level) {
    std::vector<uint8_t> buf{level, 0x70};
    buf.insert(buf.end(), uid.begin(), uid.end());
    std::vector<uint8_t> c = crc(buf);
    buf.push_back(c[0]);
    buf.push_back(c[1]);
    Transfer t = to_card(CMD_TRANSCEIVE, buf);
    return t.status == OK && t.bits == 0x18;
}

TagReading RfidReader::read_uid() {
    TagReading fail;
    std::vector<uint8_t> valid;
    std::vector<uint8_t> uid;

    if (anticoll(TAG_CMD_ANTCOL1, uid) != OK) return fail;
    if (!select_tag(uid, TAG_CMD_ANTCOL1)) return fail;

    if (uid[0] == CASCADE_TAG) {  // 7- or 10-byte UID (e.g. NTAG)
        valid.insert(valid.end(), uid.begin() + 1, uid.begin() + 4);
        if (anticoll(TAG_CMD_ANTCOL2, uid) != OK) return fail;
        if (!select_tag(uid, TAG_CMD_ANTCOL2)) return fail;
        if (uid[0] == CASCADE_TAG) {
            valid.insert(valid.end(), uid.begin() + 1, uid.begin() + 4);
            if (anticoll(TAG_CMD_ANTCOL3, uid) != OK) return fail;
        }
    }
    valid.insert(valid.end(), uid.begin(), uid.begin() + 5);

    TagReading r;
    r.success = true;
    r.id.assign(valid.begin(), valid.end() - 1);  // drop the trailing BCC
    for (size_t k = 0; k < r.id.size(); ++k) {
        char hex[4];
        std::snprintf(hex, sizeof hex, "%02X", r.id[k]);
        if (k) r.id_formatted += ':';
        r.id_formatted += hex;
    }
    r.type = r.id.size() == 4 ? "classic" : "ntag";
    return r;
}

bool RfidReader::detect_tag() { return request(TAG_CMD_REQIDL) == OK; }

TagReading RfidReader::read_tag() {
    bool present = detect_tag();
    if (!present) present = detect_tag();  // tag may not have been in IDLE state yet
    if (present) {
        TagReading r = read_uid();
        if (r.success) return r;
    }
    return TagReading{};
}

}  // namespace xerxes
