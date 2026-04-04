"""
schemas.py — Pydantic models and WebSocket message shapes.

All WebSocket messages (both directions) are JSON with a 'type' discriminator.
These TypedDicts define the wire format; the same shape is used in both the
Python server and the frontend JavaScript.
"""

from __future__ import annotations

from typing import Literal, Optional
from pydantic import BaseModel


# ---------------------------------------------------------------------------
# REST API request / response bodies
# ---------------------------------------------------------------------------

class DetectionModeRequest(BaseModel):
    mode: Literal["none", "face", "yolo"]

class TrackingStartRequest(BaseModel):
    target_id: Optional[int] = None   # None = first detected object

class EnrollPersonRequest(BaseModel):
    name: str

class MotionSettingsRequest(BaseModel):
    max_speed_deg_s: Optional[float] = None
    accel_deg_s2:    Optional[float] = None
    fine_scale:      Optional[float] = None
    pan_invert:      Optional[bool]  = None
    tilt_invert:     Optional[bool]  = None


# ---------------------------------------------------------------------------
# WebSocket — inbound (browser → server)
# ---------------------------------------------------------------------------
# All messages carry a 'type' field.  Additional fields depend on type.
#
# {"type": "velocity",      "pan": 30.5,  "tilt": -12.0}
# {"type": "stop",          "axis": "all"}
# {"type": "estop"}
# {"type": "home",          "axis": "all"}
# {"type": "set_detection", "mode": "face"}
# {"type": "set_tracking",  "enabled": true, "target_id": 2}


# ---------------------------------------------------------------------------
# WebSocket — outbound (server → browser)
# ---------------------------------------------------------------------------
# {"type": "telemetry",  "pan": 45.2,  "tilt": -10.1,
#                        "serial_ok": true, "tracking_active": false}
#
# {"type": "detections", "objects": [
#     {"id": 0, "label": "face", "confidence": 0.94,
#      "x": 120, "y": 80, "w": 60, "h": 80, "name": "Alice"}
# ]}
#
# {"type": "error", "message": "Serial disconnected"}


def make_telemetry(pan: float, tilt: float,
                   serial_ok: bool, tracking: bool,
                   sensor_power: dict = None,
                   sensor_env:   dict = None,
                   sensor_gps:   dict = None) -> dict:
    msg: dict = {
        "type": "telemetry",
        "pan": round(pan, 2),
        "tilt": round(tilt, 2),
        "serial_ok": serial_ok,
        "tracking_active": tracking,
    }
    if sensor_power:
        msg["power"] = sensor_power
    if sensor_env:
        msg["env"] = sensor_env
    if sensor_gps:
        msg["gps"] = sensor_gps
    return msg


def make_detections(detections: list) -> dict:
    return {
        "type": "detections",
        "objects": [
            {
                "id":         d.id,
                "label":      d.label,
                "confidence": round(d.confidence, 3),
                "x": d.x, "y": d.y, "w": d.w, "h": d.h,
                "name":       d.name,
            }
            for d in detections
        ],
    }


def make_error(message: str) -> dict:
    return {"type": "error", "message": message}
