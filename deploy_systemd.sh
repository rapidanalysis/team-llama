#!/bin/bash
#
# Deploys the Xerxes Pi Admin Panel as a systemd service in /opt/xerxes,
# including llama-server so the Llama tab and Ask Xerxes AI work.
#
#   sudo ./deploy_systemd.sh
#
# Safe to re-run: it rebuilds whatever is out of date, replaces the
# deployed program files, and keeps any models already in /opt/xerxes/models
# and settings in /root/.xerxes (Newt, RFID tags, OLED).

set -euo pipefail

# Must match WorkingDirectory in packaging/xerxes.service
APP_DIR="/opt/xerxes"
SERVICE_FILE="/etc/systemd/system/xerxes.service"
PORT=8000

# xerxes_backend starts llama-server from this path, relative to its working
# directory (kServerBinary in src/llama_manager.cpp), so the deployed copy
# mirrors the repo layout.
LLAMA_REL_DIR="third_party/llama.cpp/build/bin"
LLAMA_DEST="$APP_DIR/$LLAMA_REL_DIR"

echo "=========================================="
echo " Deploying Xerxes Pi Admin Panel"
echo "=========================================="

# 1. Must run as root, from anywhere — work from the repo the script lives in
if [ "$EUID" -ne 0 ]; then
  echo "[Error] Please run this script as root: sudo ./deploy_systemd.sh"
  exit 1
fi
REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$REPO_DIR"

# Build as the user who ran sudo, so files in the repo don't end up owned by root
BUILD_USER="${SUDO_USER:-root}"
as_build_user() {
  if [ "$BUILD_USER" != "root" ]; then sudo -u "$BUILD_USER" -H "$@"; else "$@"; fi
}

# 2. System packages needed to build and run
#    libcurl4-openssl-dev also pulls in the right libcurl runtime for this release
echo "[1/7] Checking system packages..."
PACKAGES="g++ make cmake git curl libcurl4-openssl-dev iproute2"
MISSING=""
for pkg in $PACKAGES; do
  if ! dpkg-query -W -f='${Status}' "$pkg" 2>/dev/null | grep -q "install ok installed"; then
    MISSING="$MISSING $pkg"
  fi
done
if [ -n "$MISSING" ]; then
  echo "      Installing:$MISSING"
  apt-get update
  # shellcheck disable=SC2086
  apt-get install -y $MISSING
else
  echo "      All present."
fi

# Header-only libraries come from install_deps.sh, not apt
for header in httplib.h json.hpp; do
  if ! printf '#include <%s>\n' "$header" | g++ -std=c++17 -E -x c++ - -Iinclude -Ithird_party >/dev/null 2>&1; then
    echo "[Error] $header not found. Install it with: sudo bash install_deps.sh"
    exit 1
  fi
done

# RFID and the OLED display need I2C. Not fatal: the rest of the panel works
# without it, and the I2C tab shows the reader/display as not found.
if ! ls /dev/i2c-* >/dev/null 2>&1; then
  echo "      Note: no /dev/i2c-* found, so the RFID reader and OLED display won't work."
  echo "            On a Raspberry Pi: sudo raspi-config -> Interface Options -> I2C, then reboot."
fi

# 3. llama.cpp source and llama-server build
echo "[2/7] Checking llama.cpp..."
if [ ! -f third_party/llama.cpp/CMakeLists.txt ]; then
  echo "      Fetching the llama.cpp submodule..."
  as_build_user git submodule update --init --recursive
fi
if [ ! -x "$LLAMA_REL_DIR/llama-server" ]; then
  echo "      Building llama-server. On a Raspberry Pi this can take an hour or more..."
  as_build_user make llama
else
  echo "      llama-server already built."
fi

# 4. Backend — make only rebuilds what changed
echo "[3/7] Building xerxes_backend..."
as_build_user make -j"$(nproc)"

# 5. Stop the running service first: the binary can't be replaced while it
#    runs, and stopping it also shuts down any llama-server it started
echo "[4/7] Stopping the existing service (if any)..."
systemctl stop xerxes 2>/dev/null || true

if ss -ltn "sport = :$PORT" | grep -q LISTEN; then
  echo "[Error] Port $PORT is already in use by another program:"
  ss -ltnp "sport = :$PORT" | tail -n +2
  echo "        Stop it (for example a ./xerxes_backend started by hand) and run this script again."
  exit 1
fi

# 6. Copy everything into /opt/xerxes
echo "[5/7] Installing to $APP_DIR..."
mkdir -p "$APP_DIR" "$APP_DIR/models" "$LLAMA_DEST"

install -m 755 xerxes_backend "$APP_DIR/xerxes_backend"

# Replace public/ rather than copying over it, so files removed from the
# project (e.g. the old lang.json) don't linger in the deployed panel
rm -rf "$APP_DIR/public"
cp -r public "$APP_DIR/public"

# llama-server plus its shared libraries (-a keeps the .so version symlinks)
rm -rf "${LLAMA_DEST:?}"/*
cp -a "$LLAMA_REL_DIR/llama-server" "$LLAMA_REL_DIR"/*.so* "$LLAMA_DEST/"

# Models downloaded during development come along; existing ones are kept
shopt -s nullglob
for model in models/*.gguf; do
  if [ ! -e "$APP_DIR/$model" ]; then
    echo "      Copying $model..."
    cp "$model" "$APP_DIR/$model"
  fi
done
shopt -u nullglob

# Confirm llama-server starts using the deployed libraries. LD_LIBRARY_PATH
# takes priority over the build-tree path compiled into the binary, so this
# keeps working if the repo is moved or deleted.
if LD_LIBRARY_PATH="$LLAMA_DEST" ldd "$LLAMA_DEST/llama-server" | grep -q "not found"; then
  echo "[Error] llama-server is missing libraries:"
  LD_LIBRARY_PATH="$LLAMA_DEST" ldd "$LLAMA_DEST/llama-server" | grep "not found"
  exit 1
fi
if ! LD_LIBRARY_PATH="$LLAMA_DEST" "$LLAMA_DEST/llama-server" --version >/dev/null 2>&1; then
  echo "[Error] The deployed llama-server doesn't run. Try: make -C $REPO_DIR llama"
  exit 1
fi

# 7. systemd unit (shared with the .deb from make deb)
echo "[6/7] Installing $SERVICE_FILE..."
install -m 644 packaging/xerxes.service "$SERVICE_FILE"

# 8. Start and check it answers
echo "[7/7] Starting the service..."
systemctl daemon-reload
systemctl enable xerxes
systemctl restart xerxes

for _ in $(seq 1 20); do
  if curl -fs "http://127.0.0.1:$PORT/api/status" >/dev/null; then break; fi
  sleep 0.5
done
if ! curl -fs "http://127.0.0.1:$PORT/api/status" >/dev/null; then
  echo "[Error] The service started but isn't answering on port $PORT."
  echo "        See what went wrong with: sudo journalctl -u xerxes -n 50"
  exit 1
fi
LLAMA_STATUS="$(curl -fs "http://127.0.0.1:$PORT/api/llama/status" || echo "unavailable")"
MODELS="$(curl -fs "http://127.0.0.1:$PORT/api/llama/models" || echo "unavailable")"

IP="$(hostname -I 2>/dev/null | awk '{print $1}')"
echo "=========================================="
echo " Deployment complete"
echo " Admin panel:  http://${IP:-<this-device>}:$PORT"
echo " Llama:        $LLAMA_STATUS (off until enabled in the Llama tab)"
echo " Models:       $MODELS"
echo " Status:       sudo systemctl status xerxes"
echo " Logs:         sudo journalctl -u xerxes -f"
echo "=========================================="
