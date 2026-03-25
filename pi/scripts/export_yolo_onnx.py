"""
export_yolo_onnx.py — Export YOLOv8n to ONNX for use on Raspberry Pi.

Run this ONCE on a machine that has torch + ultralytics installed (dev PC).
The output file (yolov8n.onnx) is then copied to the Pi's pi/models/ directory.

Usage:
    pip install ultralytics        # requires torch
    python scripts/export_yolo_onnx.py

Output: pi/models/yolov8n.onnx   (~12 MB, runs on Pi via onnxruntime, no torch needed)
"""

from pathlib import Path
from ultralytics import YOLO

OUT_DIR = Path(__file__).parent.parent / "models"
OUT_DIR.mkdir(exist_ok=True)

print("Loading YOLOv8n (downloads ~6 MB on first run)...")
model = YOLO("yolov8n.pt")

print("Exporting to ONNX (imgsz=640, no dynamic axes)...")
model.export(
    format="onnx",
    imgsz=640,
    simplify=True,
    dynamic=False,
    opset=12,
)

# ultralytics exports to the same directory as the .pt file — move it
import shutil, os

src = Path("yolov8n.onnx")
if not src.exists():
    # ultralytics may place it next to its cache
    import glob
    candidates = glob.glob("**/yolov8n.onnx", recursive=True)
    if candidates:
        src = Path(candidates[0])

dst = OUT_DIR / "yolov8n.onnx"
if src.exists():
    shutil.move(str(src), str(dst))
    print(f"Saved to {dst}")
    print("\nNext step — copy to Pi:")
    print(f"  scp {dst} pi@<pi-ip>:~/PTZ_controller/pi/models/yolov8n.onnx")
else:
    print("Export done — find yolov8n.onnx and copy it to pi/models/")
