# RFID reader (FR-12): PiicoDev RFID module over I2C

This directory has the RFID code from the C++ port of `xerxes-rfid` (`xerxes-rfid/cpp-rewrite`), built into the admin backend. The standalone program itself isn't included. Users manage it from the **I2C** tab of the web panel.

| File | What it is |
|------|------------|
| `include/rfid.h`, `rfid.cpp` | MFRC522 driver: port of Core Electronics' `PiicoDev_RFID.py`, with the same I2C register traffic |
| `include/i2c.h`, `i2c_linux.cpp` | `/dev/i2c-N` access (`I2C_RDWR` ioctl) |
| `include/tags.h`, `tags.cpp` | `tags.json` load/save, in the same format as the Python version |
| `include/util.h` | `sleep_ms()` |
| `rfid_manager.cpp` | Backend service behind `/api/rfid/*` (interface: `include/rfid_manager.h`) |

## How the web panel maps to `xerxes-rfid`

| `xerxes-rfid` | I2C tab |
|---------------|---------|
| `run` | **Run Tag Commands** toggle. Its output appears in **Reader Output** and the backend log |
| `manage` → List tags | **Registered Tags** |
| `manage` → Add tag (scan) | **Add Tag** → *Scan Tag* (you can also type the ID) |
| `manage` → Edit / Delete tag | *Edit* / *Delete* on each registered tag |
| `manage` → "already registered, edit instead?" | *Edit it instead* link |
| `manage` → Live scan | **Live Scan** (leaving the tab stops it, like Ctrl+C) |
| `--bus`, `--address`, `--tags` | **Reader Settings** (plus *Test Connection*) |
| `--version` | Version badge on Reader Settings |

### Differences from the original program
- **No restart needed after editing tags.** The reader re-reads `tags.json` on every tap, so edits apply immediately.
- **Run mode is remembered.** Settings are saved to `<config dir>/rfid.conf`, and run mode turns back on after a backend restart. The config dir is `$XERXES_CONFIG_DIR`, or `~/.xerxes` (`/root/.xerxes` under systemd).
- **Default tag file** is `<config dir>/tags.json`. To keep using an existing file, set its absolute path under Reader Settings.
- **Command output is captured** and shown in the panel, along with the exit code (up to 16 KB per run). Commands still run with `/bin/sh -c` as the backend's user. Like the original program, the reader waits for each command to finish. A command that starts a background job (`foo &`) doesn't stall it.
- **Scanning a tag to register it never runs that tag's command**, even while run mode is on.
- **The bus is only used while it's needed** (run mode, live scan or a pending scan), so a Pi without the module just shows *Idle* or *Not found*.

If the original `xerxes-rfid` program is installed on the device, don't run it while the panel's reader is on: both would poll the same module.

## Building
- `make` builds everything here into `xerxes_backend`.

## Tests (no hardware needed)
`make test` runs both RFID tests against `tests/mfrc522_sim.cpp`, a simulated module that stands in for `/dev/i2c-N`.

- `tests/test_rfid_driver.cpp` checks the driver's results and every I2C register access against those recorded from the original Python driver.
- `tests/test_rfid_manager.cpp` covers the service behind the I2C tab: tapping tags, scanning, run mode, live scan, tag editing, settings and a corrupt `tags.json`.
