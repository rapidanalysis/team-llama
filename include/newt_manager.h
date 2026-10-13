#pragma once
#include <string>

// Newt is the client half of Pangolin. It runs on the device, dials out to a
// Pangolin server and holds a tunnel open, so the device can be reached from
// outside without opening any inbound port. We run the official container
// image and only manage its settings and lifecycle.

namespace newt_manager {

struct Settings {
    std::string endpoint;   // Pangolin server, e.g. https://pangolin.example.com
    std::string id;         // site ID issued by the Pangolin server
    std::string secret;     // site secret issued with the ID
};

struct ActionResult {
    bool success;
    std::string output;     // docker output, never contains the secret
    std::string error;
};

struct StatusResult {
    std::string state;      // running, exited, restarting, created, not_installed
    bool configured;        // a settings file is stored on this device
    std::string endpoint;   // the secret is never returned
    std::string id;
};

// Returns an empty string if the settings are valid, otherwise a message.
std::string validate(const Settings& s);

// Text of the env file passed to docker with --env-file.
std::string env_file_contents(const Settings& s);

// docker run command. The secret is read from env_file, so it never appears
// on a command line or in the process list.
std::string build_run_command(const std::string& env_file);

ActionResult up(const Settings& s);
ActionResult down();
StatusResult status();

}
