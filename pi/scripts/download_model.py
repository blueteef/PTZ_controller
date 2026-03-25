"""
download_model.py — Download the EfficientDet-Lite0 INT8 TFLite model.

Run once on the Pi before enabling object detection:
    python scripts/download_model.py

Downloads to pi/models/efficientdet_lite0_int8.tflite (~4 MB).
"""

import sys
import urllib.request
from pathlib import Path

MODEL_URL = (
    "https://storage.googleapis.com/mediapipe-models/"
    "object_detector/efficientdet_lite0/int8/1/efficientdet_lite0.tflite"
)

MODELS_DIR = Path(__file__).parent.parent / "models"
DEST = MODELS_DIR / "efficientdet_lite0_int8.tflite"


def main() -> None:
    MODELS_DIR.mkdir(exist_ok=True)

    if DEST.exists():
        print(f"Already exists: {DEST}  ({DEST.stat().st_size // 1024} KB)")
        return

    print(f"Downloading {MODEL_URL}")
    print(f"  → {DEST}")

    def progress(count, block_size, total):
        pct = min(100, count * block_size * 100 // total)
        bar = "#" * (pct // 5) + "." * (20 - pct // 5)
        print(f"\r  [{bar}] {pct}%", end="", flush=True)

    try:
        urllib.request.urlretrieve(MODEL_URL, DEST, reporthook=progress)
    except Exception as e:
        print(f"\nDownload failed: {e}", file=sys.stderr)
        DEST.unlink(missing_ok=True)
        sys.exit(1)

    print(f"\nDone — {DEST.stat().st_size // 1024} KB saved to {DEST}")


if __name__ == "__main__":
    main()
