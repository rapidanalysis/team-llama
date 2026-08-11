# Xerxes Pi Admin Backend 

This repository contains the software for the Xerxes Pi carrier board by Rapid Analysis PTY LTD. 

### Notes
- This setup guide is created based on the assumption that we are developing using Rust. 
- If needed, a C++ version will also be available as soon as Rob confirms.

## Architecture

* **Language:** Rust
* **Target Hardware:** ARM64 (Raspberry Pi OS / Debian 13)
* **Dev Environment:** Vagrant (Debian 13 Trixie)

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
*Note: The first time you run `vagrant up`, it will take a few minutes to download the Debian 13 image and automatically install the Rust toolchain, Docker, and Samba.*

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
Open the integrated terminal in VSCode (which is now securely inside the Debian VM) and run:

```bash
cargo run
```

###### ***Important Note on Compilation (for the nerds):*** To bypass a known VirtualBox file-locking bug (`os error 26`), this project uses a `.cargo/config.toml` file to force Rust to output its build artifacts to `/home/vagrant/xerxes-build-target` instead of the local workspace `target/` directory. You will not see a `target/` folder in the project root.

### Testing the API
Once the `cargo run` is executed, the server will listen on port `8080` inside the virtual machine. 

Vagrant forwards this to port `27272` on your host machine. You can open your standard Windows/Mac web browser or use `curl` to test the endpoints locally:

**Check System Status:**
```bash
curl -X GET http://127.0.0.1:27272/api/status
```


---

## Shutting Down
When you are done developing for the day, return to your host terminal (not the VSCode VM terminal) and run:

```bash
vagrant halt
```

## Cross-Compiling for Production (ARM64)
##### This will not be relevant until later stages of development. Skip this part for now.
The physical Xerxes Pi utilises a Raspberry Pi CM4/CM5, which runs on an ARM64 architecture. Because your development VM is x86_64, you cannot simply copy your local testing build to the hardware. 

To build the final, highly optimised executable for the actual hardware, you must cross-compile. From inside the `/workspace` directory in your `vagrant ssh` terminal, run:

```bash
cargo build --target aarch64-unknown-linux-gnu --release
```
Output Location: The compiled, deployment-ready binary will be located on the Linux VM at:
/home/vagrant/xerxes-build-target/aarch64-unknown-linux-gnu/release/xerxes_admin_panel
This safely powers down the VM and saves your computer's resources. Use `vagrant up` to instantly resume your work tomorrow.

## Notes
- If there is any enquiry about setting up or the development process in general, contact me in Discord or drop an email at thienphu.tran@students.mq.edu.au
