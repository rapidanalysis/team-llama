#pragma once

#include <cerrno>
#include <ctime>

namespace xerxes {

inline void sleep_ms(long ms) {
    timespec ts{ms / 1000, (ms % 1000) * 1000000L};
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {
    }
}

}  // namespace xerxes
