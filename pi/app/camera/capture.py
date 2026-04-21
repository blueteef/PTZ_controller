"""
capture.py — Camera capture thread.

Supports two backends selected by CAMERA_BACKEND in config:
  picamera2 — CSI cameras via libcamera (IMX462, IMX708, etc.)
  v4l2      — USB webcams via OpenCV V4L2
  auto      — try picamera2 first, fall back to v4l2

Switch backends via .env:
  CAMERA_BACKEND=v4l2          # USB webcam (current default)
  CAMERA_BACKEND=picamera2     # CSI camera (IMX462 when connected)
  CAMERA_DEVICE=/dev/video0    # V4L2 device path
"""

from __future__ import annotations

import logging
import threading
import time
from typing import Optional

import cv2
import numpy as np

from app import config
from app.state import state

log = logging.getLogger(__name__)

try:
    from picamera2 import Picamera2
    _PICAMERA_AVAILABLE = True
except ImportError:
    _PICAMERA_AVAILABLE = False


def _device_index(path: str) -> int:
    return int(path.replace("/dev/video", ""))


_ROT_MAP = {
    1: cv2.ROTATE_90_CLOCKWISE,
    2: cv2.ROTATE_180,
    3: cv2.ROTATE_90_COUNTERCLOCKWISE,
}


class CameraCapture:
    def __init__(self) -> None:
        self._cam: Optional[object] = None      # Picamera2 instance
        self._cap: Optional[cv2.VideoCapture] = None  # V4L2 instance
        self._backend: Optional[str] = None
        self._thread = threading.Thread(target=self._capture_loop, daemon=True, name="camera")
        self._stop_event = threading.Event()

    def start(self) -> None:
        self._stop_event.clear()
        backend = config.CAMERA_BACKEND.lower()

        if backend in ("picamera2", "auto") and _PICAMERA_AVAILABLE:
            self._start_picamera2()

        if self._cam is None and backend in ("v4l2", "auto"):
            self._start_v4l2()

        if self._backend is None:
            log.warning("No camera backend available — blank frames")

        self._thread.start()

    def stop(self) -> None:
        self._stop_event.set()
        self._thread.join(timeout=3)
        if self._cam is not None:
            try:
                self._cam.stop()
                self._cam.close()
            except Exception:
                pass
        if self._cap is not None:
            self._cap.release()

    # ------------------------------------------------------------------
    # Backend init
    # ------------------------------------------------------------------

    def _start_picamera2(self) -> None:
        try:
            self._cam = Picamera2(config.CAMERA_NUM)
            cfg = self._cam.create_video_configuration(
                main={"size": (config.CAMERA_WIDTH, config.CAMERA_HEIGHT), "format": "RGB888"},
                controls={"FrameRate": config.CAMERA_FPS},
            )
            self._cam.configure(cfg)
            self._cam.start()

            # Base controls supported by all Pi cameras
            controls = {
                "Sharpness":          config.CAMERA_SHARPNESS,
                "Contrast":           config.CAMERA_CONTRAST,
                "NoiseReductionMode": config.CAMERA_NOISE_REDUCTION,
                "AwbMode":            config.CAMERA_AWB_MODE,
                "AeMeteringMode":     config.CAMERA_AE_METERING_MODE,
            }
            # AF controls only on cameras that support it (IMX708, not IMX462)
            if "AfMode" in self._cam.camera_controls:
                controls.update({
                    "AfMode":  config.CAMERA_AF_MODE,
                    "AfSpeed": config.CAMERA_AF_SPEED,
                    "AfRange": config.CAMERA_AF_RANGE,
                })
            self._cam.set_controls(controls)

            model = self._cam.camera_properties.get("Model", "unknown")
            self._backend = "picamera2"
            log.info("Camera (picamera2) started: %s %dx%d @ %dfps",
                     model, config.CAMERA_WIDTH, config.CAMERA_HEIGHT, config.CAMERA_FPS)
        except Exception as e:
            log.warning("picamera2 init failed (%s) — trying v4l2", e)
            self._cam = None

    def _start_v4l2(self) -> None:
        try:
            idx = _device_index(config.CAMERA_DEVICE)
            cap = cv2.VideoCapture(idx, cv2.CAP_V4L2)
            if not cap.isOpened():
                log.warning("V4L2 camera not available at %s", config.CAMERA_DEVICE)
                return
            # Request MJPEG — USB webcams deliver 30fps at high res in MJPEG;
            # YUYV at the same resolution is typically limited to ~5-10fps.
            cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
            cap.set(cv2.CAP_PROP_FRAME_WIDTH,  config.CAMERA_WIDTH)
            cap.set(cv2.CAP_PROP_FRAME_HEIGHT, config.CAMERA_HEIGHT)
            cap.set(cv2.CAP_PROP_FPS,          config.CAMERA_FPS)
            w   = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
            h   = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
            fps = cap.get(cv2.CAP_PROP_FPS)
            self._cap     = cap
            self._backend = "v4l2"
            log.info("Camera (v4l2/MJPG) started: %s %dx%d @ %.0ffps",
                     config.CAMERA_DEVICE, w, h, fps)
        except Exception as e:
            log.warning("V4L2 camera init failed (%s)", e)

    # ------------------------------------------------------------------
    # Capture loop
    # ------------------------------------------------------------------

    def _capture_loop(self) -> None:
        frame_interval = 1.0 / config.CAMERA_FPS
        blank = np.zeros((config.CAMERA_HEIGHT, config.CAMERA_WIDTH, 3), dtype=np.uint8)

        while not self._stop_event.is_set():
            t0 = time.monotonic()
            frame = self._read_frame(blank)
            state.set_frame(frame)
            elapsed = time.monotonic() - t0
            sleep_s = frame_interval - elapsed
            if sleep_s > 0:
                time.sleep(sleep_s)

    def _read_frame(self, blank: np.ndarray) -> np.ndarray:
        if self._backend == "picamera2" and self._cam is not None:
            try:
                frame_rgb = self._cam.capture_array()
                frame = cv2.cvtColor(frame_rgb, cv2.COLOR_RGB2BGR)
            except Exception as e:
                log.warning("Capture error: %s", e)
                return blank

        elif self._backend == "v4l2" and self._cap is not None:
            ret, frame = self._cap.read()
            if not ret:
                return blank

        else:
            return blank

        if config.CAMERA_ROTATION in _ROT_MAP:
            frame = cv2.rotate(frame, _ROT_MAP[config.CAMERA_ROTATION])
        return frame


# Module-level singleton.
camera = CameraCapture()
