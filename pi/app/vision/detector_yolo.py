"""
detector_yolo.py — YOLOv8-nano object detection (Ultralytics).

Model is downloaded on first use to pi/models/yolov8n.pt.
Run scripts/download_models.sh to pre-download before first launch.

On Pi 4 (no GPU): expect ~10-15fps at 640px input size.
"""

from __future__ import annotations

import logging
from pathlib import Path

import numpy as np

from app import config
from app.state import Detection

log = logging.getLogger(__name__)


class YOLODetector:
    def __init__(self) -> None:
        self._model = None  # lazy init

    def _ensure_init(self) -> None:
        if self._model is None:
            try:
                from ultralytics import YOLO
                model_path = config.MODEL_YOLO_PATH
                # Download if not present (first run)
                self._model = YOLO(model_path)
                log.info("YOLOv8-nano loaded from %s", model_path)
            except Exception as e:
                log.error("Failed to init YOLO: %s", e)
                raise

    def detect(self, frame: np.ndarray) -> list[Detection]:
        self._ensure_init()
        results = self._model(
            frame,
            imgsz=640,
            conf=config.YOLO_CONFIDENCE,
            verbose=False,
        )

        detections: list[Detection] = []
        if not results:
            return detections

        boxes = results[0].boxes
        for i, box in enumerate(boxes):
            x1, y1, x2, y2 = box.xyxy[0].tolist()
            cls_id = int(box.cls[0])
            label  = results[0].names.get(cls_id, str(cls_id))
            conf   = float(box.conf[0])

            detections.append(Detection(
                id=i,
                label=label,
                confidence=conf,
                x=int(x1), y=int(y1),
                w=int(x2 - x1), h=int(y2 - y1),
            ))

        return detections
