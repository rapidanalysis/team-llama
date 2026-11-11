#include "llama_manager.h"
#include "llama_process.h"
#include "model_downloader.h"
#include <mutex>
#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {

const std::string kServerBinary = "third_party/llama.cpp/build/bin/llama-server";
const std::string kHost = "127.0.0.1";
const int kPort = 8081;
const std::string kModelsDir = "models";

LlamaProcess& process() {
    static LlamaProcess instance(kServerBinary, kHost, kPort);
    return instance;
}

// Filename of the model llama-server was started with (empty when stopped)
std::string& current_model() {
    static std::string name;
    return name;
}

bool is_valid_model_filename(const std::string& name) {
    return !name.empty() &&
           name.find("..") == std::string::npos &&
           name.find('/') == std::string::npos;
}

std::mutex& lock() {
    static std::mutex m;
    return m;
}

} // namespace

namespace llama_manager {

ActionResult enable(const std::string& model_filename) {
    std::lock_guard<std::mutex> guard(lock());

    if (!is_valid_model_filename(model_filename)) {
        return {false, "Invalid model filename"};
    }

    std::string model_path = kModelsDir + "/" + model_filename;
    if (!fs::exists(model_path)) {
        return {false, "Model not found: " + model_path};
    }

    if (process().is_running()) {
        return {false, "llama-server is already running"};
    }

    if (!process().start(model_path)) {
        return {false, "Failed to start llama-server"};
    }

    current_model() = model_filename;
    return {true, ""};
}

ActionResult disable() {
    std::lock_guard<std::mutex> guard(lock());
    process().stop();
    current_model().clear();
    return {true, ""};
}

StatusResult status() {
    std::lock_guard<std::mutex> guard(lock());
    bool running = process().is_running();
    if (!running) current_model().clear();
    return {running, running ? static_cast<int>(process().pid()) : -1, current_model()};
}

ActionResult download(const std::string& source_url) {
    DownloadResult result = download_model(source_url, kModelsDir);
    if (!result.ok) return {false, result.error};
    return {true, ""};
}

std::vector<std::string> installed_models() {
    std::vector<std::string> models;
    if (!fs::exists(kModelsDir)) return models;
    for (auto& entry : fs::directory_iterator(kModelsDir)) {
        if (entry.path().extension() == ".gguf") {
            models.push_back(entry.path().filename().string());
        }
    }
    return models;
}

ActionResult delete_model(const std::string& model_filename) {
    std::lock_guard<std::mutex> guard(lock());

    if (!is_valid_model_filename(model_filename) ||
        fs::path(model_filename).extension() != ".gguf") {
        return {false, "Invalid model filename"};
    }

    if (process().is_running() && current_model() == model_filename) {
        return {false, "This model is in use. Turn Llama off before deleting it."};
    }

    std::string model_path = kModelsDir + "/" + model_filename;
    std::error_code ec;
    if (!fs::is_regular_file(model_path, ec)) {
        return {false, "Model not found: " + model_filename};
    }
    if (!fs::remove(model_path, ec) || ec) {
        return {false, "Could not delete " + model_filename + (ec ? ": " + ec.message() : "")};
    }
    return {true, ""};
}

ProxyResponse proxy(const std::string& path, const std::string& method, const std::string& body) {
    std::lock_guard<std::mutex> guard(lock());
    if (!process().is_running()) {
        throw std::runtime_error("llama-server is not running");
    }
    return proxy_to_llama(kHost, kPort, method, path, body);
}

} // namespace llama_manager
