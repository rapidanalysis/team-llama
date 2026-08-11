#!/bin/bash

# Exit immediately if a command fails
set -e

echo "=========================================="
echo " Deploying Xerxes Pi Admin Panel"
echo "=========================================="

# 1. Ensure the script is run as root
if [ "$EUID" -ne 0 ]; then
  echo "[Error] Please run this script as root using: sudo ./deploy.sh"
  exit 1
fi

# 2. Check if the required files exist in the current directory
if [ ! -f "xerxes_backend" ] || [ ! -d "public" ]; then
    echo "[Error] 'xerxes_backend' binary or 'public/' folder not found!"
    echo "Make sure you run this script from your project root directory."
    exit 1
fi

# 3. Setup the /opt/xerxes directory
APP_DIR="/opt/xerxes"
echo "[1/4] Creating application directory at $APP_DIR..."
mkdir -p $APP_DIR

# 4. Copy files to the deployment directory
echo "[2/4] Copying binary and frontend assets..."
cp xerxes_backend $APP_DIR/
cp -r public $APP_DIR/

# 5. Ensure the binary is executable
chmod +x $APP_DIR/xerxes_backend

# 6. Create the systemd service file
SERVICE_FILE="/etc/systemd/system/xerxes.service"
echo "[3/4] Creating systemd service file at $SERVICE_FILE..."

cat <<EOF > $SERVICE_FILE
[Unit]
Description=Xerxes Pi Admin Backend
After=network.target

[Service]
Type=simple
WorkingDirectory=$APP_DIR
ExecStart=$APP_DIR/xerxes_backend
Restart=always
RestartSec=3
User=root

[Install]
WantedBy=multi-user.target
EOF

# 7. Reload systemd and start the service
echo "[4/4] Reloading systemd daemon and starting service..."
systemctl daemon-reload
systemctl enable xerxes
systemctl restart xerxes

echo "=========================================="
echo " Deployment Complete!"
echo " The admin panel is now running in the background."
echo " Check the live status using: sudo systemctl status xerxes"
echo "=========================================="
