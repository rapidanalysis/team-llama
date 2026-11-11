# Admin Panel: Commands

Day-to-day, everything is done from the web panel at `http://<device-ip>:8000`. This page lists the terminal commands for building, running and checking the panel. Commands for a single feature are in that feature's own `commands.md` (e.g. [Newt](../newt/commands.md), [llama.cpp](../llama-cpp/commands.md), [RFID](../rfid/commands.md), [OLED](../oled-display/commands.md)).

Run these from the `team-llama` repo root unless stated otherwise.

## Build targets

| Command | What it does |
|---|---|
| `make` | Builds `./xerxes_backend`. Only rebuilds what changed, including files that use a header you edited |
| `make -j4` | The same, compiling four files at once |
| `make llama` | Builds `llama-server` for the Llama tab into `third_party/llama.cpp/build/bin/`. An hour or more on a Pi; does nothing if it's already built |
| `make llama LLAMA_JOBS=4` | The same with four parallel jobs. Only on a machine with plenty of memory: the default of 1 stops a 2–4 GB Pi running out of memory |
| `make static` | `./xerxes_backend` with libgcc and libstdc++ linked in, so it doesn't depend on the device's C++ runtime version (libcurl and glibc still come from the device) |
| `make deb` | Builds `build/deb/xerxes-pi_<version>_<arch>.deb`. It installs the same files as `deploy_systemd.sh` (backend, `public/`, llama-server and the `xerxes` service) and starts the service. Set the version with `make deb DEB_VERSION=0.3.0` (default `0.2.0`) |
| `make bundle` | Builds `xerxes_install.sh`, a single self-extracting script with the source in it. Copy it to a device and run `bash xerxes_install.sh` to unpack and build the backend in `./xerxes`. It doesn't include llama.cpp |
| `make clean` | Removes build output. Keeps the llama-server build, which is slow to redo |
| `make CXX=aarch64-linux-gnu-g++ TARGET=xerxes_backend_arm64` | Cross-compiles for arm64 from another architecture |

To rebuild llama-server from scratch, delete `third_party/llama.cpp/build` and run `make llama` again.

## Tests

| Command | What it does |
|---|---|
| `make test` | Builds and runs every test suite, then reports which (if any) failed |
| `make tests` | Builds the tests without running them |
| `make test_<name>` | Builds and runs one suite: `test_utils`, `test_newt`, `test_rfid_driver`, `test_rfid_manager` or `test_oled_manager` |

No hardware is needed. The RFID tests use a simulated module and the OLED tests check settings and the preview only.

## Running by hand

| Command | What it does |
|---|---|
| `./xerxes_backend` | Starts the panel on port 8000. Run it from the repo root |
| **Ctrl + C** | Stops it (in the terminal it was started in) |
| `XERXES_CONFIG_DIR=/some/folder ./xerxes_backend` | Saves settings (Newt, RFID, OLED) in that folder instead of `~/.xerxes` |

## Installing and the service

| Command | What it does |
|---|---|
| `sudo ./deploy_systemd.sh` | Builds everything and installs or updates the `xerxes` service in `/opt/xerxes`. Safe to re-run; keeps models and settings |
| `sudo apt install ./build/deb/xerxes-pi_*.deb` | Installs the package made by `make deb` |
| `bash xerxes_install.sh [folder]` | Unpacks and builds the bundle made by `make bundle` (default folder `./xerxes`) |
| `sudo systemctl status xerxes` | Shows whether the service is running (should say *active (running)*) |
| `sudo journalctl -u xerxes -f` | Follows the panel's log live. Press **Ctrl + C** to stop watching |
| `sudo journalctl -u xerxes -n 50` | Shows the last 50 log lines, e.g. after a failed start |
| `sudo systemctl restart xerxes` | Restarts the panel (this also stops any running Llama model) |
| `sudo systemctl stop xerxes` | Stops the panel, e.g. to run `./xerxes_backend` by hand |
| `sudo systemctl disable --now xerxes` | Stops it and stops it starting at boot |

## llama.cpp submodule

| Command | What it does |
|---|---|
| `git submodule update --init` | Downloads llama.cpp if the clone didn't |
| `git submodule status` | Shows whether llama.cpp is downloaded at the pinned release (see [setup.md](setup.md#step-1-clone-the-repo)) |
| `git submodule sync && git submodule update --init` | Switches an old clone from the Nozratt fork to the official repo |

How to move to a newer llama.cpp release is in [llama-cpp/commands.md](../llama-cpp/commands.md#update-llamacpp-to-a-newer-release).

## Checking the API

The panel is a set of REST endpoints under `/api/`. To check it is running:

```bash
curl http://127.0.0.1:8000/api/status
```

Other general endpoints:

| Command | What it returns |
|---|---|
| `curl http://127.0.0.1:8000/api/dashboard` | The values on the Dashboard cards |
| `curl http://127.0.0.1:8000/api/services` | Whether Samba (`smbd`), Tailscale and ngrok are running |
| `curl http://127.0.0.1:8000/api/netinfo` | Network interface, IP address, gateway and DNS servers |

Feature endpoints are listed in each feature's `commands.md`.
