# llama.cpp Setup

This page describes how to get the local AI model feature (the **Llama** tab and the **Ask Xerxes AI** chat) working on a Xerxes Pi. The full build and install of the admin panel is in [admin-panel/setup.md](../admin-panel/setup.md); this page covers the llama.cpp parts of it.

## Requirements

- The `team-llama` repo, cloned **with its llama.cpp submodule** (see below).
- The build tools installed by `install_deps.sh`: a C++17 compiler, Make, CMake, Git and the libcurl development files.
- Memory: the Pi needs enough free RAM for the model you choose. The recommended model (Qwen2.5 0.5B, about 500 MB) runs on a 4 GB CM4.
- Disk space for models in the `models/` folder.
- Internet access to download models from Hugging Face.

## Step 1: Get llama.cpp

llama.cpp is included as a git submodule in `third_party/llama.cpp`. It is the official [ggml-org/llama.cpp](https://github.com/ggml-org/llama.cpp) repo, pinned to release **b11062** (commit `3cf0325`).

Clone with `--recurse-submodules` to get it at the same time:

```bash
git clone --recurse-submodules https://github.com/rapidanalysis/team-llama.git
cd team-llama
```

If you already cloned without it:

```bash
git submodule update --init
```

Check it with `git submodule status`. A line starting with a space and ending in `third_party/llama.cpp (b11062)` means it's ready. The full table of what each output means is in [admin-panel/setup.md](../admin-panel/setup.md#step-1-clone-the-repo).

## Step 2: Build llama-server

From the repo root:

```bash
make llama
```

This runs CMake on `third_party/llama.cpp` and builds only the `llama-server` program, at:

```text
third_party/llama.cpp/build/bin/llama-server
```

- **It's slow.** Expect an hour or more on a Raspberry Pi. It only needs doing once per llama.cpp release: `make llama` does nothing if llama-server is already built, and `make clean` doesn't delete it.
- **It uses one build job by default** so a 2–4 GB Pi doesn't run out of memory. On a machine with more memory, use e.g. `make llama LLAMA_JOBS=4`.
- If it fails with `third_party/llama.cpp is empty`, go back to Step 1.

Check it built:

```bash
third_party/llama.cpp/build/bin/llama-server --version
```

## Step 3: Build and run the panel

### On the device (recommended)

```bash
sudo ./deploy_systemd.sh
```

The deploy script now handles llama.cpp too. It:

- fetches the submodule if it's missing and runs `make llama` if llama-server isn't built yet;
- copies llama-server **and its shared libraries** to `/opt/xerxes/third_party/llama.cpp/build/bin/`, then checks the copy runs;
- creates `/opt/xerxes/models/` and copies in any `models/*.gguf` from the repo that aren't already deployed (models already on the device are kept);
- prints the Llama status and installed models when it finishes.

The `.deb` package from `make deb` includes llama-server in the same place. The self-extracting `xerxes_install.sh` from `make bundle` does **not**, so the Llama tab won't work if you install that way.

### By hand (development)

```bash
make
./xerxes_backend
```

Start it from the repo root: it looks for llama-server in `third_party/llama.cpp/build/bin/` and for models in `models/`, relative to the folder it was started in.

You never start llama-server yourself. The backend starts it when you turn on **Enable Llama**, and stops it when you turn it off or when the backend stops.

## Step 4: Get a model

Open the panel at `http://<device-ip>:8000` and click **Llama**. Then either:

- **Recommended models:** click **Download** next to a model. These have been tested on the Xerxes Pi.
- **Model source:** paste the link to a `.gguf` file on Hugging Face and click **Download**. The link must start with `https://huggingface.co/`. Both the file's page link (`.../blob/main/model.gguf`) and the direct link (`.../resolve/main/model.gguf`) work.

The download runs in the background on the Pi and can take several minutes. The model then appears under **Installed models**.

You can also copy `.gguf` files into the `models/` folder (`/opt/xerxes/models/` on a deployed device) by hand. The folder is created automatically by the first download or deploy.

## Step 5: Turn it on

1. Under **Installed models**, select the model.
2. Turn on **Enable Llama**. The badge shows **running**.
3. Wait a few seconds for the model to load, then click **Ask Xerxes AI** at the bottom right and send a message.

If the toggle turns itself off after a few seconds with *Llama stopped right after starting*, llama-server couldn't load the model: usually there isn't enough free memory (try a smaller model), or llama-server hasn't been built (run `make llama`).

## llama-server configuration

The backend starts llama-server with:

| Setting | Value |
| --- | --- |
| Host | `127.0.0.1` (only reachable from the Pi itself) |
| Port | `8081` |
| Context size | `4096` tokens |
| Model | `models/<the model you selected>` |

Because llama-server only listens on `127.0.0.1`, other devices reach it through the panel on port 8000, not directly.

## Verify the installation

```bash
curl http://127.0.0.1:8000/api/status          # the panel is up
curl http://127.0.0.1:8000/api/llama/status    # is llama-server running, and with which model
curl http://127.0.0.1:8000/api/llama/models    # installed models
```

`"running": false` is expected until you turn on **Enable Llama**: Llama is off by default so it uses no memory.

## CM4 Performance Validation

The local llama.cpp setup was benchmarked on the Xerxes Pi CM4 using the Qwen2.5 0.5B Instruct Q4_K_M model (the current recommended model).

<img width="1119" height="114" alt="Screenshot 2026-10-24 at 9 30 27 pm" src="https://github.com/user-attachments/assets/9a096358-b5fa-4394-ba54-d39c07727061" />

| Metric | Result |
|---|---:|
| Prompt processing | 21.44 ± 0.60 tokens/s |
| Token generation | 8.10 ± 0.14 tokens/s |
| Model size | 462.96 MiB |
| Parameters | 630.17M |
| Backend | CPU |
| Threads | 4 |

The benchmark confirms that local inference works on the target CM4 hardware, although generation speed is limited compared with desktop hardware. The benchmark was run on 24 October 2026, before llama.cpp was pinned to release b11062, so re-run it on the current build (and after any llama.cpp update).
