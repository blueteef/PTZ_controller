"""
pipeline.py — Vision processing thread.

Reads frames from AppState, dispatches to the active detector,
draws bounding boxes, writes annotated frame + detections back to AppState.

The pipeline thread runs independently of the camera and stream — if
detection is slow, the stream stays smooth (it reads from current_frame
as fallback) and detections are just stale by a few frames.

Performance knobs (config.py / .env):
  DET_SCALE  — resize frame to this fraction before detection (0.5 = half res)
  DET_SKIP   — only run detector every N frames; reuse boxes in between
"""

from __future__ import annotations

import logging
import threading
import time
from typing import Union

import cv2
import numpy as np

from app import config
from app.state import state, Detection
from app.vision.detector_null import NullDetector
from app.vision.detector_face import FaceDetector
from app.vision.detector_yolo import YOLODetector

log = logging.getLogger(__name__)

Detector = Union[NullDetector, FaceDetector, YOLODetector]

# Bounding box colour per detector type
_COLOURS = {
    "face": (0, 220, 0),    # green
    "yolo": (0, 140, 255),  # orange-ish
}


class VisionPipeline:
    def __init__(self) -> None:
        self._detector: Detector = NullDetector()
        self._detector_lock = threading.Lock()
        self._stop_event = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True, name="vision")

    def start(self) -> None:
        self._stop_event.clear()
        self._thread.start()
        log.info("Vision pipeline started")

    def stop(self) -> None:
        self._stop_event.set()
        state.new_frame_event.set()  # wake thread so it can exit
        self._thread.join(timeout=5)
        log.info("Vision pipeline stopped")

    def set_mode(self, mode: str) -> None:
        """
        Switch detection mode.  mode: "none" | "face" | "yolo"
        Thread-safe — can be called from FastAPI handlers.
        """
        with self._detector_lock:
            if mode == "none":
                self._detector = NullDetector()
                state.tracking_enabled = False
            elif mode == "face":
                self._detector = FaceDetector(
                    compute_encodings=state.recognition_enabled
                )
            elif mode == "yolo":
                self._detector = YOLODetector()
            else:
                log.warning("Unknown detection mode '%s'", mode)
                return
        state.detection_mode = mode
        log.info("Detection mode → %s", mode)

    def set_recognition(self, enabled: bool) -> None:
        """
        Toggle face recognition on/off without changing detection mode.
        Only meaningful when mode is 'face'.
        """
        state.recognition_enabled = enabled
        with self._detector_lock:
            if isinstance(self._detector, FaceDetector):
                self._detector.set_compute_encodings(enabled)
        log.info("Face recognition → %s", "on" if enabled else "off")

    # ------------------------------------------------------------------
    # Main loop
    # ------------------------------------------------------------------

    def _run(self) -> None:
        frame_count = 0
        last_detections: list[Detection] = []

        while not self._stop_event.is_set():
            # Wait for a new frame (up to 0.1s to allow stop checks)
            state.new_frame_event.wait(timeout=0.1)
            state.new_frame_event.clear()

            frame = state.current_frame
            if frame is None:
                continue

            frame_count += 1

            # Frame skipping: reuse last detection boxes on skipped frames
            if config.DET_SKIP > 1 and frame_count % config.DET_SKIP != 0:
                annotated = _draw_boxes(frame, last_detections)
                state.set_annotated(annotated, last_detections)
                _feed_tracker(frame, last_detections)
                continue

            with self._detector_lock:
                detector = self._detector

            # Downscale frame for faster detection
            det_frame, scale_x, scale_y = _maybe_downscale(frame)

            try:
                detections = detector.detect(det_frame)
                # Scale bounding boxes back to original resolution
                if scale_x != 1.0 or scale_y != 1.0:
                    for d in detections:
                        d.x = int(d.x * scale_x)
                        d.y = int(d.y * scale_y)
                        d.w = int(d.w * scale_x)
                        d.h = int(d.h * scale_y)
            except Exception as e:
                log.error("Detection error (%s) — resetting to null detector: %s",
                          type(e).__name__, e)
                with self._detector_lock:
                    self._detector = NullDetector()
                state.detection_mode = "none"
                state.tracking_enabled = False
                detections = []

            last_detections = detections
            annotated = _draw_boxes(frame, detections)
            state.set_annotated(annotated, detections)
            _feed_tracker(frame, detections)


def _maybe_downscale(frame: np.ndarray) -> tuple[np.ndarray, float, float]:
    """Return (det_frame, scale_x, scale_y).  scale_* maps det_frame coords → frame coords."""
    scale = config.DET_SCALE
    if scale >= 1.0:
        return frame, 1.0, 1.0
    orig_h, orig_w = frame.shape[:2]
    det_w = max(1, int(orig_w * scale))
    det_h = max(1, int(orig_h * scale))
    det_frame = cv2.resize(frame, (det_w, det_h))
    return det_frame, orig_w / det_w, orig_h / det_h


def _feed_tracker(frame: np.ndarray, detections: list[Detection]) -> None:
    import app.vision.tracker as _t
    if _t.tracker is not None and state.tracking_enabled:
        h, w = frame.shape[:2]
        _t.tracker.update(detections, frame_w=w, frame_h=h)


def _draw_boxes(frame: np.ndarray, detections: list[Detection]) -> np.ndarray:
    if not detections:
        return frame
    out = frame.copy()
    for d in detections:
        colour = _COLOURS.get(d.label, (200, 200, 200))
        cv2.rectangle(out, (d.x, d.y), (d.x + d.w, d.y + d.h), colour, 2)
        label = f"{d.label} {d.confidence:.2f}"
        if d.name:
            label = f"{d.name} ({d.confidence:.2f})"
        cv2.putText(out, label, (d.x, max(d.y - 8, 12)),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.55, colour, 2)
    return out


# Module-level singleton.
pipeline = VisionPipeline()
