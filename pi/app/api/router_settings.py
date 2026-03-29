"""
router_settings.py — REST endpoints for motion settings.

Pi is the single source of truth.  GET reads Pi config (not queried from ESP32).
PUT updates Pi config in RAM and immediately sends the change to the ESP32.
Settings are re-pushed automatically on every ESP32 connect.

GET /api/settings/ui  → UI slider initial values
GET /api/settings     → all current motion settings from Pi config
PUT /api/settings     → update Pi config + send to ESP32
"""

from fastapi import APIRouter
from app import config
from app.schemas import MotionSettingsRequest
from app.serial_bridge.bridge import bridge
from app.serial_bridge import protocol

router = APIRouter(prefix="/api/settings")


@router.get("/ui")
async def get_ui_config():
    """Frontend config values — initial state for all UI sliders/toggles."""
    return {
        "max_speed_deg_s":   config.MAX_SPEED_DEG_S,
        "accel_deg_s2":      config.ACCEL_DEG_S2,
        "fine_speed_scale":  config.FINE_SPEED_SCALE,
        "tracking_speed":    config.PID_MAX_VEL_DEG_S,
        "pan_invert":        config.PAN_INVERT,
        "tilt_invert":       config.TILT_INVERT,
        "soft_limits_enabled": config.SOFT_LIMITS_ENABLED,
        "pan_min":           config.PAN_SOFT_LIMIT_MIN,
        "pan_max":           config.PAN_SOFT_LIMIT_MAX,
        "tilt_min":          config.TILT_SOFT_LIMIT_MIN,
        "tilt_max":          config.TILT_SOFT_LIMIT_MAX,
    }


@router.get("")
async def get_settings():
    """Return all motion settings from Pi config (authoritative source)."""
    return {
        "max_speed_deg_s":    config.MAX_SPEED_DEG_S,
        "accel_deg_s2":       config.ACCEL_DEG_S2,
        "fine_speed_scale":   config.FINE_SPEED_SCALE,
        "pan_invert":         config.PAN_INVERT,
        "tilt_invert":        config.TILT_INVERT,
        "soft_limits_enabled": config.SOFT_LIMITS_ENABLED,
        "pan_min":            config.PAN_SOFT_LIMIT_MIN,
        "pan_max":            config.PAN_SOFT_LIMIT_MAX,
        "tilt_min":           config.TILT_SOFT_LIMIT_MIN,
        "tilt_max":           config.TILT_SOFT_LIMIT_MAX,
        "serial_connected":   bridge._is_connected(),
    }


@router.put("")
async def update_settings(req: MotionSettingsRequest):
    """Update Pi config in RAM and push changed values to ESP32."""
    if req.max_speed_deg_s is not None:
        config.MAX_SPEED_DEG_S = req.max_speed_deg_s
        bridge.send(protocol.cmd_set_speed(config.MAX_SPEED_DEG_S))
    if req.accel_deg_s2 is not None:
        config.ACCEL_DEG_S2 = req.accel_deg_s2
        bridge.send(protocol.cmd_set_accel(config.ACCEL_DEG_S2))
    if req.pan_invert is not None:
        config.PAN_INVERT = req.pan_invert
        bridge.send(protocol.cmd_set_invert("pan", config.PAN_INVERT))
    if req.tilt_invert is not None:
        config.TILT_INVERT = req.tilt_invert
        bridge.send(protocol.cmd_set_invert("tilt", config.TILT_INVERT))
    return {"ok": True}
