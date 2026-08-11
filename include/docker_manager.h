#pragma once
#include <string>
#include <vector>

// Configuration for a docker run invocation.
// Every field is validated before the command is assembled.
struct DockerRunConfig {
    std::string image;                  // required — e.g. "nginx:latest"
    std::string name;                   // optional — container name
    std::vector<std::string> ports;     // optional — ["8080:80", "443:443"]
    std::vector<std::string> volumes;   // optional — ["/host/path:/container/path"]
    std::vector<std::string> env;       // optional — ["KEY=value"]
    std::string restart;                // optional — no|always|unless-stopped|on-failure
    bool detach = true;                 // run with -d (detached); default true
};

struct DockerRunResult {
    bool success;
    std::string output;   // stdout/stderr from docker
    std::string error;    // validation or execution error message
    DockerRunResult() : success(false) {}
};

namespace docker_manager {

    // Validates every field in config.
    // Returns an empty string on success, or a human-readable error on failure.
    std::string validate(const DockerRunConfig& config);

    // Builds the shell command string from a validated config.
    // Call validate() before this — behaviour is undefined for invalid configs.
    std::string build_command(const DockerRunConfig& config);

    // Validates, builds, and executes docker run via popen.
    // stdout and stderr are captured and returned in DockerRunResult.
    DockerRunResult run(const DockerRunConfig& config);

}
