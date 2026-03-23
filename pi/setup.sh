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
  --no-install-recommends

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

echo "[5/5] Setup complete!"
echo ""
echo "Next steps:"
echo "  source venv/bin/activate"
echo "  python3 scripts/test_serial.py       # verify ESP32 connection"
echo "  bash scripts/download_models.sh      # download YOLO model"
echo "  uvicorn app.main:app --host 0.0.0.0 --port 8000"
