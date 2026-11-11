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
- `src/rfid/`: The RFID reader (FR-12) for the PiicoDev RFID module on I2C, managed from the **I2C** tab. See `src/rfid/README.md`.
- `include/`: Contains all header files (`.h`) defining the contracts between modules.
- `public/`: Contains the frontend assets (`index.html`, `app.js`, `i18n.js`, `styles.css`, `containers.json`, `llama-models.json`, and translations in `i18n/`).

## Initial Setup (First Time Only)

### Prerequisites

1. Apple Silicon powered Mac or a Windows/Linux device based powered by an ARM chip (e.g. Qualcomm Snapdragon)
2. Debian 13 arm64 image https://www.debian.org/CD/http-ftp/
3. Virtual machine running Debian 13 arm64

### Step 1: Clone this Repo

```bash
git clone --recurse-submodules https://github.com/rapidanalysis/team-llama.git
```

If you already cloned without submodules, run `git submodule update --init` to fetch `third_party/llama.cpp`.

`third_party/llama.cpp` is the official [ggml-org/llama.cpp](https://github.com/ggml-org/llama.cpp) repo, pinned to release **b11062**. If you cloned before it moved off the Nozratt fork, run `git submodule sync && git submodule update --init` once to switch over.

**Updating llama.cpp** to another [release](https://github.com/ggml-org/llama.cpp/releases), e.g. `b12119`:

```bash
cd third_party/llama.cpp
git fetch --tags origin && git checkout b12119
cd ../..
git add third_party/llama.cpp   # then update the tag in .gitmodules' comment and above
```

Rebuild llama-server afterwards and retest the recommended models on the Xerxes Pi.

### Step 2: Install Dependencies

As root user `su`  
```bash
bash install_deps.sh
```
Once this finishes, restart the VM


## Development Workflow
`cd` into `/team-lama` then do the following:  

### Building The Application

```bash
make            # builds ./xerxes_backend; only rebuilds what changed
make llama      # builds llama-server for the Llama tab (an hour or more on a Pi)
```

Other build targets:

| Command | What it makes |
|---|---|
| `make static` | `./xerxes_backend` with libgcc and libstdc++ linked in, so it doesn't depend on the device's C++ runtime version (libcurl and glibc still come from the device) |
| `make deb` | `build/deb/xerxes-pi_<version>_<arch>.deb`. Installs the same files as `deploy_systemd.sh` (backend, `public/`, llama-server and the `xerxes` service) and starts the service. Install with `sudo apt install ./build/deb/xerxes-pi_*.deb`. Set the version with `make deb DEB_VERSION=0.3.0` |
| `make bundle` | `xerxes_install.sh`, a single self-extracting script with the source in it. Copy it to a device and run `bash xerxes_install.sh` to unpack and build the backend in `./xerxes`. It doesn't include llama.cpp |
| `make clean` | Removes build output. Keeps the llama-server build, which is slow to redo |

To cross-compile: `make CXX=aarch64-linux-gnu-g++ TARGET=xerxes_backend_arm64`. A machine with more memory can build llama.cpp faster with `make llama LLAMA_JOBS=4`.

### Running The Application

```bash
./xerxes_backend
```

The panel is at `http://<device-ip>:8000`. Start it from the repo root: it serves `public/` and keeps models in `models/` relative to the directory it's started in.

### Stopping The Application

Select the terminal window `./xerxes_backend` was run in and press `control` + `c` on your keyboard.

### Running The Tests

```bash
make test            # build and run every test (the RFID tests use a simulated module, no hardware needed)
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
curl -X GET http://127.0.0.1:8000/
```

## Deploy as a systemd Service (Production)

To run the Xerxes Pi Admin Panel in the background and start it on boot, deploy it as the `xerxes` systemd service:

```bash
sudo ./deploy_systemd.sh
```

The script:

1. Installs any missing build packages, and checks for `httplib.h` and `json.hpp` (run `install_deps.sh` first if they're missing)
2. Fetches the llama.cpp submodule and builds llama-server if it isn't built yet
3. Builds `xerxes_backend`, as the user who ran `sudo`, so the repo doesn't end up owned by root
4. Stops the running service and checks port 8000 is free
5. Installs the backend, `public/` and llama-server to `/opt/xerxes`, and copies over any models in `models/` (existing ones are kept)
6. Installs the unit from `packaging/xerxes.service`, starts the service and checks it answers

It's safe to re-run after pulling changes. Settings (Newt, RFID tags, OLED) are kept in `/root/.xerxes`, and models in `/opt/xerxes/models`.

To install on a device without building there, use `make deb` on a build machine of the same architecture and install the `.deb` instead.

Check the service:

```bash
sudo systemctl status xerxes    # should show active (running)
sudo journalctl -u xerxes -f    # live logs
```

Open the panel at:

- Local network (LAN): `http://<DEVICE_IP_ADDRESS>:8000` (e.g. `http://192.168.1.50:8000`)
- Tailscale (remote): `http://<TAILSCALE_IP>:8000` (e.g. `http://100.x.x.x:8000`)
