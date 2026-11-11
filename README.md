# Xerxes Pi Admin Backend

This repository contains the software for the Xerxes Pi carrier board by Rapid Analysis PTY LTD.

### Notes

- A Xerxes Pi (Raspberry Pi CM4/CM5) running Raspberry Pi OS or Debian 13
- The `root` user setup on the machine
- Internet connection for the first install
- About 2 GB of free disk space, plus room for any AI models you download

- **Language:** C++
- **Target Hardware:** ARM64 (Raspberry Pi OS / Debian 13)
- **Dev Environment:** Debian 13 Trixie (ARM64)

## Repository Structure

To maintain a clean codebase while still compiling to a single executable, the project is divided into modules:

- `src/`: Contains all implementation files (`.cpp`) for routing, Docker management, and system wrappers.
- `src/rfid/`: The RFID reader for the PiicoDev RFID module on I2C, managed from the **I2C** tab.
- `src/oled/`: The OLED display, also managed from the **I2C** tab.
- `include/`: Contains all header files (`.h`) defining the contracts between modules.
- `public/`: Contains the frontend assets (`index.html`, `app.js`, `i18n.js`, `styles.css`, `containers.json`, `llama-models.json`, and translations in `i18n/`).
- `packaging/`: The systemd unit (`xerxes.service`) shared by `deploy_systemd.sh` and `make deb`, plus the files for `make deb` and `make bundle`.
- `third_party/llama.cpp/`: The llama.cpp source, as a git submodule. Built into `llama-server`, which the backend starts for the **Llama** tab.

## Initial Setup (First Time Only)

The full process on a new machine is:

1. [Clone the repo](#step-1-clone-this-repo) along with llama.cpp
2. [Install dependencies](#step-2-install-dependencies)
3. [Build llama-server](#step-3-build-llama-server)
4. [Build xerxes_backend](#step-4-build-xerxes_backend)
5. [Run it](#running-the-application) by hand while developing, or [deploy it as a service](#deploy-as-a-systemd-service-production) on the device

`deploy_systemd.sh` does steps 3 and 4 itself, so on a device you can go straight from step 2 to the deploy.

### Prerequisites

1. Apple Silicon powered Mac or a Windows/Linux device based powered by an ARM chip (e.g. Qualcomm Snapdragon)
2. Debian 13 arm64 image https://www.debian.org/CD/http-ftp/
3. Virtual machine running Debian 13 arm64

### Step 1: Clone this Repo

`--recurse-submodules` also downloads llama.cpp into `third_party/llama.cpp`:

```bash
git clone --recurse-submodules https://github.com/rapidanalysis/team-llama.git
cd team-llama
```

If you already cloned without it, fetch llama.cpp now:

```bash
git submodule update --init
```
**Note:** `third_party/llama.cpp` is the official [ggml-org/llama.cpp](https://github.com/ggml-org/llama.cpp) repo, pinned to release `b11062` (commit `3cf0325`). 

### Step 2: Install Dependencies

`install_deps.sh` installs the build tools (`g++`, `make`, `cmake`, `git`), libcurl, Docker, Samba, Tailscale and ngrok, and downloads the two header-only libraries the backend needs (`httplib.h` and `json.hpp`) to `/usr/local/include`.

As a user with `sudo`:

```bash
bash install_deps.sh
```

Or as root, naming the user you develop as so they're added to the `docker` and `sudo` groups:

```bash
su -c "bash install_deps.sh --user <your-username>"
```

Once this finishes, restart the VM (or log out and back in) so the group changes apply.

### Step 3: Build llama-server

From the repo root:

```bash
make llama
```

This runs CMake on `third_party/llama.cpp` and builds only the `llama-server` target, into `third_party/llama.cpp/build/bin/`. The backend starts it from that exact path, relative to the directory the backend runs in.

- **This is slow.** Expect an hour or more on a Raspberry Pi. It only needs doing once per llama.cpp release: `make llama` does nothing if `llama-server` is already built.
- If it fails with `third_party/llama.cpp is empty`, the submodule isn't downloaded. Go back to [Step 1](#step-1-clone-this-repo).

Check it built:

```bash
third_party/llama.cpp/build/bin/llama-server --version
```

`make clean` doesn't delete this build. To rebuild llama-server from scratch, delete `third_party/llama.cpp/build` and run `make llama` again.

### Step 4: Build xerxes_backend

```bash
make
```

This compiles everything in `src/`, `src/rfid/` and `src/oled/` into `build/obj/` and links `./xerxes_backend`. After the first build, `make` only recompiles files that changed, including files that use a header you edited. Use `make -j4` to compile several files at once.

If it fails with `httplib.h` or `json.hpp: No such file or directory`, run [Step 2](#step-2-install-dependencies) first.

## Development Workflow

Work from the repo root (`cd team-llama`).

### Building The Application

```bash
make            # builds ./xerxes_backend; only rebuilds what changed
make llama      # builds llama-server for the Llama tab (an hour or more on a Pi; only needed once)
```

Other build targets:

| Command | What it makes |
|---|---|
| `make static` | `./xerxes_backend` with libgcc and libstdc++ linked in, so it doesn't depend on the device's C++ runtime version (libcurl and glibc still come from the device) |
| `make deb` | `build/deb/xerxes-pi_<version>_<arch>.deb`. Installs the same files as `deploy_systemd.sh` (backend, `public/`, llama-server and the `xerxes` service) and starts the service. Install with `sudo apt install ./build/deb/xerxes-pi_*.deb`. Set the version with `make deb DEB_VERSION=0.3.0` |
| `make bundle` | `xerxes_install.sh`, a single self-extracting script with the source in it. Copy it to a device and run `bash xerxes_install.sh` to unpack and build the backend in `./xerxes`. It doesn't include llama.cpp |
| `make clean` | Removes build output. Keeps the llama-server build, which is slow to redo |

To cross-compile: `make CXX=aarch64-linux-gnu-g++ TARGET=xerxes_backend_arm64`.

### Running The Application

```bash
./xerxes_backend
```

The panel is at `http://<device-ip>:8000`. Start it from the repo root: it serves `public/`, starts `llama-server` from `third_party/llama.cpp/build/bin/` and keeps models in `models/`, all relative to the directory it's started in.

If it says `Could not listen on port 8000`, the deployed service is probably running. Stop it first with `sudo systemctl stop xerxes`.

### Stopping The Application

Select the terminal window `./xerxes_backend` was run in and press `control` + `c` on your keyboard.

### Running The Tests

```bash
make test            # build and run every test (the RFID tests use a simulated module, no hardware needed)
make test_newt       # build and run one test
```

### RFID Reader (I2C tab)

Enable I2C first: `sudo raspi-config` → *Interface Options* → *I2C* → *Yes*, then reboot. Tags and settings are saved in `~/.xerxes/` (`/root/.xerxes/` when deployed with systemd). Tag commands run as the backend's user, which is root when deployed.

### Recommended Llama models

The **Recommended models** list on the Llama tab comes from `public/llama-models.json`. Once a model has been tested and runs well on the Xerxes Pi, add an entry there — no code change or rebuild needed:

```json
{
  "name": "Qwen2.5 0.5B Instruct (Q4_K_M)",
  "url": "https://huggingface.co/<user>/<repo>/resolve/main/<file>.gguf",
  "size": "491 MB",
  "note": { "en": "...", "es": "..." }
}
```

`size` and `note` are optional, and a missing translation falls back to `en`. A `/blob/` link copied from the Hugging Face page also works.

### Languages (i18n)

The panel's language is picked from the dropdown in the top bar (English, Español). The choice is saved in the browser; first-time visitors get their browser's language if it is supported, otherwise English.

- Strings live in `public/i18n/<code>.json`. `en.json` is the source of truth and the fallback for any missing key.
- In HTML, tag text with `data-i18n="key"` (or `data-i18n-placeholder`, `-title`, `-aria-label`). In JS, use `I18N.setText(el, "key", {param: value})` or `msg("key", params)` with `showFeedback()`, so the text re-translates when the language changes. See the header of `public/i18n.js`.
- To add a language: copy `en.json` to `<code>.json`, translate the values (keep `{placeholders}` and `<code>` tags), and add it to `LANGUAGES` in `public/i18n.js`.

Command output and error messages that come from the backend itself are shown as-is (English).

## Testing the API

**Check System Status:**
```bash
curl http://127.0.0.1:8000/api/status
```

## Deploy as a systemd Service (Production)

`deploy_systemd.sh` builds everything and installs the panel as the `xerxes` systemd service, which runs in the background and starts on boot.

### Before you deploy

1. Clone the repo with llama.cpp ([Step 1](#step-1-clone-this-repo)). If the submodule is missing, the script tries to fetch it, which needs git access to GitHub.
2. Run `install_deps.sh` ([Step 2](#step-2-install-dependencies)). The script installs missing apt packages itself, but not `httplib.h` and `json.hpp`.
3. For the RFID reader and OLED display, enable I2C (see [RFID Reader](#rfid-reader-i2c-tab)). The rest of the panel works without it.
4. Stop any `./xerxes_backend` you started by hand, since the service needs port 8000.

### Run it

From anywhere, as root via `sudo`:

```bash
sudo ./deploy_systemd.sh
```

Run it with `sudo` from your normal user, not from a root shell. The script builds as the user who ran `sudo`, so files in the repo stay owned by you.

### What it does

| Step | What happens |
|---|---|
| **[1/7] Checking system packages** | Installs any of `g++ make cmake git curl libcurl4-openssl-dev iproute2` that are missing. Stops if `httplib.h` or `json.hpp` can't be found, and warns (without stopping) if there's no `/dev/i2c-*` |
| **[2/7] Checking llama.cpp** | Runs `git submodule update --init --recursive` if `third_party/llama.cpp` is empty, then `make llama` if `llama-server` isn't built yet. **The first deploy on a Pi can take an hour or more here**; later deploys skip it |
| **[3/7] Building xerxes_backend** | Runs `make`, which only recompiles what changed |
| **[4/7] Stopping the existing service** | Stops `xerxes` (and the llama-server it started), so the files can be replaced. Stops with an error if something else is still using port 8000 |
| **[5/7] Installing to /opt/xerxes** | Copies `xerxes_backend`, replaces `public/` (so deleted files don't linger), copies `llama-server` and its libraries, and copies any `models/*.gguf` that aren't already deployed. Then checks the deployed `llama-server` has all its libraries and runs |
| **[6/7] Installing the service file** | Copies `packaging/xerxes.service` to `/etc/systemd/system/xerxes.service` |
| **[7/7] Starting the service** | Enables and restarts `xerxes`, waits up to 10 seconds for `/api/status` to answer, then prints the panel address, Llama status and installed models |

The deployed layout mirrors the repo, because the backend finds everything relative to `/opt/xerxes`:

```
/opt/xerxes/
├── xerxes_backend
├── public/
├── models/                                  downloaded models (kept across deploys)
└── third_party/llama.cpp/build/bin/         llama-server and its .so libraries
```

The service runs as root (needed for Docker, I2C and system commands), so settings for Newt, RFID tags and the OLED are saved in `/root/.xerxes`.

### Updating a deployed device

```bash
git pull
git submodule update     # only does anything if the llama.cpp pin changed
sudo ./deploy_systemd.sh
```

It's safe to re-run as often as you like. It rebuilds only what changed and keeps models and settings. If the llama.cpp pin changed, delete `third_party/llama.cpp/build` before deploying so llama-server is rebuilt at the new release.

To install on a device without building there, use `make deb` on a build machine of the same architecture and install the `.deb` instead.

### Check the service

```bash
sudo systemctl status xerxes    # should show active (running)
sudo journalctl -u xerxes -f    # live logs
```

Open the panel at:

- Local network (LAN): `http://<DEVICE_IP_ADDRESS>:8000` (e.g. `http://192.168.1.50:8000`)
- Tailscale (remote): `http://<TAILSCALE_IP>:8000` (e.g. `http://100.x.x.x:8000`)

### If the deploy fails

| Message | Fix |
|---|---|
| `Please run this script as root` | Run it with `sudo ./deploy_systemd.sh` |
| `httplib.h not found` / `json.hpp not found` | Run `sudo bash install_deps.sh`, then deploy again |
| `third_party/llama.cpp is empty` | The submodule couldn't be fetched. See the `git submodule status` table in [Step 1](#step-1-clone-this-repo) |
| `Port 8000 is already in use` | The message shows which program has it. Stop it (usually a `./xerxes_backend` in another terminal) and deploy again |
| `llama-server is missing libraries` / `doesn't run` | The llama.cpp build is incomplete. Delete `third_party/llama.cpp/build`, run `make llama`, then deploy again |
| `The service started but isn't answering` | See why with `sudo journalctl -u xerxes -n 50` |

### Removing the service

```bash
sudo systemctl disable --now xerxes
sudo rm /etc/systemd/system/xerxes.service
sudo systemctl daemon-reload
sudo rm -rf /opt/xerxes         # also deletes downloaded models
```

Settings in `/root/.xerxes` (including the Newt secret) are left behind. Delete that folder too if you want them gone.
