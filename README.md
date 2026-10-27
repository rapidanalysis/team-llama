# Xerxes Pi Admin Panel

A web-based admin panel for the Xerxes Pi carrier board by Rapid Analysis PTY LTD. From a browser you can check the device's status, change network settings, manage Docker containers, control file sharing and remote-access tunnels, and run a local AI model.

## Requirements

- A Xerxes Pi (Raspberry Pi CM4/CM5) running Raspberry Pi OS or Debian 12/13, 64-bit
- An account that can use `sudo`
- An internet connection for the first install
- About 2 GB of free disk space, plus room for any AI models you download

## Install

Run these commands on the Xerxes Pi itself.

**1. Get the code**

```bash
git clone --recurse-submodules https://github.com/rapidanalysis/team-llama/tree/developG40
cd XerxesAdminPanel
```

**2. Run the installer**

```bash
sudo ./deploy_systemd.sh
```

The installer:

1. installs any missing system packages
2. builds the AI model server (llama.cpp). **The first time, this can take an hour or more on a Pi.**
3. builds the admin panel
4. installs everything to `/opt/xerxes`
5. starts the panel as a background service that also starts automatically when the device boots

When it finishes, it prints the address of the panel:

```
==========================================
 Deployment complete
 Admin panel:  http:/<device-ip>:8000
 ...
```

**3. Open the panel**

Go to `http://<device-ip>:8000` in a browser on any computer on the same network. If the device is on Tailscale, `http://<tailscale-ip>:8000` works from anywhere.

> The panel has no login. Anyone who can reach port 8000 can control the device, so only use it on a network you trust or over Tailscale. Don't expose port 8000 to the internet.

## Using the AI assistant

The AI model is off by default and uses no memory until you turn it on.

1. Open the **Llama** tab.
2. Under **Model source**, paste the download link of a `.gguf` model from Hugging Face and click **Download**. A small model is the best start, for example:
   ```
   https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF/resolve/main/qwen2.5-0.5b-instruct-q4_k_m.gguf
   ```
3. Select the model under **Installed models**, then turn on **Enable Llama**.
4. Click **Ask Xerxes AI** in the bottom-right corner of any page to chat.

Pick a model that fits the device's memory: roughly its file size plus a few hundred MB. On a 4 GB board, stick to models under about 2 GB. Turn Llama off when you're done to free the memory.

Programs on your network can also use the model through an OpenAI-compatible API at `http://<device-ip>:8000/api/llama/v1/chat/completions`.

## Managing the service

| To | Run |
|---|---|
| Check it's running | `sudo systemctl status xerxes` |
| See live logs | `sudo journalctl -u xerxes -f` |
| Restart it | `sudo systemctl restart xerxes` |
| Stop it | `sudo systemctl stop xerxes` |
| Stop it starting at boot | `sudo systemctl disable xerxes` |

## Updating

Pull the latest code and run the installer again. It rebuilds only what changed and keeps your downloaded models.

```bash
git pull --recurse-submodules
sudo ./deploy_systemd.sh
```

## Running it by hand (for development)

To try out code changes without installing, build and run from the project folder. Stop the service first, because both use port 8000.

```bash
sudo systemctl stop xerxes   # if installed
make llama                   # first time only; takes a long time
make
./xerxes_backend
```

Always start `./xerxes_backend` from the project folder: it looks for the AI model server and the `models/` folder relative to where it's started. Press `Ctrl+C` to stop it.

Models you download while running by hand go into the project's `models/` folder. The installer copies them to `/opt/xerxes/models` the next time it runs.

## Uninstalling

```bash
sudo systemctl disable --now xerxes
sudo rm /etc/systemd/system/xerxes.service
sudo systemctl daemon-reload
sudo rm -rf /opt/xerxes        # also deletes downloaded models
```

## Troubleshooting

**The installer says port 8000 is already in use.** Another copy of the panel is running, usually one started by hand with `./xerxes_backend`. Stop it with `Ctrl+C` and run the installer again.

**The page doesn't load.** Check the service is running with `sudo systemctl status xerxes`, and look at `sudo journalctl -u xerxes -n 50` for errors. Make sure you're using port 8000 and the device's current IP address.

**Llama turns itself off right after starting.** The model is too large for the free memory, or the file is damaged or only partly downloaded. Try a smaller model, or delete the file and download it again. The logs (`sudo journalctl -u xerxes -n 50`) show the exact reason.

**The AI assistant says Llama is turned off.** Turn it on in the **Llama** tab first.

