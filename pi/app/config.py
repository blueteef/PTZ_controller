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

DB_PATH            = str(DATA_DIR / "faces.db")
MODEL_YOLO_PATH    = str(MODEL_DIR / "yolov8n.pt")

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

# IMX219 (Pi Camera Module 2 / NoIR v2) field of view at 1280x720 crop mode.
# Full-sensor H FoV ~62°; at 1280x720 crop it's approximately 47°H x 28°V.
# These are estimates — tune empirically by pointing at a target at known angles.
CAMERA_FOV_H_DEG = 47.0   # horizontal degrees
CAMERA_FOV_V_DEG = 28.0   # vertical degrees

# Camera orientation correction — set based on physical mount.
# 0=none, 1=90°CW, 2=180°(upside-down), 3=90°CCW
CAMERA_ROTATION = int(os.getenv("CAMERA_ROTATION", "2"))

# ---------------------------------------------------------------------------
# MJPEG stream
# ---------------------------------------------------------------------------
MJPEG_QUALITY      = int(os.getenv("MJPEG_QUALITY", "75"))
MJPEG_TARGET_FPS   = CAMERA_FPS   # stream pacing

# ---------------------------------------------------------------------------
# Vision / detection
# ---------------------------------------------------------------------------
YOLO_CONFIDENCE    = float(os.getenv("YOLO_CONFIDENCE", "0.45"))
FACE_MIN_CONFIDENCE = 0.6   # MediaPipe face detection threshold

# ---------------------------------------------------------------------------
# Face recognition
# ---------------------------------------------------------------------------
FACE_RECOGNITION_TOLERANCE = float(os.getenv("FACE_RECOGNITION_TOLERANCE", "0.5"))
FACE_ENROLL_FRAMES         = 5   # frames captured per enrollment session

# ---------------------------------------------------------------------------
# Joystick / control
# ---------------------------------------------------------------------------
# Invert axis direction if physical wiring or mount causes reversed motion.
# True = flip the sign of velocity commands on that axis.
PAN_INVERT  = os.getenv("PAN_INVERT",  "false").lower() == "true"
TILT_INVERT = os.getenv("TILT_INVERT", "false").lower() == "true"

# Default max speed for the web UI speed slider (deg/s).
# 180 is the firmware max — start lower for manageable joystick feel.
JOYSTICK_DEFAULT_SPEED = float(os.getenv("JOYSTICK_DEFAULT_SPEED", "45.0"))

# ---------------------------------------------------------------------------
# Tracking PID
# ---------------------------------------------------------------------------
PID_KP             = 0.08    # proportional gain
PID_KI             = 0.001   # integral gain
PID_KD             = 0.01    # derivative gain
PID_MAX_VEL_DEG_S  = float(os.getenv("PID_MAX_VEL_DEG_S", "45.0"))  # tracking speed cap
TRACKING_DEADBAND_PX = 15    # pixel radius around center — no correction inside

# Runtime-mutable motion settings (changed via UI sliders)
ACCEL_DEG_S2 = float(os.getenv("ACCEL_DEG_S2", "120.0"))

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
