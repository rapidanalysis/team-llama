#include "llama_process.h"
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <vector>
#include <thread>
#include <chrono>

LlamaProcess::LlamaProcess(std::string server_binary, std::string host, int port)
    : server_binary_(std::move(server_binary)), host_(std::move(host)), port_(port) {}

LlamaProcess::~LlamaProcess() {
    if (is_running()) stop();
}

bool LlamaProcess::start(const std::string& model_path, int ctx_size) {
    if (is_running()) return false;

    pid_t pid = fork();
    if (pid < 0) return false;

    if (pid == 0) {
        std::vector<std::string> args = {
            server_binary_,
            "-m", model_path,
            "--host", host_,
            "--port", std::to_string(port_),
            "--ctx-size", std::to_string(ctx_size)
        };
        std::vector<char*> argv;
        for (auto& a : args) argv.push_back(a.data());
        argv.push_back(nullptr);

        execv(server_binary_.c_str(), argv.data());
        _exit(127);
    }

    pid_ = pid;
    return true;
}

void LlamaProcess::stop(int timeout_ms) {
    if (pid_ <= 0) return;

    kill(pid_, SIGTERM);

    int waited = 0;
    while (waited < timeout_ms) {
        int status;
        pid_t r = waitpid(pid_, &status, WNOHANG);
        if (r == pid_) { pid_ = -1; return; }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waited += 100;
    }

    kill(pid_, SIGKILL);
    waitpid(pid_, nullptr, 0);
    pid_ = -1;
}

bool LlamaProcess::is_running() {
    if (pid_ <= 0) return false;
    int status;
    pid_t r = waitpid(pid_, &status, WNOHANG);
    if (r == 0) return true;
    pid_ = -1;
    return false;
}
