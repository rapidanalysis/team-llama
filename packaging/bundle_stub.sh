#!/bin/bash
#
# Xerxes Pi Admin Backend: self-extracting installer (made by make bundle).
#
#   bash xerxes_install.sh [directory]
#
# Unpacks the source into [directory] (default ./xerxes) and builds
# xerxes_backend there. The source is appended to this script after the
# __PAYLOAD__ line as a base64 tarball.
#
# llama.cpp is not included, so the Llama tab needs llama-server installed
# separately (make deb or deploy_systemd.sh from a full clone).

set -euo pipefail

DEST="${1:-$PWD/xerxes}"
SELF="${BASH_SOURCE[0]}"
MARKER=".xerxes-bundle"

echo "=========================================="
echo " Xerxes Pi Admin Backend installer"
echo "=========================================="

for tool in tar base64 make g++; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "[Error] '$tool' is not installed. Install the build tools with:"
    echo "        sudo apt-get install -y build-essential libcurl4-openssl-dev"
    exit 1
  fi
done

PAYLOAD_LINE="$(awk '/^__PAYLOAD__$/ { print NR + 1; exit }' "$SELF")"
if [ -z "$PAYLOAD_LINE" ]; then
  echo "[Error] No payload found. Rebuild this file with: make bundle"
  exit 1
fi

# Refuse to unpack over a directory we didn't create, so a typo can't mix
# the source into an unrelated folder
if [ -e "$DEST" ] && [ ! -e "$DEST/$MARKER" ] && [ -n "$(ls -A "$DEST" 2>/dev/null)" ]; then
  echo "[Error] $DEST already exists and wasn't made by this installer."
  echo "        Pick another directory: bash $SELF <directory>"
  exit 1
fi

echo "[1/3] Unpacking to $DEST..."
mkdir -p "$DEST"
# Replace the source rather than unpacking over it, so files removed since
# the last install don't linger. Built output in build/ is kept for speed.
(cd "$DEST" && rm -rf src include public packaging tests)
tail -n +"$PAYLOAD_LINE" "$SELF" | base64 -d | tar xzf - -C "$DEST"
touch "$DEST/$MARKER"

echo "[2/3] Checking libraries..."
MISSING=""
for header in curl/curl.h httplib.h json.hpp; do
  if ! printf '#include <%s>\n' "$header" | g++ -std=c++17 -E -x c++ - -I"$DEST/include" >/dev/null 2>&1; then
    MISSING="$MISSING $header"
  fi
done
if [ -n "$MISSING" ]; then
  echo "[Error] Missing headers:$MISSING"
  echo "        Install them with: sudo bash $DEST/install_deps.sh"
  echo "        then run this installer again."
  exit 1
fi

echo "[3/3] Building xerxes_backend..."
make -C "$DEST" -j"$(nproc 2>/dev/null || echo 1)"

echo "=========================================="
echo " Build complete"
echo " Run it:   cd $DEST && ./xerxes_backend"
echo "           (it serves public/ from the directory it is started in)"
echo " Panel:    http://<this-device>:8000"
echo "=========================================="

exit 0
