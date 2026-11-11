# Admin Panel: To Improve

What is still missing from the panel as a whole, measured against the SRS and the sponsor's requests, plus known issues and ideas for later. Feature-specific items are in each feature's own `to-improve.md`. Status as of 11 November 2026 (`team-llama` branch `developG39`, commit `186da9d`).

## Done so far

- **One compiled program.** The RFID reader and OLED display, previously separate Python and C++ programs, now run inside `xerxes_backend`, as the sponsor asked.
- **Production deployment.** `deploy_systemd.sh` builds and installs everything, including llama-server, as the `xerxes` service. `make deb` and `make bundle` give two more ways to install.
- **llama.cpp as a submodule**, pinned to the official release b11062, built by `make llama`.
- **Faster builds.** `make` only recompiles what changed.
- **Two languages** (English and Spanish) across the whole panel.
- **Unit tests** for the command wrapper, Newt, RFID and OLED, none of which need hardware (`make test`).

## Not yet complete (SRS or sponsor)

| Item | Source | What's missing | Next step |
| --- | --- | --- | --- |
| Login | NFR-13 | `src/access_control.cpp` is empty, so anyone who can reach port 8000 has full control, including root-level actions. | Add authentication before the panel is published through any tunnel. |
| Least privilege | NFR-12 | The service runs as root. | Run as a dedicated user with narrow `sudo` rules, the `docker` group and the `i2c` group. |
| Separate modules tab | Sponsor list (`docs/to_improve.md`) | A tab to turn whole modules on and off. | Agree with the sponsor which modules can be turned off. |
| Fold "System commands" into the modules | Sponsor list | The **System** tab is still a separate list of commands. | Move each command next to the feature it belongs to. |
| Tailscale and ngrok dashboards | Sponsor list | Basic cards exist on the **Tunnels** tab; configuration is still limited. | Add settings such as the ngrok auth token and Tailscale options. |
| Button output in the same box | Sponsor list | Command output now opens in a pop-up, and most cards show success or error messages under their buttons. | Confirm with the sponsor that this meets the request. |
| More network information and settings | Sponsor list | See *Known issues* below on how static IPs are set. | Show more interface detail; switch to the network manager the OS uses. |

## Known issues

- **ngrok "Admin Panel" option uses port 8080.** The panel listens on port **8000**, but the ngrok card's *8080 — Admin Panel* option (and `ngrok_start`) forwards port 8080, so it won't reach the panel. Change it to 8000 in `public/index.html` and `src/system_ops.cpp`.
- **Static IP settings are written to `/etc/dhcpcd.conf`.** Recent Raspberry Pi OS and Debian 13 installs normally manage the network with NetworkManager instead, in which case the setting may have no effect. Check on the Xerxes Pi image and switch to `nmcli` if needed.
- **Ethernet on the Xerxes Pi board is unreliable.** Wi-Fi is recommended.
- **The panel must be started from the repo root** (see [architecture.md](architecture.md#limitations-of-this-design)).
- **`install_deps.sh` doesn't install `i2c-tools`**, which is handy for checking I2C wiring (`i2cdetect -y 1`). Install it with `sudo apt install i2c-tools`.

## Ideas for later

- Serve the panel over HTTPS.
- Show long jobs (model downloads, the first Newt image download) with a progress bar instead of a waiting request.
- Find `public/`, `models/` and llama-server relative to the program itself, so it can be started from any folder.
- Publish the `.deb` as a release so devices don't need to build anything.
