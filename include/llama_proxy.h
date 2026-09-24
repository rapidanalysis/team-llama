#pragma once
#include <string>

struct ProxyResponse {
    int status = 0;
    std::string content_type = "application/json";
    std::string body;
};

ProxyResponse proxy_to_llama(
    const std::string& host,
    int port,
    const std::string& method,
    const std::string& path,
    const std::string& body
);
