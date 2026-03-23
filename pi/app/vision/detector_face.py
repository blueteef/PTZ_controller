"""
detector_face.py — MediaPipe face detection.

Returns one Detection per face found in the frame.
The detector is lazy-initialized on first use so import is cheap.

Optionally computes face encodings (dlib 128-d vectors) when
`compute_encodings=True` is passed at init — used by the recognizer.
This is slow (~100ms/face) so only enable it when recognition is active.
"""

from __future__ import annotations

import logging
from typing import Optional

import cv2
import numpy as np

from app import config
from app.state import Detection

log = logging.getLogger(__name__)


class FaceDetector:
    def __init__(self, compute_encodings: bool = False) -> None:
        self._compute_encodings = compute_encodings
        self._detector = None  # lazy init

    def _ensure_init(self) -> None:
        if self._detector is None:
            try:
                import mediapipe as mp
                self._detector = mp.solutions.face_detection.FaceDetection(
                    model_selection=0,
                    min_detection_confidence=config.FACE_MIN_CONFIDENCE,
                )
                log.info("MediaPipe FaceDetection initialized")
            except Exception as e:
                log.error("Failed to init MediaPipe: %s", e)
                raise

    def detect(self, frame: np.ndarray) -> list[Detection]:
        self._ensure_init()
        h, w = frame.shape[:2]

        # MediaPipe expects RGB
        rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        results = self._detector.process(rgb)

        detections: list[Detection] = []
        if not results.detections:
            return detections

        for i, det in enumerate(results.detections):
            bb = det.location_data.relative_bounding_box
            x = max(0, int(bb.xmin * w))
            y = max(0, int(bb.ymin * h))
            bw = int(bb.width * w)
            bh = int(bb.height * h)

            detections.append(Detection(
                id=i,
                label="face",
                confidence=det.score[0],
                x=x, y=y, w=bw, h=bh,
            ))

        return detections

    def set_compute_encodings(self, enabled: bool) -> None:
        self._compute_encodings = enabled
