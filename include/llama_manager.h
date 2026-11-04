#pragma once
#include <string>
#include <vector>
#include "llama_proxy.h"

namespace llama_manager {

struct StatusResult {
    bool running;
    int pid;
};

struct ActionResult {
    bool success;
    std::string error;
};

ActionResult enable(const std::string& model_filename);
ActionResult disable();
StatusResult status();
ActionResult download(const std::string& source_url);
std::vector<std::string> installed_models();
ActionResult delete_model(const std::string& model_filename);
ProxyResponse proxy(const std::string& path, const std::string& method, const std::string& body);

}
