# OLED Display: Setup

The Xerxes Pi can drive a small 128×64 OLED screen that shows device information (hostname, IP address, CPU temperature and more) or a QR code that opens the admin panel on your phone. The OLED driver is now built into the admin panel and managed from the **I2C** tab, so there is no separate `./oled` program to build or run.

## Requirements

- Xerxes Pi V2 with a Raspberry Pi Compute Module 4 or 5
- Raspberry Pi OS or Debian 13 (64-bit)
- An SSD1306 128×64 OLED display on I2C (address `0x3C`, some panels use `0x3D`), connected to the Xerxes Pi's Qwiic port
- I2C enabled
- The admin panel installed (see [admin-panel/setup.md](../admin-panel/setup.md))

## Setup

*All commands below are typed into the Xerxes Pi's terminal (over SSH or Raspberry Pi Connect).*

**Connect the display**  
With the Pi turned off, connect the OLED display to the Xerxes Pi's Qwiic (I2C) port. It can be daisy-chained with the PiicoDev RFID module.

**Enable I2C**

```bash
sudo raspi-config
```

Then go to **Interface Options → I2C → Yes**, exit, and reboot with `sudo reboot`.

**Check the display is detected (optional)**

```bash
sudo apt install i2c-tools
i2cdetect -y 1
```

The display appears as `3c` (or `3d`) in the table.

**Install the admin panel**  
Follow [admin-panel/setup.md](../admin-panel/setup.md). In short, on the Xerxes Pi:

```bash
git clone --recurse-submodules https://github.com/rapidanalysis/team-llama.git
cd team-llama
bash install_deps.sh
sudo ./deploy_systemd.sh
```

The display turns on by itself as soon as the panel starts, showing the hostname, IP address, DNS server and CPU temperature.

**Check it from the panel**

1. In a browser, open `http://<pi-address>:8000` and click the **I2C** tab.
2. Under **OLED Display**, the **Display** box should say **Connected**.
3. If it says **Not found**, open **Display Settings** and click **Test Connection**. The message says what is wrong (I2C not enabled, nothing at that address, or the display stopped answering). Check the cable and, if your display uses `0x3D`, change the **Address** and click **Save**.

## Developing without a display

The **Preview** on the I2C tab draws exactly what the screen would show, using the same code, so you can work on the screens on a VM or a Pi without a display. The OLED tests also need no hardware:

```bash
make test_oled_manager
```

## Moving from the old standalone driver

The old `oled` program (`./oled`, `./oled qr`, `./oled --preview`) is no longer needed. If it is still installed or running on the device, stop it, since both would write to the same display.
