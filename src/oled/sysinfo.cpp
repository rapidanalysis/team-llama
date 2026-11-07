#include "sysinfo.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
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
