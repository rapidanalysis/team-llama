# OLED Display: To Improve

What is still missing from the OLED display support, plus ideas for later. Status as of 11 November 2026 (`team-llama` branch `developG39`).

## Done so far

- **Configured from the admin panel.** The OLED driver is now part of the admin panel and managed from the **I2C** tab. There is no separate program to build or run.
- **QR code links to the admin panel**, on port 8000.
- **More device information:** uptime, memory used and disk used, as well as hostname, IP address, DNS server and CPU temperature. The user picks which items to show.
- **Live preview** on the web page, replacing the old `--preview` terminal mode.
- **Settings are remembered** across restarts, including whether the display is on.
- **Recovers by itself** if the display is unplugged and plugged back in.
- **Translations:** all text and error messages on the page are in English and Spanish.
- **Unit tests** that need no display.

## Not yet complete

- **Custom mode:** let the user write their own text on the display. The driver supports drawing text and pixels, but there is no way to enter custom content yet.
- **Custom startup screen:** the old program showed "XerxesPi" on startup; this was dropped in the merge. Bring it back with text the user can change.
- **Hardware testing of the merged version:** repeat the display tests on a Xerxes Pi with the deployed panel (both modes, every item, unplugging and reconnecting, on/off across a reboot) and record the results.

## Known issues

- The QR code uses the first IPv4 address the Pi reports, which might be a Docker or Tailscale address on a Pi with several networks.
- The *No network* screen tells the user to connect a cable, although Wi-Fi is the recommended connection on the Xerxes Pi.

## Ideas for later

- Show Newt, Tailscale or Llama status on the display.
- Show RFID scan results briefly when a tag is tapped.
- A custom QR code (e.g. a Wi-Fi login or another web address).
- Rotate between several screens automatically.
- Let the user choose the refresh rate.
- Dim or turn off the display at night.
