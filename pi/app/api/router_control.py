"""
router_control.py — WebSocket endpoint for bidirectional gimbal control.

WS /ws/control

Inbound messages (browser → server):
  {"type": "velocity",      "pan": 30.5, "tilt": -12.0}
  {"type": "stop",          "axis": "all"}
  {"type": "estop"}
  {"type": "home",          "axis": "all"}
  {"type": "set_detection", "mode": "face"}
  {"type": "set_tracking",  "enabled": true, "target_id": 2}

Outbound messages (server → browser, pushed periodically):
  {"type": "telemetry", ...}      every TELEMETRY_INTERVAL_S
  {"type": "detections", ...}     after every vision pipeline pass with detections

Multiple browser clients can connect simultaneously; each gets its own
coroutine but they all share the same AppState / serial bridge.
"""

from __future__ import annotations

import asyncio
import json
import logging

from fastapi import APIRouter, WebSocket, WebSocketDisconnect

from pathlib import Path

from app import config
from app.state import state

from app.schemas import make_telemetry, make_detections, make_error
from app.serial_bridge import protocol
from app.serial_bridge.bridge import bridge
from app.vision.pipeline import pipeline

log = logging.getLogger(__name__)

router = APIRouter()


@router.websocket("/ws/control")
async def ws_control(ws: WebSocket):
    await ws.accept()
    log.info("WebSocket client connected: %s", ws.client)

    telemetry_task = asyncio.create_task(_telemetry_loop(ws))

    try:
        while True:
            raw = await ws.receive_text()
            await _handle_message(ws, raw)
    except WebSocketDisconnect:
        log.info("WebSocket client disconnected")
    except Exception as e:
        log.warning("WebSocket error: %s", e)
    finally:
        telemetry_task.cancel()
        try:
            await telemetry_task
        except asyncio.CancelledError:
            pass


# ---------------------------------------------------------------------------
# Telemetry push loop
# ---------------------------------------------------------------------------

async def _telemetry_loop(ws: WebSocket) -> None:
    """Push telemetry + latest detections to this client on a fixed interval."""
    prev_det_count = -1
    while True:
        await asyncio.sleep(config.TELEMETRY_INTERVAL_S)
        try:
            msg = make_telemetry(
                state.gimbal_pan_deg,
                state.gimbal_tilt_deg,
                state.serial_connected,
                state.tracking_enabled,
                sensor_power=state.sensor_power or None,
                sensor_env=state.sensor_env or None,
                sensor_gps=state.sensor_gps or None,
                sensor_imu=state.sensor_imu or None,
                sensor_mag=state.sensor_mag or None,
                sensor_ups=state.sensor_ups or None,
                stab_roll=state.stab_roll_enabled,
                stab_pitch=state.stab_pitch_enabled,
                stab_heading=state.stab_heading_lock,
            )
            await ws.send_json(msg)

            # Send detections only when they change
            dets = state.last_detections
            if len(dets) != prev_det_count:
                await ws.send_json(make_detections(dets))
                prev_det_count = len(dets)
        except Exception:
            break  # client disconnected; let the outer handler clean up


# ---------------------------------------------------------------------------
# Inbound message dispatch
# ---------------------------------------------------------------------------

async def _handle_message(ws: WebSocket, raw: str) -> None:
    try:
        msg = json.loads(raw)
    except json.JSONDecodeError:
        await ws.send_json(make_error("Invalid JSON"))
        return

    t = msg.get("type")

    if t == "velocity":
        pan  = float(msg.get("pan",  0.0))
        tilt = float(msg.get("tilt", 0.0))
        # Direction invert is handled at the ESP32 hardware level (set invert).
        bridge.send(protocol.cmd_vel("pan",  pan))
        bridge.send(protocol.cmd_vel("tilt", tilt))

    elif t == "stop":
        axis = msg.get("axis", "all")
        # Zero velocity first — MKS CR_UART mode keeps last velocity otherwise
        if axis in ("pan", "all"):
            bridge.send_urgent(protocol.cmd_vel("pan", 0.0))
        if axis in ("tilt", "all"):
            bridge.send_urgent(protocol.cmd_vel("tilt", 0.0))
        bridge.send_urgent(protocol.cmd_stop(axis))

    elif t == "estop":
        bridge.send_urgent(protocol.cmd_vel("pan",  0.0))
        bridge.send_urgent(protocol.cmd_vel("tilt", 0.0))
        bridge.send_urgent(protocol.cmd_estop())

    elif t == "home":
        axis = msg.get("axis", "all")
        bridge.send(protocol.cmd_home(axis))

    elif t == "set_detection":
        mode = msg.get("mode", "none")
        pipeline.set_mode(mode)

    elif t == "update_settings":
        # Runtime tuning — applied immediately, not persisted across restart.
        if "tracking_speed" in msg:
            config.PID_MAX_VEL_DEG_S = float(msg["tracking_speed"])
        if "pan_invert" in msg:
            config.PAN_INVERT = bool(msg["pan_invert"])
            bridge.send(protocol.cmd_set_invert("pan", config.PAN_INVERT))
        if "tilt_invert" in msg:
            config.TILT_INVERT = bool(msg["tilt_invert"])
            bridge.send(protocol.cmd_set_invert("tilt", config.TILT_INVERT))
        if "speed" in msg:
            config.MAX_SPEED_DEG_S = float(msg["speed"])
            bridge.send(protocol.cmd_set_speed(config.MAX_SPEED_DEG_S))
        if "accel" in msg:
            config.ACCEL_DEG_S2 = float(msg["accel"])
            bridge.send(protocol.cmd_set_accel(config.ACCEL_DEG_S2))

    elif t == "set_tracking":
        enabled     = bool(msg.get("enabled", False))
        target_id   = msg.get("target_id")
        target_name = msg.get("target_name")
        from app.vision.tracker import tracker
        if tracker is None:
            await ws.send_json(make_error("Tracker not initialised"))
            return
        if enabled:
            tracker.start(target_id=target_id, target_name=target_name)
        else:
            tracker.stop()

    elif t == "set_recognition":
        enabled = bool(msg.get("enabled", False))
        pipeline.set_recognition(enabled)

    elif t == "calibrate_compass":
        known = float(msg.get("heading", 0)) % 360.0
        current = state.sensor_mag.get("hdg", 0.0)
        delta = (known - current + 540.0) % 360.0 - 180.0   # signed shortest path
        new_offset = (config.MAG_HDG_OFFSET_DEG + delta) % 360.0
        config.MAG_HDG_OFFSET_DEG = new_offset
        _update_env_key("MAG_HDG_OFFSET_DEG", f"{new_offset:.2f}")
        log.info("Compass calibrated: known=%.1f current=%.1f delta=%.1f new_offset=%.2f",
                 known, current, delta, new_offset)
        await ws.send_json({"type": "compass_calibrated", "offset": new_offset})

    elif t == "set_stabilization":
        state.stab_roll_enabled  = bool(msg.get("roll",    False))
        state.stab_pitch_enabled = bool(msg.get("pitch",   False))
        state.stab_heading_lock  = bool(msg.get("heading", False))

    else:
        await ws.send_json(make_error(f"Unknown message type: '{t}'"))


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _update_env_key(key: str, value: str) -> None:
    """Upsert a single key in pi/.env without touching other lines."""
    env_path = Path(config.PI_DIR) / ".env"
    lines = env_path.read_text().splitlines() if env_path.exists() else []
    written = False
    new_lines = []
    for line in lines:
        k = line.split("=", 1)[0].strip()
        if k == key:
            new_lines.append(f"{key}={value}")
            written = True
        else:
            new_lines.append(line)
    if not written:
        new_lines.append(f"{key}={value}")
    env_path.write_text("\n".join(new_lines) + "\n")
