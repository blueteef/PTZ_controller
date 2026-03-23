#!/bin/bash
# download_models.sh — Pre-download ML models before first launch.
# Run once on the Pi: bash scripts/download_models.sh

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODEL_DIR="$SCRIPT_DIR/../models"
mkdir -p "$MODEL_DIR"

echo "Downloading YOLOv8-nano..."
python3 -c "
from ultralytics import YOLO
import shutil, pathlib
m = YOLO('yolov8n.pt')  # downloads to ~/.cache/ultralytics/
src = pathlib.Path.home() / '.cache/ultralytics/yolov8n.pt'
dst = pathlib.Path('$MODEL_DIR/yolov8n.pt')
if src.exists() and not dst.exists():
    shutil.copy(src, dst)
    print(f'Copied to {dst}')
else:
    print(f'Model already at {dst} or source not found')
"

echo "Done.  Models in $MODEL_DIR/"
ls -lh "$MODEL_DIR/"
