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
        if config.PAN_INVERT:  pan  = -pan
        if config.TILT_INVERT: tilt = -tilt
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

    elif t == "set_tracking":
        enabled   = bool(msg.get("enabled", False))
        target_id = msg.get("target_id")
        from app.vision.tracker import tracker
        if tracker is None:
            await ws.send_json(make_error("Tracker not initialised"))
            return
        if enabled:
            tracker.start(target_id)
        else:
            tracker.stop()

    else:
        await ws.send_json(make_error(f"Unknown message type: '{t}'"))
