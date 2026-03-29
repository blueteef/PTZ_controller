"""
config.py — All tuneable constants for the Pi application.

Values can be overridden via environment variables or a .env file.
Load order: defaults below → .env file → environment.
"""

import os
from pathlib import Path

# Load .env file if present (must happen before any os.getenv calls)
try:
    from dotenv import load_dotenv
    load_dotenv(Path(__file__).parent.parent / ".env")
except ImportError:
    pass

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
PI_DIR   = Path(__file__).parent.parent          # pi/
DATA_DIR = PI_DIR / "data"
MODEL_DIR = PI_DIR / "models"

DATA_DIR.mkdir(exist_ok=True)
MODEL_DIR.mkdir(exist_ok=True)

DB_PATH              = str(DATA_DIR / "faces.db")
MODEL_YOLO_PATH      = str(MODEL_DIR / "yolov8n.pt")
MODEL_OBJECT_PATH    = str(MODEL_DIR / "efficientdet_lite0_int8.tflite")

# ---------------------------------------------------------------------------
# ESP32 serial bridge
# ---------------------------------------------------------------------------
SERIAL_PORT        = os.getenv("SERIAL_PORT", "/dev/serial0")
SERIAL_BAUD        = int(os.getenv("SERIAL_BAUD", "115200"))
SERIAL_TIMEOUT     = 0.1   # seconds — read timeout per call
SERIAL_RECONNECT_S = 2.0   # wait between reconnect attempts

# ---------------------------------------------------------------------------
# Camera
# ---------------------------------------------------------------------------
CAMERA_WIDTH   = int(os.getenv("CAMERA_WIDTH",  "1280"))
CAMERA_HEIGHT  = int(os.getenv("CAMERA_HEIGHT", "720"))
CAMERA_FPS     = int(os.getenv("CAMERA_FPS",    "30"))

# IMX708 (Arducam/Pi Camera Module 3) field of view.
# Full-sensor binned mode (~66°H x 41°V) — applies at all output resolutions.
# Tune empirically by pointing at a target at known angles.
CAMERA_FOV_H_DEG = 66.0   # horizontal degrees
CAMERA_FOV_V_DEG = 41.0   # vertical degrees

# Camera orientation correction — set based on physical mount.
# 0=none, 1=90°CW, 2=180°(upside-down), 3=90°CCW
CAMERA_ROTATION = int(os.getenv("CAMERA_ROTATION", "0"))

# Image quality tuning.
# Sharpness 0-16 (default 1.0): higher = crisper edges, helps detection.
# Contrast  0-32 (default 1.0): slight boost separates features.
# NoiseReductionMode: 0=off 1=fast 2=high-quality
# AwbMode: 0=auto 1=incandescent 2=tungsten 3=fluorescent 4=indoor 5=daylight 6=cloudy
# AeMeteringMode: 0=centre-weighted 1=spot 2=matrix
CAMERA_SHARPNESS          = float(os.getenv("CAMERA_SHARPNESS",    "1.5"))
CAMERA_CONTRAST           = float(os.getenv("CAMERA_CONTRAST",     "1.1"))
CAMERA_NOISE_REDUCTION    = int(os.getenv("CAMERA_NOISE_REDUCTION", "1"))
CAMERA_AWB_MODE           = int(os.getenv("CAMERA_AWB_MODE",        "0"))
CAMERA_AE_METERING_MODE   = int(os.getenv("CAMERA_AE_METERING_MODE","0"))

# Autofocus — IMX708 only (Camera Module 3 / Arducam 12MP AF).
# AfMode: 0=manual  1=auto (trigger on demand)  2=continuous
# AfSpeed: 0=normal  1=fast
# AfRange: 0=normal (0.1m–inf)  1=macro  2=full
CAMERA_AF_MODE  = int(os.getenv("CAMERA_AF_MODE",  "2"))   # continuous
CAMERA_AF_SPEED = int(os.getenv("CAMERA_AF_SPEED", "1"))   # fast
CAMERA_AF_RANGE = int(os.getenv("CAMERA_AF_RANGE", "0"))   # normal

# ---------------------------------------------------------------------------
# MJPEG stream
# ---------------------------------------------------------------------------
MJPEG_QUALITY      = int(os.getenv("MJPEG_QUALITY", "75"))
MJPEG_TARGET_FPS   = CAMERA_FPS   # stream pacing

# ---------------------------------------------------------------------------
# Vision / detection
# ---------------------------------------------------------------------------
YOLO_CONFIDENCE    = float(os.getenv("YOLO_CONFIDENCE", "0.45"))
FACE_MIN_CONFIDENCE  = float(os.getenv("FACE_MIN_CONFIDENCE", "0.4"))  # MediaPipe threshold
FACE_MODEL_SELECTION = int(os.getenv("FACE_MODEL_SELECTION", "1"))     # 0=short range (<2m), 1=full range

# Detection performance — reduce CPU load on Pi
# DET_SCALE:      resize frame before object detection (YOLO/EfficientDet).  0.5 = half res.
#                 Bounding boxes are automatically scaled back to full resolution.
# FACE_DET_SCALE: scale for face detection.  MediaPipe face detection is fast enough at full
#                 resolution and is much more accurate — keep at 1.0 unless CPU is overloaded.
# DET_SKIP:       only run detector every N frames; reuse previous boxes in between.
DET_SCALE      = float(os.getenv("DET_SCALE",      "0.5"))
FACE_DET_SCALE = float(os.getenv("FACE_DET_SCALE", "1.0"))
DET_SKIP       = int(os.getenv("DET_SKIP",         "2"))

# ---------------------------------------------------------------------------
# Face recognition
# ---------------------------------------------------------------------------
FACE_RECOGNITION_TOLERANCE = float(os.getenv("FACE_RECOGNITION_TOLERANCE", "0.5"))
FACE_ENROLL_FRAMES         = 5   # frames captured per enrollment session

# ---------------------------------------------------------------------------
# Motion settings — Pi is the single source of truth.
# These are pushed to the ESP32 on every connect via push_settings().
# Changing them here (or via .env) is the only place you need to edit.
# ---------------------------------------------------------------------------
MAX_SPEED_DEG_S  = float(os.getenv("MAX_SPEED_DEG_S",  "45.0"))
ACCEL_DEG_S2     = float(os.getenv("ACCEL_DEG_S2",    "120.0"))
FINE_SPEED_SCALE = float(os.getenv("FINE_SPEED_SCALE",  "0.2"))

# Axis direction invert — flips the DIR pin on the ESP32 A4988 driver.
# Set in .env: PAN_INVERT=true  or  TILT_INVERT=true
PAN_INVERT  = os.getenv("PAN_INVERT",  "false").lower() == "true"
TILT_INVERT = os.getenv("TILT_INVERT", "false").lower() == "true"

# Soft limits (degrees at output shaft)
PAN_SOFT_LIMIT_MIN  = float(os.getenv("PAN_SOFT_LIMIT_MIN",  "-180.0"))
PAN_SOFT_LIMIT_MAX  = float(os.getenv("PAN_SOFT_LIMIT_MAX",   "180.0"))
TILT_SOFT_LIMIT_MIN = float(os.getenv("TILT_SOFT_LIMIT_MIN",  "-45.0"))
TILT_SOFT_LIMIT_MAX = float(os.getenv("TILT_SOFT_LIMIT_MAX",   "90.0"))
SOFT_LIMITS_ENABLED = os.getenv("SOFT_LIMITS_ENABLED", "false").lower() == "true"

# ---------------------------------------------------------------------------
# Tracking PID
# ---------------------------------------------------------------------------
PID_KP             = 2.0     # proportional gain  (error in degrees → deg/s)
PID_KI             = 0.01    # integral gain
PID_KD             = 0.05    # derivative gain
PID_MAX_VEL_DEG_S  = float(os.getenv("PID_MAX_VEL_DEG_S", "45.0"))  # tracking speed cap
TRACKING_DEADBAND_PX = 15    # pixel radius around center — no correction inside

# Coast mode: if no detection for this many frames, decay velocity to zero
TRACKING_COAST_FRAMES = 5

# ---------------------------------------------------------------------------
# Telemetry
# ---------------------------------------------------------------------------
TELEMETRY_INTERVAL_S = 0.1   # how often to push telemetry to WebSocket clients

# ---------------------------------------------------------------------------
# Web server
# ---------------------------------------------------------------------------
HOST = "0.0.0.0"
PORT = 8000

# ---------------------------------------------------------------------------
# Claude API (optional)
# ---------------------------------------------------------------------------
ANTHROPIC_API_KEY = os.getenv("ANTHROPIC_API_KEY", "")
CLAUDE_MODEL      = "claude-haiku-4-5-20251001"
