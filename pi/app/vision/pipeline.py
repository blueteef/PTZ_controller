"""
pipeline.py — Vision processing thread.

Reads frames from AppState, dispatches to the active detector,
draws bounding boxes, writes annotated frame + detections back to AppState.

The pipeline thread runs independently of the camera and stream — if
detection is slow, the stream stays smooth (it reads from current_frame
as fallback) and detections are just stale by a few frames.
"""

from __future__ import annotations

import logging
import threading
import time
from typing import Union

import cv2
import numpy as np

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
                # Switching to none stops tracking automatically
                state.tracking_enabled = False
            elif mode == "face":
                self._detector = FaceDetector()
            elif mode == "yolo":
                self._detector = YOLODetector()
            else:
                log.warning("Unknown detection mode '%s'", mode)
                return
        state.detection_mode = mode
        log.info("Detection mode → %s", mode)

    # ------------------------------------------------------------------
    # Main loop
    # ------------------------------------------------------------------

    def _run(self) -> None:
        while not self._stop_event.is_set():
            # Wait for a new frame (up to 0.1s to allow stop checks)
            state.new_frame_event.wait(timeout=0.1)
            state.new_frame_event.clear()

            frame = state.current_frame
            if frame is None:
                continue

            with self._detector_lock:
                detector = self._detector

            try:
                detections = detector.detect(frame)
            except Exception as e:
                log.error("Detection error: %s", e)
                detections = []

            annotated = _draw_boxes(frame, detections)
            state.set_annotated(annotated, detections)

            # Feed detections to tracker (no-op when tracking disabled)
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
