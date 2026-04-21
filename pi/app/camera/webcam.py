"""
webcam.py — USB webcam capture (secondary visible camera).

Auto-discovers the first USB V4L2 device that is NOT the TC001 thermal
camera (identified by rejecting 256x192 resolution).  Captures MJPEG
at the configured resolution and serves frames via current_frame.
"""

from __future__ import annotations

import logging
import threading
import time
from pathlib import Path
from typing import Optional

import cv2
import numpy as np

from app import config

log = logging.getLogger(__name__)

_PROBE_PATHS     = [f"/dev/video{i}" for i in range(20)]
_RECONNECT_DELAY = 2.0
_MAX_FAIL_FRAMES = 30

_PI_CAMERA_KEYWORDS = ("unicam", "bcm2835", "rpi", "isp", "imx708", "ov5647", "imx290", "imx462")


def _is_usb_video_device(path: str) -> bool:
    name  = path.replace("/dev/", "")
    sysfs = Path(f"/sys/class/video4linux/{name}/name")
    try:
        card = sysfs.read_text().strip().lower()
        return not any(kw in card for kw in _PI_CAMERA_KEYWORDS)
    except FileNotFoundError:
        return False
    except Exception:
        return True


def _path_to_index(path: str) -> int:
    return int(path.replace("/dev/video", ""))


class WebcamCapture:
    """Thread-safe USB webcam reader.  Call start() once at app startup."""

    def __init__(self) -> None:
        self._lock   = threading.Lock()
        self._frame: Optional[np.ndarray] = None
        self._stop   = threading.Event()
        self._thread: Optional[threading.Thread] = None
        self.device: Optional[str] = None
        self._available = False

    @property
    def is_available(self) -> bool:
        return self._available

    @property
    def current_frame(self) -> Optional[np.ndarray]:
        with self._lock:
            return self._frame

    def start(self) -> None:
        hint = str(getattr(config, "WEBCAM_DEVICE", "auto"))
        if hint.lower() == "none":
            log.info("Webcam disabled (WEBCAM_DEVICE=none)")
            return

        dev = self._find_device(hint)
        if dev is None:
            log.warning("No webcam found — webcam stream unavailable")
            return

        self.device = dev
        self._available = True
        self._stop.clear()
        self._thread = threading.Thread(target=self._run, daemon=True, name="webcam-capture")
        self._thread.start()
        log.info("Webcam started on %s", dev)

    def stop(self) -> None:
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=3.0)
        self._available = False

    def _find_device(self, hint: str) -> Optional[str]:
        if hint != "auto":
            return hint if self._probe(hint) else None
        for path in _PROBE_PATHS:
            if not _is_usb_video_device(path):
                continue
            if self._probe(path):
                log.info("Webcam: found device at %s", path)
                return path
        return None

    def _probe(self, path: str) -> bool:
        """Accept device only if it rejects 256x192 (not TC001) and yields a frame."""
        try:
            cap = cv2.VideoCapture(_path_to_index(path), cv2.CAP_V4L2)
            if not cap.isOpened():
                cap.release()
                return False
            # TC001 accepts 256x192 — skip any device that does
            cap.set(cv2.CAP_PROP_FRAME_WIDTH,  256)
            cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 192)
            if cap.get(cv2.CAP_PROP_FRAME_WIDTH) == 256 and cap.get(cv2.CAP_PROP_FRAME_HEIGHT) == 192:
                log.debug("Skipping %s — accepted 256x192 (thermal device)", path)
                cap.release()
                return False
            # Configure for normal webcam use
            cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
            cap.set(cv2.CAP_PROP_FRAME_WIDTH,  getattr(config, "WEBCAM_WIDTH",  1280))
            cap.set(cv2.CAP_PROP_FRAME_HEIGHT, getattr(config, "WEBCAM_HEIGHT", 720))
            cap.set(cv2.CAP_PROP_FPS, getattr(config, "WEBCAM_FPS", 30))
            ret, frame = cap.read()
            cap.release()
            return ret and frame is not None
        except Exception:
            return False

    def _open(self) -> cv2.VideoCapture:
        cap = cv2.VideoCapture(_path_to_index(self.device), cv2.CAP_V4L2)
        cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
        cap.set(cv2.CAP_PROP_FRAME_WIDTH,  getattr(config, "WEBCAM_WIDTH",  1280))
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, getattr(config, "WEBCAM_HEIGHT", 720))
        cap.set(cv2.CAP_PROP_FPS, getattr(config, "WEBCAM_FPS", 30))
        return cap

    def _run(self) -> None:
        cap   = self._open()
        fails = 0

        while not self._stop.is_set():
            ret, frame = cap.read()
            if not ret:
                fails += 1
                if fails >= _MAX_FAIL_FRAMES:
                    log.warning("Webcam: too many failures, reconnecting to %s", self.device)
                    cap.release()
                    time.sleep(_RECONNECT_DELAY)
                    cap = self._open()
                    fails = 0
                continue
            fails = 0
            with self._lock:
                self._frame = frame

        cap.release()


webcam = WebcamCapture()
