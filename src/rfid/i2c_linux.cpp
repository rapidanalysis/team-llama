// Linux i2c-dev backend (replaces smbus2's i2c_rdwr usage in PiicoDev_Unified).
#include "i2c.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace xerxes {
namespace {

std::string hex2(unsigned v) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "0x%02X", v & 0xFF);
    return buf;
}

class LinuxI2CBus final : public I2CBus {
public:
    explicit LinuxI2CBus(int bus) : path_("/dev/i2c-" + std::to_string(bus)) {
        fd_ = ::open(path_.c_str(), O_RDWR | O_CLOEXEC);
        if (fd_ < 0) {
            throw I2CError("could not open " + path_ + ": " + std::strerror(errno) +
                           " (is I2C enabled? try `sudo raspi-config` -> Interface Options -> I2C)");
        }
    }

    ~LinuxI2CBus() override {
        if (fd_ >= 0) ::close(fd_);
    }

    LinuxI2CBus(const LinuxI2CBus&) = delete;
    LinuxI2CBus& operator=(const LinuxI2CBus&) = delete;

    void write_mem(uint8_t addr, uint8_t reg, const uint8_t* data, size_t len) override {
        std::vector<uint8_t> buf;
        buf.reserve(len + 1);
        buf.push_back(reg);
        buf.insert(buf.end(), data, data + len);

        i2c_msg msg{};
        msg.addr = addr;
        msg.flags = 0;
        msg.len = static_cast<__u16>(buf.size());
        msg.buf = buf.data();
        transfer(&msg, 1, addr);
    }

    void read_mem(uint8_t addr, uint8_t reg, uint8_t* out, size_t len) override {
        i2c_msg msgs[2]{};
        msgs[0].addr = addr;
        msgs[0].flags = 0;
        msgs[0].len = 1;
        msgs[0].buf = &reg;
        msgs[1].addr = addr;
        msgs[1].flags = I2C_M_RD;
        msgs[1].len = static_cast<__u16>(len);
        msgs[1].buf = out;
        transfer(msgs, 2, addr);
    }

private:
    void transfer(i2c_msg* msgs, unsigned n, uint8_t addr) {
        i2c_rdwr_ioctl_data data{};
        data.msgs = msgs;
        data.nmsgs = n;
        int rc;
        do {
            rc = ::ioctl(fd_, I2C_RDWR, &data);
        } while (rc < 0 && errno == EINTR);
        if (rc < 0) {
            throw I2CError("could not communicate with module at address " + hex2(addr) + " on " +
                           path_ + ": " + std::strerror(errno) + ", check wiring");
        }
    }

    std::string path_;
    int fd_ = -1;
};

}  // namespace

std::unique_ptr<I2CBus> open_i2c_bus(int bus) { return std::make_unique<LinuxI2CBus>(bus); }

}  // namespace xerxes
