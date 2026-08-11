# Xerxes Pi Admin Backend 

This repository contains the software for the Xerxes Pi carrier board by Rapid Analysis PTY LTD. 

### Notes
- This setup guide is created using C++. 
- If needed, a Rust version will also be available at `legacy/` folder.

## Architecture

* **Language:** C++
* **Target Hardware:** ARM64 (Raspberry Pi OS / Debian 13)
* **Dev Environment:** Vagrant (Debian 13 Trixie)

---
## Repository Structure

To maintain a clean codebase while still compiling to a single executable, the project is divided into modules:

* `src/`: Contains all implementation files (`.cpp`) for routing, Docker management, and system wrappers.
* `include/`: Contains all header files (`.h`) defining the contracts between modules.
* `public/`: Contains the frontend assets (`index.html`, `styles.css`, and `containers.json`).

---

## Initial Setup (First Time Only)

### Prerequisites
Before cloning this repository, ensure your host machine (Windows/Mac) has the following installed:
1. [VirtualBox](https://www.virtualbox.org/wiki/Downloads)
2. [Vagrant](https://developer.hashicorp.com/vagrant/downloads) (Verify via terminal: `vagrant --version`)

### Step 1: Clone and Boot
Clone the repository to your local machine and boot the environment.

```bash
git clone https://github.com/PaulTr27/XerxesAdminPanel
cd XerxesAdminPanel
vagrant up
```
*Note: The first time you run `vagrant up`, it will take a few minutes to download the Debian 13 image and automatically install the C++ toolchain, Docker, and Samba.*

### Step 2: Open and Connect
Because Vagrant automatically syncs this folder, you can edit the code natively on your host machine but compile it safely inside the Linux VM.

1. Open the local `XerxesAdminPanel` folder in VSCode (or your favourite IDE).
2. Open the integrated terminal in VSCode.
3. Securely log into the virtual machine by running:
   ```bash
   vagrant ssh
   ```
4. Once inside the VM, navigate to the synced workspace:
   ```bash
   cd /workspace
   ```

---

## Development Workflow

### Compiling and Running
From inside the `/workspace` directory in your `vagrant ssh` terminal, compile all modules into a single binary using Make:

```bash
make
```

Once compiled, start the server by executing the output binary:

```bash
./xerxes_backend
```


### Testing the API
Once the `./xerxes_backend` is executed, the server will listen on port `8080` inside the virtual machine. 

Vagrant forwards this to port `7272` on your host machine. You can open your standard Windows/Mac web browser or use `curl` to test the endpoints locally:

**Check System Status:**
```bash
curl -X GET http://127.0.0.1:7272/
```


---

## Shutting Down
When you are done developing for the day, stop the server (`Ctrl+C`), type `exit` to leave the SSH session, then safely power down the VM by running:

```bash
vagrant halt
```

## Cross-Compiling for Production (ARM64)
##### ***Notes***: This will not be relevant until later stages of development. Skip this part for now.
The physical Xerxes Pi utilises a Raspberry Pi CM4/CM5, which runs on an ARM64 architecture. Because your development VM simulates x86_64, you cannot simply copy your local testing build to the hardware. 

To build the final executable for the actual hardware, you must cross-compile. We can override the default compiler in our Makefile by passing the ARM64 GCC compiler:

```bash
make CXX=aarch64-linux-gnu-g++ TARGET=xerxes_backend_arm64
```

You can then securely copy the resulting `xerxes_backend_arm64` file directly to your physical Xerxes Pi board for deployment.

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

- Vagrant Host (if port-forwarded): http://localhost:7272 (or any port configured in Vagrantfile)

- Tailscale (Remote): http://<TAILSCALE_IP>:8080 (e.g., http://100.x.x.x:8080)

## Notes
- There will be some differences between MacBooks and Windows machines in the setup process. If this setup guide does not work for you, contact me immediately so we can all progress.
- If there is any enquiry about setting up or the development process in general, contact me in Discord or drop an email at thienphu.tran@students.mq.edu.au


