#include "network_config.h"
#include "json.hpp"
#include <stdio.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <iostream>

namespace network_config {

    static std::string capture(const char* cmd) {
        FILE* pipe = popen(cmd, "r");
        if (!pipe) return "";
        char buf[256];
        std::string result;
        while (fgets(buf, sizeof(buf), pipe)) result += buf;
        pclose(pipe);
        return result;
    }

    static std::string trim(std::string s) {
        auto start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        auto end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
    }

    // Parse non-loopback interface names from `ip addr show` output.
    // Lines starting with a digit look like: "2: eth0: <FLAGS> ..."
    static std::vector<std::string> parse_interfaces(const std::string& output) {
        std::vector<std::string> ifaces;
        std::istringstream ss(output);
        std::string line;
        while (std::getline(ss, line)) {
            if (line.empty() || !std::isdigit((unsigned char)line[0])) continue;
            auto c1 = line.find(':');
            if (c1 == std::string::npos) continue;
            auto c2 = line.find(':', c1 + 1);
            if (c2 == std::string::npos) continue;
            std::string name = trim(line.substr(c1 + 1, c2 - c1 - 1));
            // Skip loopback and virtual interfaces
            if (name == "lo" || name.rfind("docker", 0) == 0 || name.rfind("veth", 0) == 0) continue;
            ifaces.push_back(name);
        }
        return ifaces;
    }

    // Extract the first non-loopback "inet" address (with prefix) from `ip addr show`.
    static std::string parse_primary_ip(const std::string& output) {
        std::istringstream ss(output);
        std::string line;
        bool in_lo = false;
        while (std::getline(ss, line)) {
            if (!line.empty() && std::isdigit((unsigned char)line[0]))
                in_lo = (line.find(": lo:") != std::string::npos);
            if (in_lo) continue;
            auto pos = line.find("    inet ");
            if (pos != std::string::npos) {
                std::string rest = trim(line.substr(pos + 9));
                auto sp = rest.find(' ');
                return sp == std::string::npos ? rest : rest.substr(0, sp);
            }
        }
        return "";
    }

    // Extract the first interface that holds the primary IP.
    static std::string parse_primary_iface(const std::vector<std::string>& ifaces) {
        return ifaces.empty() ? "" : ifaces[0];
    }

    // Parse the gateway from `ip route show default` output.
    // Output looks like: "default via 192.168.1.1 dev eth0 ..."
    static std::string parse_gateway(const std::string& output) {
        auto via = output.find("via ");
        if (via == std::string::npos) return "";
        std::string rest = output.substr(via + 4);
        auto sp = rest.find(' ');
        return trim(sp == std::string::npos ? rest : rest.substr(0, sp));
    }

    // Parse nameserver lines from /etc/resolv.conf output.
    static std::vector<std::string> parse_dns(const std::string& output) {
        std::vector<std::string> dns;
        std::istringstream ss(output);
        std::string line;
        while (std::getline(ss, line)) {
            if (line.substr(0, 11) == "nameserver ") {
                std::string addr = trim(line.substr(11));
                if (!addr.empty()) dns.push_back(addr);
            }
        }
        return dns;
    }

    // Strict validation: only digits and dots (and optional /prefix for CIDR).
    static bool is_valid_ip(const std::string& ip) {
        if (ip.empty() || ip.size() > 19) return false;
        for (char c : ip) {
            if (!std::isdigit((unsigned char)c) && c != '.' && c != '/') return false;
        }
        return true;
    }

    // Only alphanumeric, underscore, hyphen, and dot are allowed in interface names.
    static bool is_valid_iface(const std::string& iface) {
        if (iface.empty() || iface.size() > 20) return false;
        for (char c : iface) {
            if (!std::isalnum((unsigned char)c) && c != '_' && c != '-' && c != '.') return false;
        }
        return true;
    }

    // ----------------------------------------------------------------
    // get_network_info()
    // Returns hostname, primary IP, gateway, DNS, and interface list.
    // ----------------------------------------------------------------
    nlohmann::json get_network_info() {
        std::string addr_out  = capture("ip addr show 2>/dev/null");
        std::string route_out = capture("ip route show default 2>/dev/null");
        std::string dns_out   = capture("cat /etc/resolv.conf 2>/dev/null");

        auto ifaces   = parse_interfaces(addr_out);
        auto dns      = parse_dns(dns_out);

        nlohmann::json j;
        j["interface"]  = parse_primary_iface(ifaces);
        j["ip"]         = parse_primary_ip(addr_out);
        j["gateway"]    = parse_gateway(route_out);
        j["dns"]        = dns;
        j["interfaces"] = ifaces;
        return j;
    }

    // ----------------------------------------------------------------
    // set_static()
    // Appends a Xerxes-managed static config block to /etc/dhcpcd.conf.
    // All inputs validated before any shell access.
    // ----------------------------------------------------------------
    std::string set_static(const std::string& iface,
                           const std::string& ip,
                           const std::string& gateway,
                           const std::string& dns) {
        if (!is_valid_iface(iface))
            return "[error] Invalid interface name.";
        if (!is_valid_ip(ip))
            return "[error] Invalid IP address. Use CIDR notation e.g. 192.168.1.100/24";
        if (!is_valid_ip(gateway))
            return "[error] Invalid gateway address.";
        if (!dns.empty() && !is_valid_ip(dns))
            return "[error] Invalid DNS address.";

        std::string effective_dns = dns.empty() ? "8.8.8.8" : dns;

        // Write the config block to a temp file to avoid shell quoting issues
        std::ofstream tmp("/tmp/xerxes_netcfg.tmp");
        if (!tmp.is_open())
            return "[error] Could not write temporary config file.";

        tmp << "\n# Xerxes static config — " << iface << "\n"
            << "interface " << iface << "\n"
            << "static ip_address=" << ip << "\n"
            << "static routers=" << gateway << "\n"
            << "static domain_name_servers=" << effective_dns << "\n";
        tmp.close();

        return capture(
            "sudo tee -a /etc/dhcpcd.conf < /tmp/xerxes_netcfg.tmp > /dev/null 2>&1 "
            "&& echo 'Static IP configuration saved. Reboot or restart dhcpcd to apply: sudo systemctl restart dhcpcd' "
            "|| echo '[error] Failed to write /etc/dhcpcd.conf — check sudo permissions.'"
        );
    }

    // ----------------------------------------------------------------
    // set_dhcp()
    // Removes the Xerxes-managed static block for an interface,
    // reverting it to DHCP.
    // ----------------------------------------------------------------
    std::string set_dhcp(const std::string& iface) {
        if (!is_valid_iface(iface))
            return "[error] Invalid interface name.";

        // Delete lines from the Xerxes marker comment up to (and including) the next blank line
        std::string cmd =
            "sudo sed -i '/# Xerxes static config — " + iface + "/,/^[[:space:]]*$/{/^[[:space:]]*$/!d}' "
            "/etc/dhcpcd.conf 2>&1 "
            "&& sudo sed -i '/# Xerxes static config — " + iface + "/d' /etc/dhcpcd.conf 2>&1 "
            "&& echo 'Reverted " + iface + " to DHCP. Reboot or restart dhcpcd to apply: sudo systemctl restart dhcpcd' "
            "|| echo '[error] Could not modify /etc/dhcpcd.conf'";

        return capture(cmd.c_str());
    }

}
