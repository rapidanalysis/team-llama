# Newt: Commands

Day-to-day, Newt is controlled from the **Newt (Pangolin)** card on the admin panel's **Tunnels** page, and you never need a terminal. This page lists the commands for when you do: checking on Newt, troubleshooting, scripting, or running Newt without the admin panel.

Run these on the Xerxes Pi (or your development VM). Commands starting with `docker` need a user that can use Docker (see [setup.md](setup.md#step-2-make-sure-docker-works)); otherwise put `sudo` in front.

## The Newt card

| Control | What it does |
| --- | --- |
| **Endpoint**, **Newt ID**, **Secret** | The connection details from your Pangolin Site. Leave **Secret** empty to keep the saved one |
| **Enable** | Saves the details and (re)starts Newt. Asks first if another copy of Newt is running on the device |
| **Disable** | Stops and removes Newt. The saved details are kept |
| **Check Status** | Refreshes the **Tunnel** box |
| **View Logs** | Shows Newt's last 100 log lines in a pop-up |
| **Update automatically** | Checks for a newer Newt once a day and installs it (on by default once Newt is set up) |
| **Check for Updates** | Downloads the latest image to compare. Restarts nothing |
| **Update Now** | Installs the latest version. Rolls back by itself if it doesn't reconnect within a minute |
| **Roll Back** | Restarts Newt on the version from before the last update |
| **Check Again** (in the *Newt needs Docker access* box) | Re-checks whether the panel can use Docker after you've applied a fix |

## Checking on Newt

The admin panel runs Newt as a Docker container called **`xerxes-newt`**.

| Command | What it does |
| --- | --- |
| `docker ps --filter name=xerxes-newt` | Shows whether Newt is running and for how long. |
| `docker logs --tail 50 xerxes-newt` | Shows Newt's last 50 log lines. |
| `docker logs -f xerxes-newt` | Follows the log live. Press **Ctrl+C** to stop watching (Newt keeps running). |
| `docker restart xerxes-newt` | Restarts Newt with the same settings. |
| `docker rm -f xerxes-newt` | Stops and removes Newt (the same as **Disable** on the card). Saved settings are kept. |
| `docker images fosrl/newt` | Shows the Newt images on the device: `latest`, and `xerxes-previous` (the version kept for **Roll Back**) after an update. |
| `sudo journalctl -u xerxes \| grep '\[Newt\]'` | Shows what the panel did with Newt (enable, updates, daily checks), on a deployed device. |

### What the log lines mean

| Log line | Meaning |
| --- | --- |
| `Newt version X` | Printed when Newt starts. This is the version shown on the card. |
| `Websocket connected` | Newt reached the Pangolin server and is logging in. |
| `Tunnel connection to server established successfully!` | Connected. The card shows **Connected**. |
| `Failed to connect: failed to get token ... No newt found with that newtId` | Pangolin rejected the Newt ID or secret. The card shows **Rejected**. |
| `Failed to connect: ...` (any other reason) | Newt can't reach the server. The card shows **Can't reach server**. |

## Saved settings

The admin panel saves its Newt settings in a folder that only its own user can read:

| Admin panel runs as | Folder |
| --- | --- |
| root (system service) | `/root/.xerxes/` |
| your user (development) | `~/.xerxes/` |

| File | Contents |
| --- | --- |
| `newt.env` (permissions `600`) | The endpoint, Newt ID and secret. Do not share or commit this file. Deleting it makes the card show **Not set up** after Newt is disabled. |
| `newt-update.conf` | Whether automatic updates are on, when the last check ran and what it found, and any version that was rolled back (so automatic updates skip it). |

## Environment variables

Set these when starting the admin panel by hand, e.g. `XERXES_CONFIG_DIR=/etc/xerxes ./xerxes_backend`.

| Variable | Effect |
| --- | --- |
| `XERXES_CONFIG_DIR` | Use this folder for settings instead of `~/.xerxes`. Also used by RFID and OLED. |
| `XERXES_DOCKER` | Set to `sudo -n docker` to run Docker through a narrow sudo rule instead of directly. Any other value is ignored. |
| `XERXES_NEWT_UPDATE_WAIT` | Seconds to wait for the tunnel to reconnect after an update before rolling back (3 to 600, default 60). |

## Updating Newt

Use the card's **Updates** section (see [setup.md](setup.md#step-5-keep-newt-up-to-date-optional)). Behind the scenes, an update:

1. downloads `fosrl/newt:latest`;
2. if it is newer than the running version, keeps the running version as `fosrl/newt:xerxes-previous`;
3. restarts `xerxes-newt` on the new version;
4. if the tunnel was connected before and doesn't reconnect within 60 seconds, restarts on `xerxes-previous` and marks the new version to be skipped by automatic updates.

## Controlling Newt from scripts (API)

The card talks to the admin panel's REST API, which you can also call with `curl`. Replace `localhost` with the Pi's address when calling from another machine.

| Command | What it does |
| --- | --- |
| `curl http://localhost:8000/api/newt/status` | Current state (see below). The secret is never returned. |
| `curl http://localhost:8000/api/newt/logs` | Newt's last 100 log lines, as `{"output": "..."}`. |
| `curl http://localhost:8000/api/newt/docker-check` | Whether the panel can use Docker: `state` (`ok`, `permission_denied`, `sudo_denied`, `daemon_down`, `not_installed` or `error`), a message, the user the panel runs as, and a list of `fixes` with the commands to run. |
| `curl -X POST http://localhost:8000/api/newt/disable` | Stops Newt. Settings are kept. |
| `curl -X POST http://localhost:8000/api/newt/enable -H "Content-Type: application/json" -d '{"endpoint":"https://pangolin.example.com","newt_id":"<id>","secret":""}'` | Starts Newt. An empty `secret` reuses the saved one, which keeps the secret out of your shell history. |
| `curl -X POST http://localhost:8000/api/newt/update/check` | Checks for a newer version. Restarts nothing. |
| `curl -X POST http://localhost:8000/api/newt/update` | Installs the newest version (rolls back by itself if it doesn't reconnect). |
| `curl -X POST http://localhost:8000/api/newt/rollback` | Restarts Newt on the version from before the last update. |
| `curl -X POST http://localhost:8000/api/newt/auto-update -H "Content-Type: application/json" -d '{"enabled":false}'` | Turns automatic updates off (or on with `true`; only allowed once Newt is set up). |

**Status.** `GET /api/newt/status` returns `state`, which is one of `connected`, `connecting`, `auth_failed`, `error`, `stopped`, `not_configured` or `docker_unavailable`. It also returns the container's Docker state, the saved `endpoint` and `newt_id`, `has_secret`, the running `version`, whether a roll back is available, and the update settings (`auto_update`, `last_check` as Unix time, `last_result`, `update_available`).

**Results.** The other `POST` endpoints return `success`, `error`, `output` and `code`. `code` tells a script what happened:

| `code` | Meaning |
| --- | --- |
| `docker_access` | The panel can't use Docker. Call `docker-check` for the fixes. |
| `conflict` | (enable only) Another Newt is running; `conflicts` lists them. Send the request again with `"force": true` to start anyway. |
| `up_to_date` | (update only) Nothing newer to install. |
| `rolled_back` | (update only) The new version failed, and the previous one was restored. |

Rules the admin panel checks before starting Newt:

| Field | Allowed |
| --- | --- |
| `endpoint` | Starts with `https://` (or `http://`); letters, digits and `. - : / _` only; up to 255 characters |
| `newt_id` | Letters and digits only, up to 64 characters |
| `secret` | Letters and digits only, up to 128 characters, or empty to reuse the saved one |

## Running Newt without the admin panel

Useful for testing a Site on its own. **Only run one copy of Newt per Site**: stop the admin panel's copy first with **Disable** or `docker rm -f xerxes-newt`.

### With Docker

```bash
docker run -d --name newt --restart unless-stopped \
  -e PANGOLIN_ENDPOINT=https://pangolin.example.com \
  -e NEWT_ID=<id> \
  -e NEWT_SECRET=<secret> \
  fosrl/newt:latest
```

Remove it afterwards with `docker rm -f newt`. If you leave it running, the panel lists it as another copy of Newt the next time you click **Enable**.

Newt reads these environment variables:

| Variable | Meaning |
| --- | --- |
| `PANGOLIN_ENDPOINT` | Address of the Pangolin server |
| `NEWT_ID` | The Site's Newt ID |
| `NEWT_SECRET` | The Site's secret |
| `LOG_LEVEL` | Optional, for example `DEBUG` for more detail |

The same values can be given as flags: `newt --id <id> --secret <secret> --endpoint <url>`.

### Installed directly (no Docker)

This is how Newt is set up on the sponsor's Xerxes Pi:

```bash
curl -fsSL https://static.pangolin.net/get-newt.sh | bash    # installs /usr/local/bin/newt
```

It is then run as a system service (`/etc/systemd/system/newt.service`) that reads its settings from `/etc/newt/newt.env` (permissions `600`):

| Command | What it does |
| --- | --- |
| `systemctl status newt` | Shows whether the service is running. |
| `journalctl -u newt -f` | Follows Newt's log. |
| `sudo systemctl restart newt` | Restarts Newt. |
| `sudo systemctl disable --now newt` | Stops Newt and stops it starting at boot. |

The admin panel detects this service when it is running and asks before starting its own copy.

**Caution:** if you reach the Pi through Pangolin (for example a web SSH page), that connection runs over this Newt. Stopping it, or starting a second Newt with the same Site details, cuts you off.

## For developers

| Command | What it does |
| --- | --- |
| `make` | Builds the admin panel (`xerxes_backend`), including Newt support. |
| `make test_newt` | Runs the 19 Newt unit tests in `tests/test_newt.cpp`. |
| `make test` | Runs every test suite, including Newt's. |
