"""
capture.py — Camera capture thread using picamera2.

Runs in its own background thread.  Each captured frame is written to
AppState.current_frame and new_frame_event is set so the vision pipeline
can wake up immediately.

picamera2 is only imported if available — on non-Pi dev machines this
module stubs out gracefully (no camera available, logs a warning).
"""

from __future__ import annotations

import logging
import threading
import time
from typing import Optional

import numpy as np

from app import config
from app.state import state

log = logging.getLogger(__name__)

try:
    from picamera2 import Picamera2
    _PICAMERA_AVAILABLE = True
except ImportError:
    _PICAMERA_AVAILABLE = False
    log.warning("picamera2 not available — camera capture will produce blank frames")


class CameraCapture:
    def __init__(self) -> None:
        self._cam: Optional[object] = None
        self._thread = threading.Thread(target=self._capture_loop, daemon=True, name="camera")
        self._stop_event = threading.Event()

    def start(self) -> None:
        self._stop_event.clear()
        if _PICAMERA_AVAILABLE:
            try:
                self._cam = Picamera2()
                cfg = self._cam.create_video_configuration(
                    main={"size": (config.CAMERA_WIDTH, config.CAMERA_HEIGHT), "format": "BGR888"},
                    controls={"FrameRate": config.CAMERA_FPS},
                )
                self._cam.configure(cfg)
                self._cam.start()
                # Apply image quality and autofocus controls after start.
                self._cam.set_controls({
                    "Sharpness":          config.CAMERA_SHARPNESS,
                    "Contrast":           config.CAMERA_CONTRAST,
                    "NoiseReductionMode": config.CAMERA_NOISE_REDUCTION,
                    "AwbMode":            config.CAMERA_AWB_MODE,
                    "AeMeteringMode":     config.CAMERA_AE_METERING_MODE,
                    "AfMode":             config.CAMERA_AF_MODE,
                    "AfSpeed":            config.CAMERA_AF_SPEED,
                    "AfRange":            config.CAMERA_AF_RANGE,
                })
                log.info("Camera started at %dx%d @ %dfps  sharpness=%.1f contrast=%.1f",
                         config.CAMERA_WIDTH, config.CAMERA_HEIGHT, config.CAMERA_FPS,
                         config.CAMERA_SHARPNESS, config.CAMERA_CONTRAST)
            except Exception as e:
                log.warning("Camera init failed (%s) — running with blank frames", e)
                self._cam = None
        else:
            log.warning("Running without camera — blank frames")
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

    # ------------------------------------------------------------------
    # Capture loop (runs in background thread)
    # ------------------------------------------------------------------

    def _capture_loop(self) -> None:
        frame_interval = 1.0 / config.CAMERA_FPS
        blank = np.zeros((config.CAMERA_HEIGHT, config.CAMERA_WIDTH, 3), dtype=np.uint8)

        while not self._stop_event.is_set():
            t0 = time.monotonic()

            if _PICAMERA_AVAILABLE and self._cam is not None:
                try:
                    # capture_array() with BGR888 format returns BGR directly — no conversion needed
                    import cv2
                    frame = self._cam.capture_array()
                    # Apply rotation correction for physical mount orientation
                    _ROT = {1: cv2.ROTATE_90_CLOCKWISE,
                            2: cv2.ROTATE_180,
                            3: cv2.ROTATE_90_COUNTERCLOCKWISE}
                    if config.CAMERA_ROTATION in _ROT:
                        frame = cv2.rotate(frame, _ROT[config.CAMERA_ROTATION])
                except Exception as e:
                    log.warning("Capture error: %s", e)
                    frame = blank
            else:
                frame = blank

            state.set_frame(frame)

            elapsed = time.monotonic() - t0
            sleep_s = frame_interval - elapsed
            if sleep_s > 0:
                time.sleep(sleep_s)


# Module-level singleton.
camera = CameraCapture()
