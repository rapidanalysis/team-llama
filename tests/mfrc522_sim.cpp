// C++ port of the MFRC522 + ISO 14443-A tag simulator written for the xerxes-rfid
// rewrite. Register behaviour must stay identical: test_rfid_driver compares the
// I2C trace against one recorded from the original Python driver.
#include "mfrc522_sim.h"

#include "i2c.h"

#include <cstdio>
#include <deque>
#include <memory>
#include <mutex>

namespace rfid_sim {
namespace {

constexpr uint8_t kAddress = 0x2C;

constexpr uint8_t REG_COMMAND = 0x01, REG_COM_IRQ = 0x04, REG_DIV_IRQ = 0x05, REG_ERROR = 0x06;
constexpr uint8_t REG_FIFO_DATA = 0x09, REG_FIFO_LEVEL = 0x0A, REG_CONTROL = 0x0C, REG_BIT_FRAMING = 0x0D;
constexpr uint8_t REG_CRC_MSB = 0x21, REG_CRC_LSB = 0x22;
constexpr uint8_t CMD_IDLE = 0x00, CMD_CALC_CRC = 0x03, CMD_TRANSCEIVE = 0x0C, CMD_SOFT_RESET = 0x0F;

using Bytes = std::vector<uint8_t>;

struct SessionEnd {};

Bytes crc_a(const Bytes& data) {
    unsigned crc = 0x6363;
    for (uint8_t in : data) {
        unsigned b = in ^ (crc & 0xFF);
        b = (b ^ (b << 4)) & 0xFF;
        crc = ((crc >> 8) ^ (b << 8) ^ (b << 3) ^ (b >> 4)) & 0xFFFF;
    }
    return {static_cast<uint8_t>(crc & 0xFF), static_cast<uint8_t>(crc >> 8)};
}

Bytes parse_uid(const std::string& s) {
    Bytes out;
    for (size_t i = 0; i + 1 < s.size(); i += 3)
        out.push_back(static_cast<uint8_t>(std::stoi(s.substr(i, 2), nullptr, 16)));
    return out;
}

std::string hex_list(const Bytes& b) {
    std::string out;
    char buf[4];
    for (size_t i = 0; i < b.size(); ++i) {
        std::snprintf(buf, sizeof buf, "%02X", b[i]);
        if (i) out += ' ';
        out += buf;
    }
    return out;
}

class Tag {
public:
    explicit Tag(const Bytes& uid) : uid_(uid) {
        if (uid.size() == 4) {
            levels_ = {uid};
        } else {  // 7 bytes: cascade tag 0x88 + 3 bytes, then the last 4
            levels_ = {{0x88, uid[0], uid[1], uid[2]}, {uid[3], uid[4], uid[5], uid[6]}};
        }
    }

    int completed_reads = 0;

    // Returns the tag's answer, or an empty vector for no answer.
    Bytes handle(const Bytes& frame, int last_bits) {
        static const uint8_t sel[] = {0x93, 0x95, 0x97};
        if (frame == Bytes{0x26} && last_bits == 7) {  // REQA
            if (state_ == IDLE) {
                state_ = READY;
                level_ = 0;
                return {static_cast<uint8_t>(uid_.size() == 7 ? 0x44 : 0x04), 0x00};
            }
            return to_idle();
        }
        if (state_ != READY || frame.empty() || frame[0] != sel[level_]) return to_idle();

        Bytes cl = levels_[level_];
        uint8_t bcc = cl[0] ^ cl[1] ^ cl[2] ^ cl[3];
        Bytes cl_bcc = cl;
        cl_bcc.push_back(bcc);

        if (frame.size() == 2 && frame[1] == 0x20) return cl_bcc;  // anticollision

        if (frame.size() == 9 && frame[1] == 0x70) {  // select
            Bytes sent(frame.begin() + 2, frame.begin() + 7);
            if (sent != cl_bcc || crc_a(Bytes(frame.begin(), frame.begin() + 7)) != Bytes(frame.begin() + 7, frame.end()))
                return to_idle();
            uint8_t sak;
            if (level_ + 1 < levels_.size()) {
                ++level_;
                sak = 0x04;  // UID not complete
            } else {
                state_ = ACTIVE;
                ++completed_reads;
                sak = uid_.size() == 4 ? 0x08 : 0x00;
            }
            Bytes out{sak};
            Bytes c = crc_a(out);
            out.insert(out.end(), c.begin(), c.end());
            return out;
        }
        return to_idle();
    }

private:
    enum State { IDLE, READY, ACTIVE };

    Bytes to_idle() {
        state_ = IDLE;
        return {};
    }

    Bytes uid_;
    std::vector<Bytes> levels_;
    State state_ = IDLE;
    size_t level_ = 0;
};

class Chip {
public:
    explicit Chip(std::vector<Step> steps, bool interactive)
        : steps_(steps.begin(), steps.end()), interactive_(interactive) {
        reset();
        advance();
    }

    std::mutex m;
    std::vector<std::string> trace;
    std::deque<std::string> pending_taps;

    bool write(uint8_t addr, uint8_t reg, const Bytes& data) {
        trace.push_back("W " + hex2(reg) + " " + hex_list(data));
        if (addr != kAddress) return false;
        if (reg == REG_FIFO_DATA) {
            fifo_.insert(fifo_.end(), data.begin(), data.end());
            if (fifo_.size() > 64) fifo_.resize(64);
            return true;
        }
        uint8_t v = data.empty() ? 0 : data[0];
        if (reg == REG_FIFO_LEVEL) {
            if (v & 0x80) fifo_.clear();
        } else if (reg == REG_COM_IRQ || reg == REG_DIV_IRQ) {
            if (v & 0x80)
                regs_[reg] |= v & 0x7F;
            else
                regs_[reg] &= ~v & 0x7F;
        } else if (reg == REG_COMMAND) {
            uint8_t cmd = v & 0x0F;
            if (cmd == CMD_SOFT_RESET) {
                reset();
            } else if (cmd == CMD_CALC_CRC) {
                Bytes c = crc_a(fifo_);
                regs_[REG_CRC_LSB] = c[0];
                regs_[REG_CRC_MSB] = c[1];
                regs_[REG_DIV_IRQ] |= 0x04;
                command_ = CMD_IDLE;
            } else {
                command_ = cmd;
            }
        } else if (reg == REG_BIT_FRAMING) {
            regs_[reg] = v & 0x7F;
            if ((v & 0x80) && command_ == CMD_TRANSCEIVE) transceive(v & 0x07);
        } else {
            regs_[reg] = v;
        }
        return true;
    }

    bool read(uint8_t addr, uint8_t reg, uint8_t* out, size_t n) {
        if (addr != kAddress) {
            trace.push_back("R " + hex2(reg) + " NACK");
            return false;
        }
        Bytes got;
        for (size_t i = 0; i < n; ++i) {
            if (reg == REG_FIFO_DATA) {
                uint8_t b = 0;
                if (!fifo_.empty()) {
                    b = fifo_.front();
                    fifo_.erase(fifo_.begin());
                }
                got.push_back(b);
            } else if (reg == REG_FIFO_LEVEL) {
                got.push_back(static_cast<uint8_t>(fifo_.size()));
            } else {
                got.push_back(regs_[reg]);
            }
        }
        trace.push_back("R " + hex2(reg) + " -> " + hex_list(got));
        for (size_t i = 0; i < n; ++i) out[i] = got[i];
        return true;
    }

private:
    static std::string hex2(uint8_t v) {
        char buf[4];
        std::snprintf(buf, sizeof buf, "%02X", v);
        return buf;
    }

    void reset() {
        for (auto& r : regs_) r = 0;
        fifo_.clear();
        command_ = CMD_IDLE;
    }

    void advance() {
        tag_.reset();
        if (interactive_) {
            if (!pending_taps.empty()) {
                tag_ = std::make_unique<Tag>(parse_uid(pending_taps.front()));
                pending_taps.pop_front();
            } else {
                absent_left_ = 1;
            }
            return;
        }
        if (steps_.empty()) throw SessionEnd{};
        Step s = steps_.front();
        steps_.pop_front();
        if (s.kind == StepKind::End) throw SessionEnd{};
        if (s.kind == StepKind::Absent)
            absent_left_ = s.count;
        else
            tag_ = std::make_unique<Tag>(parse_uid(s.uid));
    }

    void on_reqa() {
        if (!tag_) {
            if (absent_left_ <= 0) advance();
            --absent_left_;
        } else if (tag_->completed_reads >= 1) {
            advance();  // tag taken away
            if (!tag_) --absent_left_;
        }
    }

    void transceive(int last_bits) {
        Bytes frame;
        frame.swap(fifo_);
        if (frame == Bytes{0x26} && last_bits == 7) on_reqa();
        Bytes resp = tag_ ? tag_->handle(frame, last_bits) : Bytes{};
        regs_[REG_ERROR] = 0;
        if (!resp.empty()) {
            fifo_ = resp;
            regs_[REG_CONTROL] = 0;
            regs_[REG_COM_IRQ] |= 0x30;
        } else {
            regs_[REG_COM_IRQ] |= 0x01;  // timer expired: no answer
        }
    }

    std::deque<Step> steps_;
    bool interactive_;
    uint8_t regs_[64];
    Bytes fifo_;
    uint8_t command_ = CMD_IDLE;
    std::unique_ptr<Tag> tag_;
    int absent_left_ = 0;
};

std::mutex g_chip_mutex;
std::shared_ptr<Chip> g_chip;

std::shared_ptr<Chip> current() {
    std::lock_guard<std::mutex> lk(g_chip_mutex);
    return g_chip;
}

class SimBus final : public xerxes::I2CBus {
public:
    void write_mem(uint8_t addr, uint8_t reg, const uint8_t* data, size_t len) override {
        auto chip = current();
        if (!chip) throw xerxes::I2CError("no simulated module");
        std::lock_guard<std::mutex> lk(chip->m);
        bool ok;
        try {
            ok = chip->write(addr, reg, Bytes(data, data + len));
        } catch (const SessionEnd&) {
            throw xerxes::I2CError("simulator session ended");
        }
        if (!ok) throw xerxes::I2CError("could not communicate with module (NACK), check wiring");
    }

    void read_mem(uint8_t addr, uint8_t reg, uint8_t* out, size_t len) override {
        auto chip = current();
        if (!chip) throw xerxes::I2CError("no simulated module");
        std::lock_guard<std::mutex> lk(chip->m);
        if (!chip->read(addr, reg, out, len))
            throw xerxes::I2CError("could not communicate with module (NACK), check wiring");
    }
};

}  // namespace

void set_script(const std::vector<Step>& steps) {
    auto chip = std::make_shared<Chip>(steps, false);
    std::lock_guard<std::mutex> lk(g_chip_mutex);
    g_chip = chip;
}

void set_interactive() {
    auto chip = std::make_shared<Chip>(std::vector<Step>{}, true);
    std::lock_guard<std::mutex> lk(g_chip_mutex);
    g_chip = chip;
}

void tap(const std::string& uid) {
    auto chip = current();
    std::lock_guard<std::mutex> lk(chip->m);
    chip->pending_taps.push_back(uid);
}

std::vector<std::string> trace() {
    auto chip = current();
    std::lock_guard<std::mutex> lk(chip->m);
    return chip->trace;
}

}  // namespace rfid_sim

namespace xerxes {
std::unique_ptr<I2CBus> open_i2c_bus(int) { return std::make_unique<rfid_sim::SimBus>(); }
}  // namespace xerxes
