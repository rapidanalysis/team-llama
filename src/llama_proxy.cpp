#include "llama_proxy.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>

namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                    [](unsigned char c) { return std::tolower(c); });
    return s;
}

ProxyResponse parse_response(const std::string& raw) {
    ProxyResponse resp;
    auto header_end = raw.find("\r\n\r\n");
    std::string head = (header_end == std::string::npos) ? raw : raw.substr(0, header_end);
    resp.body = (header_end == std::string::npos) ? "" : raw.substr(header_end + 4);

    std::istringstream head_stream(head);
    std::string line;
    std::getline(head_stream, line);
    {
        std::istringstream status_line(line);
        std::string http_version;
        int code = 0;
        status_line >> http_version >> code;
        resp.status = code;
    }

    while (std::getline(head_stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = to_lower(line.substr(0, colon));
        if (key == "content-type") {
            std::string value = line.substr(colon + 1);
            auto start = value.find_first_not_of(' ');
            resp.content_type = (start == std::string::npos) ? "" : value.substr(start);
        }
    }

    return resp;
}

} // namespace

ProxyResponse proxy_to_llama(
    const std::string& host,
    int port,
    const std::string& method,
    const std::string& path,
    const std::string& body
) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) throw std::runtime_error("socket() failed");

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(sock);
        throw std::runtime_error(
            "llama-server not reachable on " + host + ":" + std::to_string(port));
    }

    std::ostringstream req;
    req << method << " " << path << " HTTP/1.1\r\n"
        << "Host: " << host << "\r\n"
        << "Content-Type: application/json\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n"
        << "\r\n"
        << body;

    std::string req_str = req.str();
    ssize_t sent_total = 0;
    while (sent_total < static_cast<ssize_t>(req_str.size())) {
        ssize_t n = send(sock, req_str.data() + sent_total, req_str.size() - sent_total, 0);
        if (n <= 0) { close(sock); throw std::runtime_error("send() failed"); }
        sent_total += n;
    }

    std::string raw;
    char buf[8192];
    ssize_t n;
    while ((n = recv(sock, buf, sizeof(buf), 0)) > 0) {
        raw.append(buf, n);
    }

    close(sock);
    return parse_response(raw);
}
