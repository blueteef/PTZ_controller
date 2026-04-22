#!/bin/bash
# setup.sh — One-shot Pi provisioning script.
# Run as the pi user (not root) on a fresh Raspberry Pi OS Bookworm installation.

set -e
echo "=== PTZ Pi Setup ==="

# ── System packages ────────────────────────────────────────────────
echo "[1/5] Installing system packages..."
sudo apt-get update -qq
sudo apt-get install -y \
  python3-pip python3-venv python3-dev \
  libcamera-apps libcamera-dev \
  python3-picamera2 \
  python3-opencv \
  cmake libopenblas-dev liblapack-dev \
  libhdf5-dev libhdf5-serial-dev \
  sqlite3 \
  can-utils \
  --no-install-recommends

# ── CAN bus (MCP2515 via SPI) ──────────────────────────────────────
# Wiring: SCK=GPIO11(P23) MOSI=GPIO10(P19) MISO=GPIO9(P21)
#         CS=GPIO8(P24/CE0) INT=GPIO25(P22) VCC=3.3V(P1) GND(P6)
CONFIG_TXT="/boot/firmware/config.txt"
if ! grep -q "mcp2515-can0" "$CONFIG_TXT"; then
  echo ""
  echo ">>> Adding MCP2515 CAN overlay to $CONFIG_TXT"
  echo ">>> If your module crystal is 16MHz, change oscillator=8000000 to 16000000"
  sudo tee -a "$CONFIG_TXT" > /dev/null <<'EOF'

# CAN bus — MCP2515 SPI transceiver (TJA1050 or compatible)
dtparam=spi=on
dtoverlay=mcp2515-can0,oscillator=8000000,interrupt=25
EOF
  echo ">>> Reboot required for CAN overlay to take effect"
else
  echo "MCP2515 CAN overlay already present in $CONFIG_TXT — skipping"
fi

# Persist can0 bringup via systemd-networkd so it survives reboots
if [ ! -f /etc/systemd/network/can0.network ]; then
  sudo tee /etc/systemd/network/can0.network > /dev/null <<'EOF'
[Match]
Name=can0

[CAN]
BitRate=500000
EOF
  sudo systemctl enable systemd-networkd
  echo "Created /etc/systemd/network/can0.network — can0 will auto-up at 500k after reboot"
fi

# ── Python venv ────────────────────────────────────────────────────
echo "[2/5] Creating Python virtual environment..."
VENV_DIR="$(dirname "$0")/venv"
python3 -m venv --system-site-packages "$VENV_DIR"
source "$VENV_DIR/bin/activate"

# ── Python packages ────────────────────────────────────────────────
echo "[3/5] Installing Python packages..."
pip install --upgrade pip -q
pip install -r "$(dirname "$0")/requirements.txt" -q

# ── Data directories ───────────────────────────────────────────────
echo "[4/5] Creating data directories..."
mkdir -p "$(dirname "$0")/data" "$(dirname "$0")/models"

# ── .env file ──────────────────────────────────────────────────────
ENV_FILE="$(dirname "$0")/.env"
if [ ! -f "$ENV_FILE" ]; then
  cp "$(dirname "$0")/.env.example" "$ENV_FILE"
  echo "Created .env from template — edit it if needed"
fi

# ── Fan control service (GPIO17, thermal-driven) ────────────────────
# Uses system python3 + gpiozero (system package — no venv needed)
echo "[5/5] Installing fan control service..."
SCRIPT_ABS="$(cd "$(dirname "$0")" && pwd)/scripts/fan_control.py"
chmod +x "$SCRIPT_ABS"

if [ ! -f /etc/systemd/system/fan-control.service ]; then
  sudo tee /etc/systemd/system/fan-control.service > /dev/null <<UNIT
[Unit]
Description=PTZ Pi CPU fan controller (GPIO17 thermal-driven, high-active MOSFET)
After=multi-user.target
# Restart on any failure — if it can't run, the fan stays ON from last exit

[Service]
Type=simple
User=root
ExecStart=/usr/bin/python3 ${SCRIPT_ABS}
Restart=always
RestartSec=5
# Override thresholds here without editing the script:
Environment=FAN_GPIO=18
Environment=FAN_ON_TEMP=65
Environment=FAN_OFF_TEMP=55
Environment=FAN_POLL_S=5

[Install]
WantedBy=multi-user.target
UNIT
  sudo systemctl daemon-reload
  sudo systemctl enable fan-control
  sudo systemctl start fan-control
  echo "Fan control service installed, enabled, and started"
else
  echo "fan-control.service already exists — skipping (run: sudo systemctl restart fan-control to reload)"
fi

# Install fan-ctl command globally
sudo install -m 755 "$(dirname "$0")/scripts/fan-ctl" /usr/local/bin/fan-ctl
echo "fan-ctl installed — try: fan-ctl status"

echo ""
echo "=== Setup complete! ==="
echo ""
echo "Next steps:"
echo "  source venv/bin/activate"
echo "  fan-ctl status                       # verify fan controller is running"
echo "  python3 scripts/test_serial.py       # verify ESP32 connection"
echo "  bash scripts/download_models.sh      # download YOLO model"
echo "  uvicorn app.main:app --host 0.0.0.0 --port 8000"
