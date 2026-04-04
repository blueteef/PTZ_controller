"""
state.py — Shared application state (thread-safe).

All mutable runtime state lives here so every component (camera thread,
vision thread, serial bridge thread, FastAPI async handlers) works from
the same source of truth.

Reads are usually lock-free (single Python reference replace is GIL-atomic).
Writes that must be seen atomically acquire `_lock` explicitly.
"""

from __future__ import annotations

import threading
from dataclasses import dataclass, field
from typing import Optional

import numpy as np


@dataclass
class Detection:
    id: int
    label: str
    confidence: float
    x: int        # bounding box — top-left x, pixels
    y: int        # bounding box — top-left y, pixels
    w: int        # width, pixels
    h: int        # height, pixels
    name: Optional[str] = None   # recognized face name (if known)

    @property
    def cx(self) -> int:
        return self.x + self.w // 2

    @property
    def cy(self) -> int:
        return self.y + self.h // 2


class AppState:
    """
    Singleton-style shared state container.
    Use the module-level `state` instance everywhere.
    """

    def __init__(self) -> None:
        self._lock = threading.Lock()

        # Latest raw frame from camera (BGR numpy array), replaced each capture tick.
        self.current_frame: Optional[np.ndarray] = None

        # Unused for streaming (stream.py always serves current_frame).
        # Kept for API consumers that may want the last-processed frame reference.
        self.annotated_frame: Optional[np.ndarray] = None

        # Detection / tracking
        self.detection_mode: str = "none"   # "none" | "face" | "yolo"
        self.recognition_enabled: bool = False
        self.tracking_enabled: bool = False
        self.tracking_target_id: Optional[int] = None     # detection .id to follow
        self.tracking_target_name: Optional[str] = None   # recognized name to follow

        # Last set of detections from the vision pipeline
        self.last_detections: list[Detection] = []

        # Gimbal position as last reported by ESP32
        self.gimbal_pan_deg: float = 0.0
        self.gimbal_tilt_deg: float = 0.0

        # Serial bridge health
        self.serial_connected: bool = False

        # Sensor telemetry pushed by ESP32 ($PWR / $ENV / $GPS / $IMU / $MAG lines)
        self.sensor_power: dict = {}  # {"vin": V, "curr": mA, "pwr": mW}
        self.sensor_env:   dict = {}  # {"temp": °C, "press": hPa, "alt": m}
        self.sensor_gps:   dict = {}  # {"lat", "lon", "fix", "sats", "hdg", "spd"}
        self.sensor_imu:   dict = {}  # {"ok", "roll": °, "pitch": °}
        self.sensor_mag:   dict = {}  # {"ok", "hdg": °}

        # Signal new frame from capture → vision thread
        self.new_frame_event: threading.Event = threading.Event()

    # ------------------------------------------------------------------
    # Convenience setters (acquire lock for multi-field atomicity)
    # ------------------------------------------------------------------

    def set_frame(self, frame: np.ndarray) -> None:
        self.current_frame = frame
        self.new_frame_event.set()

    def set_annotated(self, frame: np.ndarray, detections: list[Detection]) -> None:
        with self._lock:
            self.annotated_frame = frame
            self.last_detections = detections

    def set_gimbal_position(self, pan: float, tilt: float) -> None:
        with self._lock:
            self.gimbal_pan_deg = pan
            self.gimbal_tilt_deg = tilt

    def get_servo_frame(self) -> Optional[np.ndarray]:
        """Return the best available frame for streaming."""
        return self.annotated_frame if self.annotated_frame is not None else self.current_frame


# Module-level singleton — import this everywhere.
state = AppState()
