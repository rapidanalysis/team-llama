#pragma once
#include <string>
#include <vector>

// Newt (Pangolin tunnel client) management.
//
// Newt runs as a Docker container named "xerxes-newt" from the official
// fosrl/newt image. The Pangolin endpoint, Newt ID and secret are saved to a
// 0600 env file outside the web root ($HOME/.xerxes/newt.env, or the
// directory in $XERXES_CONFIG_DIR) and passed with --env-file, so the secret
// never appears on a command line, in the server log, or in an API response.
//
// Also handles:
//   - automatic updates (sponsor request, 16 Oct): pull fosrl/newt:latest,
//     recreate the container if the image changed, and roll back to the
//     previous image if the new one does not reconnect;
//   - a Docker access check with step-by-step fixes (defect D-01);
//   - detecting other copies of Newt on the device, e.g. the sponsor's native
//     newt.service, before enabling (defect D-05).
//
// Docker is called as "docker" by default. Set XERXES_DOCKER="sudo -n docker"
// to go through a narrow sudoers rule instead (see docker_fixes()).

namespace newt_manager {

    // Another copy of Newt found on this device.
    struct Instance {
        std::string kind;     // "service" (systemd) or "container"
        std::string name;     // e.g. "newt.service" or the container name
        std::string detail;   // image / status, for the confirmation message
    };

    struct Result {
        bool success;
        std::string error;    // short human-readable message when success == false
        std::string output;   // raw docker output or a success message
        // Machine-readable reason, so the UI can react:
        //   ""              normal success / failure
        //   "docker_access" the backend can't use Docker; call docker_check()
        //   "conflict"      other Newt instances are running; see conflicts
        //   "up_to_date"    update requested but nothing newer
        //   "rolled_back"   new version failed to connect, previous one restored
        std::string code;
        std::vector<Instance> conflicts;
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
        std::string version;     // Newt version from the container log, e.g. "1.18.1" ("" if unknown)
        bool rollback_available; // a previous image is kept, differs from the running one, and can be restored
        // Update settings and the result of the last check
        bool auto_update;
        long long last_check;    // Unix time of the last update check, 0 = never
        std::string last_result; // e.g. "Newt is up to date."
        bool update_available;   // the last check found a newer image
        Status() : has_secret(false), rollback_available(false), auto_update(true),
                   last_check(0), update_available(false) {}
    };

    // One way to give the backend Docker access, shown to the user as-is.
    struct Fix {
        std::string title;
        std::string why;
        std::vector<std::string> commands;  // copy-paste into a terminal on the device
        std::string after;                  // what to do once the commands have run
    };

    struct DockerAccess {
        // ok | permission_denied | sudo_denied | daemon_down | not_installed | error
        std::string state;
        std::string message;     // one-line explanation
        std::string user;        // the user the backend runs as
        std::string detail;      // Docker's own output
        std::vector<Fix> fixes;  // empty when state == "ok"
    };

    // ── setup and control ─────────────────────────────────────────────────

    // Returns "" if the values are valid, otherwise a message for the user.
    // An empty secret is allowed only when allow_empty_secret is true
    // (meaning "keep the saved secret").
    std::string validate(const std::string& endpoint,
                         const std::string& newt_id,
                         const std::string& secret,
                         bool allow_empty_secret = false);

    // Checks Docker access, then other Newt instances (unless force is true),
    // then saves the config and (re)starts the xerxes-newt container.
    // If secret is empty, the previously saved secret is reused.
    Result enable(const std::string& endpoint,
                  const std::string& newt_id,
                  const std::string& secret,
                  bool force = false);

    // Stops and removes the container. The saved config is kept so Newt can
    // be re-enabled without re-entering the secret.
    Result disable();

    // Current state, derived from `docker inspect` and the container's logs.
    Status status();

    // Last `lines` lines of the container's log (stdout + stderr).
    std::string logs(int lines);

    // Can the backend use Docker? Includes fixes when it can't.
    DockerAccess docker_check();

    // Other copies of Newt on this device: the native newt.service and any
    // running container other than xerxes-newt that uses a Newt image.
    std::vector<Instance> find_other_instances();

    // ── updates ───────────────────────────────────────────────────────────

    // Pulls fosrl/newt:latest and records whether it differs from the image
    // the container is running. Does not restart anything.
    Result check_update();

    // Pulls the latest image and, if it is newer, keeps the current image as
    // fosrl/newt:xerxes-previous and recreates the container. If the tunnel
    // was connected before and does not reconnect within the wait time
    // (60 s, or $XERXES_NEWT_UPDATE_WAIT seconds), rolls back automatically.
    // automatic = true when called by the daily updater: it then skips an
    // image that was rolled back before.
    Result update(bool automatic = false);

    // Restarts Newt from the image kept by the last update.
    Result rollback();

    // Turns the daily automatic update on or off (saved on the device).
    Result set_auto_update(bool enabled);

    // Starts the background thread that runs update(true) once a day while
    // automatic updates are on. Safe to call more than once.
    void start_auto_updater();

    // ── pure helpers (no Docker needed; exposed for tests) ────────────────

    // Works out the tunnel state from Newt's log output. Sets *detail to the
    // line that decided it.
    std::string classify_logs(const std::string& log, std::string* detail);

    // Version from the latest "Newt version X" line, e.g. "1.18.1"; "" if none.
    std::string parse_version(const std::string& log);

    // True if latest differs from running. When automatic is true, an image
    // the user rolled back from (skipped) is not installed again.
    bool update_needed(const std::string& running_image,
                       const std::string& latest_image,
                       const std::string& skipped_image,
                       bool automatic);

    // True if the daily updater should run now.
    bool auto_update_due(bool enabled, bool configured,
                         long long now, long long last_check);

    // Maps Docker's output and exit code to a DockerAccess state.
    std::string classify_docker_output(const std::string& output, int exit_code);

    // Fixes to show for a DockerAccess state. user is shown in commands only
    // if it is a valid Linux user name.
    std::vector<Fix> docker_fixes(const std::string& state, const std::string& user);

    // Picks Newt containers out of
    // `docker ps --format '{{.Names}}\t{{.Image}}\t{{.Status}}'`,
    // leaving out our own xerxes-newt.
    std::vector<Instance> parse_containers(const std::string& docker_ps_output);

    // The docker command to use for a value of $XERXES_DOCKER. Only "docker"
    // and "sudo -n docker" are accepted; anything else falls back to "docker".
    std::string docker_command_for(const char* env_value);

}
