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
- `include/`: Contains all header files (`.h`) defining the contracts between modules.
- `public/`: Contains the frontend assets (`index.html`, `styles.css`, and `containers.json`).

## Initial Setup (First Time Only)

### Prerequisites

1. Apple Silicon powered Mac or a Windows/Linux device based powered by an ARM chip (e.g. Qualcomm Snapdragon)
2. Debian 13 arm64 image https://www.debian.org/CD/http-ftp/
3. Virtual machine running Debian 13 arm64

### Step 1: Clone this Repo

```bash
git clone https://github.com/rapidanalysis/team-llama.git
```

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
make
```

### Running The Application

```bash
./xerxes-backend
```

### Stopping The Application

Select the terminal window `./xerxes-backend` was run in and press `control` + `c` on your keyboard.

### Removing Built Application
If changes have been made, run the following command then `make` the program again.

```bash
rm ./xerxes-backend
```


## Testing the API

**Check System Status:**
```bash
curl -X GET http://127.0.0.1:7272/
```

## Deploy as a systemd Service (Production)

To run the Xerxes Pi Admin Panel continuously in the background and ensure it starts automatically on device boot, you should deploy it as a native Linux `systemd` service.
Deployment process is written in `deploy_systemd.sh`.

1. Execute the Script
   Make it executable, and run it with root privileges:

```Bash
chmod +x deploy.sh
sudo ./deploy.sh
```

2. Verify the Service
   To confirm the backend is running successfully in the background, check the systemd status:

```Bash
sudo systemctl status xerxes
```

You should see active (running) highlighted in green.

3. Accessing the Admin Panel
   Once deployed, the C++ backend will automatically listen on Port 8080. You can access the panel via your web browser:

- Local Network (LAN): http://<DEVICE_IP_ADDRESS>:8080 (e.g., http://192.168.1.50:8080)
- Tailscale (Remote): http://<TAILSCALE_IP>:8080 (e.g., http://100.x.x.x:8080)
