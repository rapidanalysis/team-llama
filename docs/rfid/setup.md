# RFID Setup

The RFID reader lets you tap an RFID tag on the Xerxes Pi to run a command, for example `apt update`. It is now built into the admin panel and managed from the **I2C** tab, so there is no separate program or Python to install.

## Requirements
- Xerxes Pi V2
- Raspberry Pi Compute Module 4 or 5 with at least 4GB of RAM
- PiicoDev RFID Module, connected to the Xerxes Pi with a PiicoDev (Qwiic) cable
- RFID tags (Mifare Classic or NTAG213/NTAG203, 13.56MHz)
- Internet access for the first install (Wi-Fi is recommended)
- Another computer to SSH into the Xerxes Pi (or connect via Raspberry Pi Connect) and to open the admin panel

## Setup
*All commands below are typed into the Xerxes Pi's terminal (over SSH or Raspberry Pi Connect).*

**Connect the module**  
With the Pi turned off, connect the PiicoDev RFID Module to the Xerxes Pi's I2C (Qwiic) port. It can be daisy-chained with the OLED display.

**Enable I2C**  
`sudo raspi-config`  
Then select *Interface Options* → *I2C* → *Yes*, exit, and reboot with `sudo reboot`

**Check the module is detected (optional)**  
`sudo apt install i2c-tools`  
`i2cdetect -y 1`  
The RFID module appears as `2c` in the table (the OLED display, if connected, as `3c`).

**Install the admin panel**  
Follow [admin-panel/setup.md](../admin-panel/setup.md). On the Xerxes Pi this is, in short:  
`git clone --recurse-submodules https://github.com/rapidanalysis/team-llama.git`  
`cd team-llama`  
`bash install_deps.sh`  
`sudo ./deploy_systemd.sh`

**Open the RFID reader**  
1. In a browser, open `http://<pi-address>:8000` and click the **I2C** tab.
2. Under **RFID Reader** → **Reader Settings**, click **Test Connection**. It should say *RFID module found on /dev/i2c-1 at 0x2C.*
3. If it doesn't, check the cable and that I2C is enabled. The defaults (bus `1`, address `0x2C`) suit the PiicoDev module on a Xerxes Pi, so only change them if your wiring differs.

**Register a tag and turn the reader on**  
1. Under **Add Tag**, click **Scan Tag** and hold a tag against the reader, or type its ID.
2. Enter the **Command to run** and click **Add Tag**.
3. Turn on **Run Tag Commands** at the top of **RFID Reader**.
4. Tap the tag. The command runs, and its output appears in **Reader Output**.

More detail on each control is in [commands.md](commands.md).

## Moving from the old Python version
The standalone `xerxes-rfid.py` and `tag-management.py` programs are no longer needed.

- **Keep your tags:** the panel uses the same `tags.json` format. Either copy your old file to `/root/.xerxes/tags.json` (the default location when deployed), or enter its full path under **Reader Settings** → **Tag File**.
- **Stop the old reader:** don't run `xerxes-rfid.py` while **Run Tag Commands** is on, since both would read the same module.

## Developing without the hardware
The RFID code can be tested on any computer without the module:  
`make test_rfid_driver test_rfid_manager`  
These use a simulated module instead of `/dev/i2c-1`. On a VM without I2C the **I2C** tab still loads, and the module shows as **Idle** or **Not found**.
