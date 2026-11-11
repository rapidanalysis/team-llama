# Admin Panel: Setup

This page gets the Xerxes Pi Admin Panel built and running, either by hand while developing or as a service on the device. Every feature documented in this repo (Newt, llama.cpp, RFID, OLED, languages) is part of the same program, so this is the only build you need.

The code lives in the `team-llama` repo. These steps match its `developG39` branch.

## What you need

| Item | Notes |
| --- | --- |
| A Xerxes Pi (Raspberry Pi CM4 or CM5) | Running Raspberry Pi OS or Debian 13 (Trixie), 64-bit (arm64) |
| The `root` user set up | On Raspberry Pi OS, set a root password with `sudo passwd root` |
| Internet access for the first install | Wi-Fi is recommended: Ethernet on the Xerxes Pi board has been unreliable |
| About 2 GB of free disk space | Plus room for any AI models you download (the recommended one is about 500 MB) |
| Another computer | To connect to the Pi over SSH (or Raspberry Pi Connect) and open the panel in a browser |

### Developing without a Pi

You can develop on a virtual machine instead of a Pi. You need:

1. An Apple Silicon Mac, or a Windows/Linux computer with an ARM processor (e.g. Qualcomm Snapdragon).
2. The Debian 13 arm64 image from https://www.debian.org/CD/http-ftp/.
3. A virtual machine running that image.

The VM must be arm64 so that what you build matches the Pi. The I2C devices (RFID and OLED) can't be used on a VM, but the rest of the panel works.

## The full process

1. [Clone the repo](#step-1-clone-the-repo) along with llama.cpp
2. [Install the dependencies](#step-2-install-the-dependencies)
3. [Build llama-server](#step-3-build-llama-server)
4. [Build xerxes_backend](#step-4-build-xerxes_backend)
5. [Run it by hand](#step-5a-run-it-by-hand-development) while developing, **or** [deploy it as a service](#step-5b-deploy-it-as-a-service-production) on the device

`deploy_systemd.sh` does steps 3 and 4 itself, so on a device you can go straight from step 2 to step 5b.

## Step 1: Clone the repo

`--recurse-submodules` also downloads llama.cpp into `third_party/llama.cpp`:

```bash
git clone --recurse-submodules https://github.com/rapidanalysis/team-llama.git
cd team-llama
```

If you already cloned without it, fetch llama.cpp now:

```bash
git submodule update --init
```

Check it worked:

```bash
git submodule status
```

| Output | Meaning |
|---|---|
| ` 3cf0325… third_party/llama.cpp (b11062)` (leading space; the bracket may show the commit instead of the tag) | llama.cpp is downloaded at the pinned release. You're done |
| `-3cf0325… third_party/llama.cpp` (leading `-`) | Not downloaded yet. Run `git submodule update --init` |
| `+…` (leading `+`) | Checked out at a different commit than the repo pins. Run `git submodule update` to go back to the pinned one |
| Nothing at all | Your copy of the repo doesn't record the submodule (older `developG39` commits didn't). Run `git pull`, then `git submodule update --init` |

If you cloned before llama.cpp moved from the Nozratt fork to the official repo, switch over once with:

```bash
git submodule sync
git submodule update --init
```

## Step 2: Install the dependencies

`install_deps.sh` installs:

- the build tools (`build-essential`, `cmake`, `git`, `curl`) and libcurl
- Docker (needed by Newt and the Docker tab) and Samba
- Tailscale and ngrok
- the two header-only libraries the backend is built with, `httplib.h` and `json.hpp`, into `/usr/local/include`

As a user with `sudo`:

```bash
bash install_deps.sh
```

Or as root, naming the user you develop as so they're added to the `docker` and `sudo` groups:

```bash
su -c "bash install_deps.sh --user <your-username>"
```

When it finishes, restart the VM or Pi (or log out and back in) so the group changes apply.

## Step 3: Build llama-server

llama-server runs the AI models for the **Llama** tab. From the repo root:

```bash
make llama
```

This builds only the `llama-server` part of llama.cpp, into `third_party/llama.cpp/build/bin/`.

- **It's slow.** Expect an hour or more on a Raspberry Pi. It only needs doing once per llama.cpp release: `make llama` does nothing if `llama-server` is already built.
- **It builds with one job by default**, so a 2–4 GB Pi doesn't run out of memory. On a VM or a computer with more memory, speed it up with e.g. `make llama LLAMA_JOBS=4`.
- If it fails with `third_party/llama.cpp is empty`, the submodule isn't downloaded. Go back to [Step 1](#step-1-clone-the-repo).

Check it built:

```bash
third_party/llama.cpp/build/bin/llama-server --version
```

## Step 4: Build xerxes_backend

```bash
make
```

This compiles everything in `src/`, `src/rfid/` and `src/oled/` into one program, `./xerxes_backend`. After the first build, `make` only recompiles files that changed. Use `make -j4` to compile several files at once.

If it fails with `httplib.h` or `json.hpp: No such file or directory`, run [Step 2](#step-2-install-the-dependencies) first.

Optionally, check everything works:

```bash
make test
```

The tests need no hardware: the RFID tests use a simulated module.

## Step 5a: Run it by hand (development)

From the repo root:

```bash
./xerxes_backend
```

Open the panel at `http://<device-ip>:8000` (or `http://localhost:8000` on the same machine). Stop it by pressing **Ctrl + C** in the terminal it's running in.

Always start it from the repo root. It serves the web pages from `public/`, starts llama-server from `third_party/llama.cpp/build/bin/`, and keeps models in `models/`, all relative to the folder it was started in.

If it says `Could not listen on port 8000`, the deployed service is probably running. Stop it first with `sudo systemctl stop xerxes`.

## Step 5b: Deploy it as a service (production)

`deploy_systemd.sh` builds everything and installs the panel as the `xerxes` system service, which runs in the background and starts by itself when the Pi boots.

### Before you deploy

1. Clone the repo with llama.cpp ([Step 1](#step-1-clone-the-repo)). If the submodule is missing, the script tries to fetch it, which needs access to GitHub.
2. Run `install_deps.sh` ([Step 2](#step-2-install-the-dependencies)). The script installs any missing apt packages itself, but not `httplib.h` and `json.hpp`.
3. For the RFID reader and OLED display, enable I2C: `sudo raspi-config` → *Interface Options* → *I2C* → *Yes*, then reboot. The rest of the panel works without it.
4. Stop any `./xerxes_backend` you started by hand, since the service needs port 8000.

### Run it

From your normal user (not a root shell):

```bash
sudo ./deploy_systemd.sh
```

The script builds as the user who ran `sudo`, so files in the repo stay owned by you. **The first deploy on a Pi can take an hour or more** while llama-server is built; later deploys skip that.

When it finishes it prints the panel's address, whether Llama is available, and any installed models. Open the panel at:

- Local network: `http://<DEVICE_IP_ADDRESS>:8000` (e.g. `http://192.168.1.50:8000`)
- Tailscale (remote): `http://<TAILSCALE_IP>:8000` (e.g. `http://100.x.x.x:8000`)

### If the deploy fails

| Message | Fix |
|---|---|
| `Please run this script as root` | Run it with `sudo ./deploy_systemd.sh` |
| `httplib.h not found` / `json.hpp not found` | Run `sudo bash install_deps.sh`, then deploy again |
| `third_party/llama.cpp is empty` | The submodule couldn't be fetched. See the `git submodule status` table in [Step 1](#step-1-clone-the-repo) |
| `Port 8000 is already in use` | The message shows which program has it. Stop it (usually a `./xerxes_backend` in another terminal) and deploy again |
| `llama-server is missing libraries` / `doesn't run` | The llama.cpp build is incomplete. Delete `third_party/llama.cpp/build`, run `make llama`, then deploy again |
| `The service started but isn't answering` | See why with `sudo journalctl -u xerxes -n 50` |

### Installing without building on the device

Two other ways to install, both described in [commands.md](commands.md#build-targets):

- **`.deb` package** (`make deb`): built on another machine of the same architecture, then installed with `sudo apt install ./xerxes-pi_*.deb`. Includes llama-server and starts the service.
- **Self-extracting script** (`make bundle`): one file you copy to the device and run with `bash xerxes_install.sh`. It builds the backend there, but does **not** include llama.cpp, so the Llama tab won't work.

## Updating a deployed device

```bash
git pull
git submodule update     # only does anything if the llama.cpp pin changed
sudo ./deploy_systemd.sh
```

It's safe to re-run as often as you like. It rebuilds only what changed and keeps downloaded models and settings. If the llama.cpp pin changed, delete `third_party/llama.cpp/build` before deploying so llama-server is rebuilt at the new release.

## Removing the service

```bash
sudo systemctl disable --now xerxes
sudo rm /etc/systemd/system/xerxes.service
sudo systemctl daemon-reload
sudo rm -rf /opt/xerxes         # also deletes downloaded models
```

Settings in `/root/.xerxes` (including the Newt secret and RFID tags) are left behind. Delete that folder too if you want them gone.
