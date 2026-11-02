#pragma once
#include <string>

// Newt (Pangolin tunnel client) management — FR-1.
//
// Newt runs as a Docker container named "xerxes-newt" from the official
// fosrl/newt image. The Pangolin endpoint, Newt ID and secret are saved to a
// 0600 env file outside the web root ($HOME/.xerxes/newt.env, or the
// directory in $XERXES_CONFIG_DIR) and passed with --env-file, so the secret
// never appears on a command line, in the server log, or in an API response.

namespace newt_manager {

    struct Result {
        bool success;
        std::string error;    // short human-readable message when success == false
        std::string output;   // raw docker output (for the UI's details view)
        Result() : success(false) {}
    };

    struct Status {
        // connected | connecting | auth_failed | error | stopped |
        // not_configured | docker_unavailable
        std::string state;
        std::string container;   // docker state: running, exited, restarting... or "absent"
        std::string endpoint;    // saved endpoint ("" if not configured)
        std::string newt_id;     // saved Newt ID ("" if not configured)
        bool has_secret;         // true if a secret is saved (the secret itself is never returned)
        std::string detail;      // the log line or docker message the state was derived from
        Status() : has_secret(false) {}
    };

    // Returns "" if the values are valid, otherwise a message for the user.
    // An empty secret is allowed only when allow_empty_secret is true
    // (meaning "keep the saved secret").
    std::string validate(const std::string& endpoint,
                         const std::string& newt_id,
                         const std::string& secret,
                         bool allow_empty_secret = false);

    // Saves the config and (re)starts the xerxes-newt container.
    // If secret is empty, the previously saved secret is reused.
    Result enable(const std::string& endpoint,
                  const std::string& newt_id,
                  const std::string& secret);

    // Stops and removes the container. The saved config is kept so Newt can
    // be re-enabled without re-entering the secret.
    Result disable();

    // Current state, derived from `docker inspect` and the container's logs.
    Status status();

    // Last `lines` lines of the container's log (stdout + stderr).
    std::string logs(int lines);

    // Pure helper used by status(): works out the tunnel state from Newt's
    // log output. Sets *detail to the line that decided it. Exposed for tests.
    std::string classify_logs(const std::string& log, std::string* detail);

}
