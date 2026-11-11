# Newt: To Improve

What is still missing from Newt support, measured against the SRS and the sponsor's requests, plus ideas for later. Status as of 11 November 2026 (`team-llama` branch `developG39`).

## Done so far

- **FR-1 (Newt client):** the **Newt (Pangolin)** card on the **Tunnels** page enables and disables Newt and shows the connection status. Tested on the development VM against the sponsor's Pangolin server (`pangolin.weburban.com`): the tunnel connects and the card shows **Connected**.
- **Automatic updates** (sponsor request, 16 October): a daily check, **Check for Updates**, **Update Now** and **Roll Back**, with automatic roll back if a new version doesn't reconnect. The running Newt version is shown on the card.
- **Docker access help** (defect D-01): when the panel can't use Docker, the card explains why and shows the commands to fix it.
- **Duplicate Newt detection** (defect D-05): before enabling, the panel looks for the sponsor's native `newt.service` and other Newt containers, and asks before starting a second copy.
- **Deployment as root:** `deploy_systemd.sh` installs the panel as a root service, which can use Docker without extra setup.
- **Translations (FR-11):** all of the card's text, including status hints and error messages, now comes from `public/i18n/<lang>.json` (English and Spanish), apart from the Docker fix text (see below).
- 19 unit tests (`tests/test_newt.cpp`).

## Not yet complete (SRS or sponsor)

The items below are carried over from the 21 October status. The code can't show whether the tests or sponsor decisions have happened since, so confirm each with the Newt owner before submission.

| Item | Source | What's missing | Next step |
| --- | --- | --- | --- |
| Feature parity with ngrok | FR-2 | Reaching the admin panel, SSH and other services through Pangolin. A connected tunnel publishes nothing until a Resource is added in the Pangolin dashboard. | The sponsor adds a Resource for the admin panel (port 8000) on the team's Site, or gives the team a dashboard login. Then test admin panel, SSH and one other service. |
| Admin panel reachable remotely | Team task list | Same blocker as above. | As above. |
| Site shown as online in Pangolin | Team task list | Newt's log confirms the tunnel, but nobody on the team has seen the Site's status in the dashboard. | Ask the sponsor to confirm, with a screenshot for the user manual. |
| WireGuard confirmed by network capture | NFR-7 | Not yet captured. | Capture traffic on the Pi or VM while Newt is connected (for example `sudo tcpdump -i any udp`) and record the result. |
| Tested on Xerxes Pi hardware | NFR-5 | The card was tested on an x86-64 VM. Newt ran on the sponsor's arm64 Pi through the same Docker path on 18 October, but not through the card. | Deploy `developG39` on the Pi with `deploy_systemd.sh` and repeat the VM test with a separate Site. The panel will warn about the Pi's existing `newt.service`; don't stop it. |
| Updates tested end to end | Sponsor request | Update and roll back are covered by unit tests of the rules, but a real update from one Newt release to the next needs an older image to start from. | Start Newt on an older tag, then use **Update Now** and **Roll Back** on the Pi. |
| Docker or native Newt | Sponsor decision | The admin panel runs Newt in Docker. The sponsor's own Pi runs Newt natively under systemd. | Ask the sponsor which they want for the product. The native option is in [architecture.md](architecture.md). |
| Licensing | SRS standards and risks | Pangolin is dual-licensed (AGPL-3 and the Fossorial Commercial License). Using the official `fosrl/newt` image unmodified has not been formally checked against RapidAnalysis's commercial use. | Confirm Newt's licence and the sponsor's position in writing. |
| Admin panel login | NFR-13 | The admin panel has no authentication, so it must not be published through Pangolin without Pangolin's own login or PIN. | Add authentication (`access_control.cpp` is still empty). |

## Known issues

- The Docker fix titles, explanations and some backend error messages are in English only, even when the panel is in Spanish.
- If Newt rewords its log messages in a future release, the status could stay on **Connecting...** (see [architecture.md](architecture.md#limitations-of-this-design)).

## Ideas for later

- Show the Pi's public Pangolin address(es) on the Newt card, like the ngrok card shows its tunnel URL.
- Create and remove Pangolin Resources from the card using Pangolin's API, so users never need the dashboard (completes FR-2).
- Run Newt natively under systemd instead of Docker, removing the Docker dependency (see [architecture.md](architecture.md)).
- Download the Newt image in the background, with progress on the card, instead of waiting on the first **Enable** or an update.
- Show tunnel status on the OLED display.
- Read Newt's status from a health or metrics endpoint instead of its log wording.
- Store the secret in a system secret store instead of a `600` file.
- Let the user choose the time of day for the automatic update.
