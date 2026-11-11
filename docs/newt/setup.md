# Newt: Setup

Newt lets you reach a Xerxes Pi from anywhere on the internet through a Pangolin server, without opening any ports on your router. It is the open-source alternative to ngrok in the Xerxes Pi admin panel.

This guide gets a Xerxes Pi (or a development VM) ready to run Newt and connects it for the first time. For how the pieces fit together, see [architecture.md](architecture.md).

## What you need

| Item | Why | Where to get it |
| --- | --- | --- |
| A Pangolin server | Newt connects to it; it gives your Pi a public address | Your own self-hosted Pangolin, or Pangolin Cloud (`https://app.pangolin.net`) |
| A **Site** on that server for this Pi | Gives you the three connection details: endpoint, Newt ID and secret | Created in the Pangolin dashboard (step 1) |
| Docker on the Pi | The admin panel runs Newt as a Docker container | Installed by `install_deps.sh` (step 2) |
| The admin panel, built and running | Its **Tunnels** page has the Newt card | Step 3 |
| Outgoing internet access from the Pi | Newt dials out to Pangolin | Your normal network; no router changes needed |

Use **one Site per device**. Two devices (or two copies of Newt) on the same Site knock each other offline.

## Step 1: Create a Site in Pangolin

Whoever manages your Pangolin server does this step.

1. Log in to the Pangolin dashboard.
2. Add a new Site. Name it after the Pi (for example its hostname) and choose **Newt** as the connection method. Menu wording can differ between Pangolin versions.
3. Copy the three values Pangolin shows: **Endpoint**, **Newt ID** and **Secret**. Store them somewhere safe and treat the secret like a password.

Pangolin also shows ready-made `docker run` and Docker Compose commands. You do not need them: the admin panel runs Newt for you.

## Step 2: Make sure Docker works

`install_deps.sh` in the `team-llama` repo installs Docker, starts it, and sets it to start at every boot (see [admin-panel/setup.md](../admin-panel/setup.md#step-2-install-the-dependencies)). To check:

```bash
docker --version                       # should print a version number
sudo systemctl status docker           # should show active (running)
```

The admin panel must also be **allowed** to use Docker. Choose one:

- **Recommended:** run the admin panel as its system service (`sudo ./deploy_systemd.sh` in `team-llama`). The service runs as root, so it can always use Docker.
- **Development:** run the panel as your own user and add that user to the `docker` group. `install_deps.sh` already does this for the user it sets up; otherwise run `sudo usermod -aG docker $USER`, then log out and back in. Be aware this gives that user full control of the machine.
- **Narrow sudo rule:** allow only the `docker` command through `sudo` and start the panel with `XERXES_DOCKER='sudo -n docker'` (the exact commands are shown in the panel; see below).

You don't need to get this right first time. If the panel can't use Docker, the Newt card shows a **Newt needs Docker access** box that says what is wrong (Docker not installed, not running, or not allowed) and gives the commands to fix it for your device, with a **Copy** button for each. Run them in a terminal on the Pi, then click **Check Again**.

## Step 3: Build and start the admin panel

Newt itself needs no compiling: it comes ready-made in the `fosrl/newt` Docker image. Only the admin panel is built, as described in [admin-panel/setup.md](../admin-panel/setup.md). In short, from the `team-llama` folder (branch `developG39` or later):

```bash
sudo ./deploy_systemd.sh     # on the Pi: installs and starts the panel as a service
```

or, while developing:

```bash
make                         # builds ./xerxes_backend
make test_newt               # optional: runs the 19 Newt unit tests
./xerxes_backend
```

The admin panel is then at `http://<pi-address>:8000`.

## Step 4: Connect Newt

1. Open the admin panel and click **Tunnels**.
2. Scroll to the **Newt (Pangolin)** card.
3. Enter the **Endpoint** (the full address, starting with `https://`), the **Newt ID** and the **Secret** from step 1.
4. Click **Enable**. The first time can take up to a minute while the Pi downloads Newt (about 36 MB).
5. Within about 10 seconds the **Tunnel** box should change from **Connecting...** to **Connected**, and the card says *Tunnel is up. This device is reachable through Pangolin.* The **Version** box shows which Newt version is running.

Your details are saved, so next time you can click **Enable** with the Secret box left empty.

If another copy of Newt is already running on the Pi (for example one installed directly as `newt.service`, or another Newt container), the panel lists it and asks before starting. If both use the same Site they will keep disconnecting each other, and anything you reach through that tunnel (such as a web SSH session) may drop. Click **Cancel** unless you are sure they use different Sites.

## Step 5: Keep Newt up to date (optional)

The **Updates** section of the card keeps Newt current:

- **Update automatically** is on by default once Newt is set up. The panel checks once a day and installs a newer Newt by itself.
- **Check for Updates** downloads the latest version to compare, without restarting anything.
- **Update Now** installs the latest version straight away. The tunnel drops for a few seconds.
- **Roll Back** goes back to the version that was running before the last update.

If a new version doesn't reconnect within a minute, the panel automatically puts the previous version back, and automatic updates then skip that version.

## Step 6: Publish a service (optional)

A connected tunnel on its own publishes nothing. To reach the admin panel or another service from outside, add a **Resource** to this Pi's Site in the Pangolin dashboard and point it at the service (for example the admin panel on port 8000). **Always protect it with Pangolin's login or PIN options:** the admin panel has no login of its own.

## If something goes wrong

| What you see | Fix |
| --- | --- |
| **Rejected** / *No newt found with that newtId* | The Newt ID or secret is wrong. Copy both again from the Site in Pangolin. |
| **Can't reach server** | Check the endpoint address and the Pi's internet connection. |
| **Docker unavailable** and the *Newt needs Docker access* box | Follow one of the fixes in the box, then click **Check Again**. See step 2. |
| A list of other Newt copies when you click **Enable** | Another Newt is running on the Pi. Stop it (see [commands.md](commands.md#running-newt-without-the-admin-panel)) or confirm only if it uses a different Site. |
| *Unexpected response from backend* | An old build of the admin panel is running. Stop it and start the one you just built. |
| Tunnel keeps dropping | Another copy of Newt is using the same Site. Remove it (see [commands.md](commands.md)). |
| An update was rolled back | The new version didn't reconnect. Newt is back on the previous version; check its logs with **View Logs**. |

More detail on each command is in [commands.md](commands.md).
