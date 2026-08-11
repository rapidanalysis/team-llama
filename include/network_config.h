#pragma once
#include <string>
#include <vector>
#include "json.hpp"

namespace network_config {

    // Returns a JSON object with interface list, primary IP, gateway, and DNS servers.
    // Called by GET /api/netinfo.
    nlohmann::json get_network_info();

    // Writes a static IP configuration block for the given interface to /etc/dhcpcd.conf.
    // All inputs are validated before any shell interaction.
    // ip must be in CIDR notation e.g. "192.168.1.100/24".
    std::string set_static(const std::string& iface,
                           const std::string& ip,
                           const std::string& gateway,
                           const std::string& dns);

    // Removes the Xerxes-managed static block for the given interface from /etc/dhcpcd.conf,
    // reverting it to DHCP.
    std::string set_dhcp(const std::string& iface);

}
