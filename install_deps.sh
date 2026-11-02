#!/usr/bin/env bash
# Installs dependencies on Debian 13 (Trixie) arm64.
#
# Usage:
#   bash install_deps.sh                          # as a user with sudo
#   su -c "bash install_deps.sh --user <you>"     # as root (no sudo yet)

set -euo pipefail

DEV_USER=""
HTTPLIB_REF="${HTTPLIB_REF:-master}"
JSON_REF="${JSON_REF:-develop}"
INCLUDE_DIR="/usr/local/include"

while [ $# -gt 0 ]; do
  case "$1" in
    -u|--user) DEV_USER="$2"; shift 2 ;;
    *) echo "Unknown option: $1" >&2; exit 1 ;;
  esac
done

if [ "$(id -u)" -eq 0 ]; then
  SUDO=""
  DEV_USER="${DEV_USER:-${SUDO_USER:-root}}"
else
  SUDO="sudo"
  DEV_USER="$(id -un)"
fi

# APT packages
export DEBIAN_FRONTEND=noninteractive
$SUDO apt-get update -y
$SUDO apt-get install -y \
  build-essential gdb git curl wget ca-certificates file gnupg \
  libcurl4-openssl-dev cmake \
  docker.io samba nmap \
  sudo procps iproute2 iputils-ping keyutils coreutils sed grep

# Services and groups
$SUDO systemctl enable --now docker
$SUDO systemctl enable smbd
if [ "$DEV_USER" != "root" ]; then
  $SUDO usermod -aG docker,sudo "$DEV_USER"
fi

# Tailscale
if ! command -v tailscale >/dev/null 2>&1; then
  curl -fsSL https://tailscale.com/install.sh | $SUDO sh
fi

# ngrok
if ! command -v ngrok >/dev/null 2>&1; then
  curl -fsSL https://ngrok-agent.s3.amazonaws.com/ngrok.asc \
    | $SUDO tee /etc/apt/trusted.gpg.d/ngrok.asc >/dev/null
  echo "deb https://ngrok-agent.s3.amazonaws.com buster main" \
    | $SUDO tee /etc/apt/sources.list.d/ngrok.list >/dev/null
  $SUDO apt-get update -y
  $SUDO apt-get install -y ngrok
fi

# Header-only libraries
$SUDO mkdir -p "$INCLUDE_DIR"
$SUDO curl -fsSL --retry 3 \
  "https://raw.githubusercontent.com/yhirose/cpp-httplib/${HTTPLIB_REF}/httplib.h" \
  -o "${INCLUDE_DIR}/httplib.h"
$SUDO curl -fsSL --retry 3 \
  "https://raw.githubusercontent.com/nlohmann/json/${JSON_REF}/single_include/nlohmann/json.hpp" \
  -o "${INCLUDE_DIR}/json.hpp"

echo "Done. Log out and back in so ${DEV_USER}'s docker/sudo group membership applies."
