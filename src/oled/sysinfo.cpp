#include "sysinfo.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <cstdio>
#include <fstream>

std::string getHostname() {
    char name[256] = {};
    if (gethostname(name, sizeof(name) - 1) != 0)
        return "unknown";
    return name;
}

// First IPv4 address that isn't 127.0.0.1
std::string getIP() {
    struct ifaddrs* list; 
    if (getifaddrs(&list) != 0)
        return "No IP";

    std::string ip = "No IP";
    for (struct ifaddrs* i = list; i != nullptr; i = i->ifa_next) {
        if (i->ifa_addr == nullptr || i->ifa_addr->sa_family != AF_INET)
            continue;
        if (i->ifa_flags & IFF_LOOPBACK)
            continue;

        char buf[INET_ADDRSTRLEN];
        struct sockaddr_in* addr = (struct sockaddr_in*)i->ifa_addr;
        inet_ntop(AF_INET, &addr->sin_addr, buf, sizeof(buf));
        ip = buf;
        break;
    }

    freeifaddrs(list);
    return ip;
}

std::string getDNS() {
    std::ifstream file("/etc/resolv.conf");
    std::string word;
    while (file >> word) {
        if (word == "nameserver" && file >> word)
            return word;
    }
    return "-";
}

// The file gives the temperature in millidegrees, e.g. 55900 = 55.9 C
std::string getCpuTemp() {
    std::ifstream file("/sys/class/thermal/thermal_zone0/temp");
    int milli;
    if (!(file >> milli))
        return "-";

    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f`C", milli / 1000.0);
    return buf;
}

// The first number in /proc/uptime is seconds since boot, e.g. "3d 4h" or "2h 15m"
std::string getUptime() {
    std::ifstream file("/proc/uptime");
    double seconds;
    if (!(file >> seconds))
        return "-";

    long mins = (long)seconds / 60;
    char buf[24];
    if (mins >= 24 * 60)
        snprintf(buf, sizeof(buf), "%ldd %ldh", mins / (24 * 60), (mins / 60) % 24);
    else
        snprintf(buf, sizeof(buf), "%ldh %ldm", mins / 60, mins % 60);
    return buf;
}

// Used / total RAM in GB, e.g. "1.2/7.8G". Used is MemTotal - MemAvailable,
// which is what `free` shows, so cache doesn't count as used
std::string getMemory() {
    // Lines look like "MemTotal:  8058740 kB" (some have no unit, so read lines)
    std::ifstream file("/proc/meminfo");
    std::string line;
    long total = -1, available = -1;
    while (std::getline(file, line)) {
        long kb;
        if (sscanf(line.c_str(), "MemTotal: %ld", &kb) == 1)
            total = kb;
        else if (sscanf(line.c_str(), "MemAvailable: %ld", &kb) == 1)
            available = kb;
    }
    if (total <= 0 || available < 0)
        return "-";

    char buf[24];
    snprintf(buf, sizeof(buf), "%.1f/%.1fG", (total - available) / 1048576.0, total / 1048576.0);
    return buf;
}

// Used / total space on / in GB, e.g. "12/58G"
std::string getDisk() {
    struct statvfs fs;
    if (statvfs("/", &fs) != 0 || fs.f_blocks == 0)
        return "-";

    double total = (double)fs.f_blocks * fs.f_frsize;
    double used = total - (double)fs.f_bfree * fs.f_frsize;
    char buf[24];
    snprintf(buf, sizeof(buf), "%.0f/%.0fG", used / 1e9, total / 1e9);
    return buf;
}
