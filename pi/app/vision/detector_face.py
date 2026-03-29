"""
detector_face.py — MediaPipe face detection + optional dlib face recognition.

Detection runs every frame at full speed.
Recognition (encoding + DB match) is gated behind compute_encodings=True and
runs every RECOGNITION_INTERVAL detection passes to keep CPU load manageable
on the Pi (~150ms per face for dlib encoding).

Requires the `face_recognition` package for recognition:
  pip install face-recognition
Detection-only mode works without it.
"""

from __future__ import annotations

import logging
from typing import Optional

import cv2
import numpy as np

from app import config
from app.state import Detection

log = logging.getLogger(__name__)

RECOGNITION_INTERVAL = 5   # run encoding every Nth detect() call


class FaceDetector:
    def __init__(self, compute_encodings: bool = False) -> None:
        self._compute_encodings = compute_encodings
        self._detector = None   # lazy MediaPipe init
        self._rec_counter = 0

    def set_compute_encodings(self, enabled: bool) -> None:
        self._compute_encodings = enabled
        self._rec_counter = 0

    # ------------------------------------------------------------------

    def _ensure_init(self) -> None:
        if self._detector is None:
            try:
                import mediapipe as mp
                self._detector = mp.solutions.face_detection.FaceDetection(
                    model_selection=config.FACE_MODEL_SELECTION,
                    min_detection_confidence=config.FACE_MIN_CONFIDENCE,
                )
                log.info("MediaPipe FaceDetection initialized")
            except Exception as e:
                log.error("Failed to init MediaPipe: %s", e)
                raise

    def detect(self, frame: np.ndarray) -> list[Detection]:
        self._ensure_init()
        h, w = frame.shape[:2]

        rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        results = self._detector.process(rgb)

        detections: list[Detection] = []
        if not results.detections:
            return detections

        for i, det in enumerate(results.detections):
            bb = det.location_data.relative_bounding_box
            x = max(0, int(bb.xmin * w))
            y = max(0, int(bb.ymin * h))
            bw = min(int(bb.width  * w), w - x)
            bh = min(int(bb.height * h), h - y)

            detections.append(Detection(
                id=i,
                label="face",
                confidence=det.score[0],
                x=x, y=y, w=bw, h=bh,
            ))

        # Recognition pass — throttled to every RECOGNITION_INTERVAL calls
        if self._compute_encodings and detections:
            self._rec_counter += 1
            if self._rec_counter >= RECOGNITION_INTERVAL:
                self._rec_counter = 0
                self._run_recognition(frame, detections)

        return detections

    # ------------------------------------------------------------------
    # Recognition helpers
    # ------------------------------------------------------------------

    def _run_recognition(self, frame: np.ndarray,
                         detections: list[Detection]) -> None:
        try:
            import face_recognition as fr
        except ImportError:
            log.warning("face_recognition not installed — recognition disabled")
            self._compute_encodings = False
            return

        from app.db import get_all_encodings
        known = get_all_encodings()
        if not known:
            return

        known_names = [n for n, _ in known]
        known_encs  = [e for _, e in known]

        rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)

        for d in detections:
            # face_recognition location format: (top, right, bottom, left)
            location = [(d.y, d.x + d.w, d.y + d.h, d.x)]
            try:
                encodings = fr.face_encodings(rgb, location)
            except Exception:
                continue

            if not encodings:
                continue

            enc = encodings[0]
            distances = fr.face_distance(known_encs, enc)
            best_idx  = int(np.argmin(distances))

            if distances[best_idx] <= config.FACE_RECOGNITION_TOLERANCE:
                d.name = known_names[best_idx]
