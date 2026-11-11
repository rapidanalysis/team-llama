# OLED Display: Commands

The OLED display is controlled from the **I2C** tab of the admin panel (`http://<pi-address>:8000`), under **OLED Display**. It replaces the old `./oled` command; the table at the end shows where each of its options went.

## Show on Display

The toggle at the top of **OLED Display** turns the screen on and off.

- **On:** the screen shows what you chose under **Screen Content**, refreshed every 2 seconds.
- **Off:** the screen is blanked and switched off.
- The setting is remembered, so the screen stays on or off after the panel or the Pi restarts. It is **on** by default.

The small boxes underneath show:

- **Display:** **Connected**, **Not found** (with the reason) or **Idle**. *Idle* is normal while the display is off: it is only driven while it is on.
- **Bus / Address:** where the panel looks for the display (normally `/dev/i2c-1`, `0x3C`).
- **Showing:** **Device information** or **QR code**.

## Screen Content

Choose what the screen shows. The **Preview** updates as you make changes, and shows *Unsaved changes* until you click **Save**.

**Mode**

- **Device information:** a list of the items you tick under **Information to show**.
- **QR code to this admin panel:** a QR code on the left that opens `http://<pi-address>:8000` when scanned with a phone, with *Scan to open the admin panel* next to it. The preview shows the link it contains. If the Pi has no IPv4 address yet, the screen says *No network* instead.

**Information to show** (Device information mode)

| Item | Shown as | Where it comes from |
| --- | --- | --- |
| Hostname | `xerxes-pi` | The device's hostname |
| IPv4 address | `IP   192.168.1.50` | The first network address found (not the loopback) |
| DNS server | `DNS  192.168.1.1` | `/etc/resolv.conf` |
| CPU temperature | `CPU  55.9°C` | `/sys/class/thermal/thermal_zone0/temp` |
| Uptime | `UP   2h 15m` or `UP   3d 4h` | `/proc/uptime` |
| Memory used | `MEM  1.2/7.8G` (used / total) | `/proc/meminfo` |
| Disk used | `DISK 12/58G` (used / total) | The root filesystem |

Pick at least one. Items are shown in the order listed, with a blank line between them when four or fewer are picked. The screen fits 8 lines of 21 characters.

**Save** sends the change to the display, which updates within a few seconds. Your choice of items is kept when you switch to QR mode, so switching back restores it.

## Display Settings

- **I2C Bus:** the bus number, e.g. `1` for `/dev/i2c-1`.
- **Address:** the display's I2C address, `0x3C` by default (some panels use `0x3D`).
- **Save** applies the settings. **Defaults** fills in the defaults.
- **Test Connection** re-initialises the display once and reports whether it answered, e.g. *Display found on /dev/i2c-1 at 0x3C.*

If the display stops answering (for example the cable is pulled), the panel shows the error, keeps your settings, and retries every 3 seconds. The screen comes back by itself once it is reconnected.

## Saved settings

Settings are saved in `oled.conf` in the panel's settings folder (`/root/.xerxes/` when deployed, `~/.xerxes/` when run by hand, or `$XERXES_CONFIG_DIR` if set):

```
enabled=1
mode=stats
stats=hostname,ip,dns,cpu_temp
bus=1
address=0x3C
```

## API

The **I2C** tab uses these endpoints, which can also be called with `curl` (replace `localhost` with the Pi's address from another computer). Errors include a `code` (e.g. `no_device`) and an English `error` message.

| Command | What it does |
| --- | --- |
| `curl localhost:8000/api/oled/status` | Settings, what is showing, the available items, the panel link used for the QR code, and the display's state |
| `curl -X POST localhost:8000/api/oled/power -d '{"enabled":false}'` | Turns the screen off (or on with `true`) |
| `curl -X POST localhost:8000/api/oled/display -d '{"mode":"stats","stats":["hostname","ip","uptime"]}'` | Changes what is shown. `mode` is `stats` or `qr`; `stats` uses the keys `hostname`, `ip`, `dns`, `cpu_temp`, `uptime`, `memory`, `disk` |
| `curl -X POST localhost:8000/api/oled/settings -d '{"bus":1,"address":"0x3C"}'` | Changes the bus and address |
| `curl -X POST localhost:8000/api/oled/probe` | Tests the connection to the display |
| `curl 'localhost:8000/api/oled/preview?mode=stats&stats=ip,dns'` | Draws a screen without changing the display, as 64 rows of hex. Without `mode`, previews the saved settings |

## Logs

On a deployed device, display events appear in the panel's log with lines starting `[OLED]`:

```bash
sudo journalctl -u xerxes -f
```

## Where the old command's options went

| Old `./oled` command | I2C tab |
| --- | --- |
| `./oled` (system info screen) | **Mode** → **Device information** |
| `./oled qr` (QR code for `http://<pi-ip>`) | **Mode** → **QR code to this admin panel** (now links to the panel on port 8000) |
| `./oled qr http://example.com` (custom QR text) | Not available: the QR code always links to the admin panel |
| `./oled --preview` (print the screen in the terminal) | **Preview** on the page |
| Ctrl+C / SIGTERM (clear and turn off the display) | **Show on Display** toggle |
