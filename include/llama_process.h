#pragma once
#include <string>
#include <sys/types.h>

// Manages the llama-server child process: starts it against a given
// model file, tracks whether it's alive, and stops it cleanly.
class LlamaProcess {
public:
    LlamaProcess(std::string server_binary, std::string host, int port);
    ~LlamaProcess();

    bool start(const std::string& model_path, int ctx_size = 4096);
    void stop(int timeout_ms = 5000);
    bool is_running();

    pid_t pid() const { return pid_; }
    int port() const { return port_; }

private:
    std::string server_binary_;
    std::string host_;
    int port_;
    pid_t pid_ = -1;
};
