"""
thermal.py — Topdon TC002 (or compatible UVC thermal camera) capture.

Opens the device via OpenCV's V4L2 backend in a background thread.

Device discovery order:
  1. THERMAL_DEVICE env var if set to a path (e.g. /dev/video10)
  2. If THERMAL_DEVICE=auto (default): scan /dev/video0–/dev/video19, return
     the first device that opens and yields a frame.  The Pi native camera is
     managed exclusively by libcamera/Picamera2 and is invisible to OpenCV's
     V4L2 backend, so this reliably finds only USB UVC devices.
  3. THERMAL_DEVICE=none disables the thermal camera entirely.

Frame format handling:
  3-channel BGR  — device delivers pseudo-colored YUYV/MJPEG, pass through.
  uint16 Y16     — raw 16-bit temperatures, normalised and colormapped.
  uint8 greyscale — normalised and colormapped.
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
_RECONNECT_DELAY = 2.0   # seconds between reconnect attempts
_MAX_FAIL_FRAMES = 30    # consecutive read failures before reconnect

# Substrings that identify Pi camera / ISP kernel devices — never thermal.
_PI_CAMERA_KEYWORDS = ("unicam", "bcm2835", "rpi", "isp", "imx708", "ov5647")


def _is_usb_video_device(path: str) -> bool:
    """Return False for Pi camera ISP/sensor devices; True for USB UVC devices.

    Reads the V4L2 card name from sysfs without opening the device, so it
    cannot interfere with Picamera2's exclusive access to the native camera.
    """
    name = path.replace("/dev/", "")          # "video0", "video10", …
    sysfs = Path(f"/sys/class/video4linux/{name}/name")
    try:
        card = sysfs.read_text().strip().lower()
        for kw in _PI_CAMERA_KEYWORDS:
            if kw in card:
                log.debug("Skipping %s (Pi camera device: %r)", path, card)
                return False
        return True
    except FileNotFoundError:
        # Device doesn't exist in sysfs — skip it
        return False
    except Exception:
        return True   # Can't tell — let the open probe decide


def _colormap_id() -> int:
    """Return cv2.COLORMAP_* constant from the THERMAL_COLORMAP config string."""
    name = str(getattr(config, "THERMAL_COLORMAP", "INFERNO")).upper()
    return getattr(cv2, f"COLORMAP_{name}", cv2.COLORMAP_INFERNO)


def _to_bgr(raw: np.ndarray, cmap: int) -> np.ndarray:
    """Convert any frame format to a displayable BGR image."""
    if raw.ndim == 3 and raw.shape[2] == 3:
        return raw                                       # already BGR

    if raw.dtype == np.uint16:
        lo = np.percentile(raw, 5)
        hi = np.percentile(raw, 95)
        span = hi - lo
        if span > 0:
            norm = np.clip((raw.astype(np.float32) - lo) / span, 0.0, 1.0)
        else:
            norm = np.zeros_like(raw, dtype=np.float32)
        gray8 = (norm * 255).astype(np.uint8)
    elif raw.ndim == 2:
        gray8 = raw                                      # uint8 greyscale
    else:
        gray8 = raw[:, :, 0]

    return cv2.applyColorMap(gray8, cmap)


class ThermalCamera:
    """Thread-safe thermal camera reader.  Call start() once at app startup."""

    def __init__(self) -> None:
        self._lock   = threading.Lock()
        self._frame: Optional[np.ndarray] = None
        self._stop   = threading.Event()
        self._thread: Optional[threading.Thread] = None
        self.device: Optional[str] = None
        self._available = False

    # ── Public API ────────────────────────────────────────────────────

    @property
    def is_available(self) -> bool:
        return self._available

    @property
    def current_frame(self) -> Optional[np.ndarray]:
        with self._lock:
            return self._frame

    def start(self) -> None:
        hint = str(getattr(config, "THERMAL_DEVICE", "auto"))
        if hint.lower() == "none":
            log.info("Thermal camera disabled (THERMAL_DEVICE=none)")
            return

        dev = self._find_device(hint)
        if dev is None:
            log.warning("No thermal camera found — thermal stream unavailable")
            return

        self.device = dev
        self._available = True
        self._stop.clear()
        self._thread = threading.Thread(
            target=self._run, daemon=True, name="thermal-capture"
        )
        self._thread.start()
        log.info("Thermal camera started on %s", dev)

    def stop(self) -> None:
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=3.0)
        self._available = False
        log.info("Thermal camera stopped")

    # ── Private ───────────────────────────────────────────────────────

    def _find_device(self, hint: str) -> Optional[str]:
        if hint != "auto":
            return hint if self._probe(hint) else None
        for path in _PROBE_PATHS:
            if not _is_usb_video_device(path):
                continue
            if self._probe(path):
                log.info("Thermal: found device at %s", path)
                return path
        return None

    @staticmethod
    def _probe(path: str) -> bool:
        """Return True if the device opens and yields at least one frame."""
        try:
            cap = cv2.VideoCapture(path, cv2.CAP_V4L2)
            if not cap.isOpened():
                cap.release()
                return False
            ret, frame = cap.read()
            cap.release()
            return ret and frame is not None
        except Exception:
            return False

    def _open(self) -> cv2.VideoCapture:
        cap = cv2.VideoCapture(self.device, cv2.CAP_V4L2)
        # Request Y16 (raw 16-bit).  Device silently falls back to its default
        # (usually pseudo-colored YUYV) if Y16 is unsupported.
        cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"Y16 "))
        return cap

    def _run(self) -> None:
        cmap  = _colormap_id()
        cap   = self._open()
        fails = 0

        while not self._stop.is_set():
            ret, raw = cap.read()
            if not ret:
                fails += 1
                if fails >= _MAX_FAIL_FRAMES:
                    log.warning("Thermal: too many read failures, reconnecting to %s", self.device)
                    cap.release()
                    time.sleep(_RECONNECT_DELAY)
                    cap = self._open()
                    fails = 0
                continue

            fails = 0
            bgr = _to_bgr(raw, cmap)
            with self._lock:
                self._frame = bgr

        cap.release()


thermal_camera = ThermalCamera()
