"""
router_settings.py — REST endpoints for motion settings.

GET /api/settings   → current speed/accel from ESP32
PUT /api/settings   → update speed/accel on ESP32 (and optionally save)
"""

from fastapi import APIRouter
from app.schemas import MotionSettingsRequest
from app.serial_bridge.bridge import bridge
from app.serial_bridge import protocol

router = APIRouter(prefix="/api/settings")


@router.get("")
async def get_settings():
    speed_resp = bridge.query(protocol.cmd_get_speed(), timeout=0.4)
    accel_resp = bridge.query(protocol.cmd_get_accel(), timeout=0.4)
    return {
        "max_speed_deg_s": protocol.parse_speed(speed_resp),
        "accel_deg_s2":    protocol.parse_accel(accel_resp),
        "serial_ok":       bridge._is_connected(),
    }


@router.put("")
async def update_settings(req: MotionSettingsRequest, save: bool = False):
    if req.max_speed_deg_s is not None:
        bridge.send(protocol.cmd_set_speed(req.max_speed_deg_s))
    if req.accel_deg_s2 is not None:
        bridge.send(protocol.cmd_set_accel(req.accel_deg_s2))
    if save:
        bridge.send(protocol.cmd_save())
    return {"ok": True}
