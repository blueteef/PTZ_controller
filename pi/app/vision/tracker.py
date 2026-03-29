"""
tracker.py — Gimbal tracking PID controller.

Converts detection centroids to pan/tilt velocity commands sent to the ESP32.

Coordinate convention:
  - Frame origin at top-left
  - Pan error:  positive = target is RIGHT of center → positive pan velocity
  - Tilt error: positive = target is BELOW center   → negative tilt velocity
                (tilt positive = up)

PID is per-axis, reset when tracking target changes or is lost.
"""

from __future__ import annotations

import logging
import time
from typing import Optional

from app import config
from app.state import state, Detection
from app.serial_bridge import protocol

log = logging.getLogger(__name__)


class _PIDAxis:
    def __init__(self) -> None:
        self.reset()

    def reset(self) -> None:
        self._integral  = 0.0
        self._prev_err  = 0.0
        self._prev_time = time.monotonic()

    def update(self, error: float) -> float:
        now = time.monotonic()
        dt  = now - self._prev_time
        if dt <= 0:
            dt = 0.001
        self._prev_time = now

        self._integral += error * dt
        # Anti-windup: clamp integral contribution
        max_i = config.PID_MAX_VEL_DEG_S / max(config.PID_KI, 1e-9)
        self._integral = max(-max_i, min(max_i, self._integral))

        derivative = (error - self._prev_err) / dt
        self._prev_err = error

        output = (config.PID_KP * error
                  + config.PID_KI * self._integral
                  + config.PID_KD * derivative)
        return max(-config.PID_MAX_VEL_DEG_S,
                   min(config.PID_MAX_VEL_DEG_S, output))


class GimbalTracker:
    def __init__(self, send_fn) -> None:
        """
        send_fn: callable(cmd: str) — ESPBridge.send or equivalent.
        """
        self._send = send_fn
        self._pan_pid  = _PIDAxis()
        self._tilt_pid = _PIDAxis()
        self._coast_count = 0

    def start(self, target_id: Optional[int] = None,
              target_name: Optional[str] = None) -> None:
        state.tracking_enabled     = True
        state.tracking_target_id   = target_id
        state.tracking_target_name = target_name
        self._pan_pid.reset()
        self._tilt_pid.reset()
        self._coast_count = 0
        log.info("Tracking started — target_id=%s target_name=%s", target_id, target_name)

    def stop(self) -> None:
        state.tracking_enabled     = False
        state.tracking_target_id   = None
        state.tracking_target_name = None
        self._send(protocol.cmd_stop())
        self._pan_pid.reset()
        self._tilt_pid.reset()
        log.info("Tracking stopped")

    def update(self, detections: list[Detection],
               frame_w: int, frame_h: int) -> None:
        """
        Called by the vision pipeline after each detection pass.
        Computes velocity commands and sends them to the ESP32.
        """
        if not state.tracking_enabled:
            return

        target = self._find_target(detections)

        if target is None:
            self._coast_count += 1
            if self._coast_count >= config.TRACKING_COAST_FRAMES:
                # Lost target — stop and reset
                self._send(protocol.cmd_stop())
                self._pan_pid.reset()
                self._tilt_pid.reset()
            # else: coast (keep last velocity) — no send
            return

        self._coast_count = 0

        # Pixel error from frame center
        cx_err = target.cx - frame_w // 2
        cy_err = target.cy - frame_h // 2

        # Apply deadband
        if (abs(cx_err) < config.TRACKING_DEADBAND_PX and
                abs(cy_err) < config.TRACKING_DEADBAND_PX):
            self._send(protocol.cmd_stop())
            self._pan_pid.reset()
            self._tilt_pid.reset()
            return

        # Convert pixel error → degree error using camera FoV
        pan_err_deg  =  cx_err * (config.CAMERA_FOV_H_DEG / frame_w)
        tilt_err_deg = -cy_err * (config.CAMERA_FOV_V_DEG / frame_h)  # invert Y

        pan_vel  = self._pan_pid.update(pan_err_deg)
        tilt_vel = self._tilt_pid.update(tilt_err_deg)

        if config.PAN_INVERT:  pan_vel  = -pan_vel
        if config.TILT_INVERT: tilt_vel = -tilt_vel

        self._send(protocol.cmd_vel("pan",  pan_vel))
        self._send(protocol.cmd_vel("tilt", tilt_vel))

    # ------------------------------------------------------------------

    def _find_target(self, detections: list[Detection]) -> Optional[Detection]:
        if not detections:
            return None

        # Priority 1: match by recognized name (face recognition lock)
        name = state.tracking_target_name
        if name:
            for d in detections:
                if d.name == name:
                    return d

        # Priority 2: match by detection ID (click-to-track)
        tid = state.tracking_target_id
        if tid is not None:
            for d in detections:
                if d.id == tid:
                    return d

        # Fallback: first detection
        return detections[0]


# Module-level singleton — injected with bridge.send at startup in main.py
tracker: Optional[GimbalTracker] = None
