"""
detector_yolo.py — Object detection via MediaPipe Tasks ObjectDetector (TFLite).

Uses EfficientDet-Lite0 INT8 — runs natively on Pi without PyTorch or ONNX Runtime.
Download the model once:
    python scripts/download_model.py

Model: efficientdet_lite0_int8.tflite  (~4 MB, COCO 80 classes)
Typical Pi 4 latency: ~80 ms at 640×360  (vs ~400 ms at 1280×720).
"""

from __future__ import annotations

import logging
from pathlib import Path

import cv2
import numpy as np

from app import config
from app.state import Detection

log = logging.getLogger(__name__)


class YOLODetector:
    """Object detector backed by MediaPipe Tasks EfficientDet-Lite0 TFLite model."""

    def __init__(self) -> None:
        self._detector = None  # lazy init

    def _ensure_init(self) -> None:
        if self._detector is not None:
            return

        model_path = Path(config.MODEL_OBJECT_PATH)
        if not model_path.exists():
            raise FileNotFoundError(
                f"TFLite model not found: {model_path}\n"
                "Run  python scripts/download_model.py  to download it."
            )

        try:
            import mediapipe as mp
            from mediapipe.tasks import python as mp_python
            from mediapipe.tasks.python import vision as mp_vision

            base_options = mp_python.BaseOptions(model_asset_path=str(model_path))
            options = mp_vision.ObjectDetectorOptions(
                base_options=base_options,
                score_threshold=config.YOLO_CONFIDENCE,
                max_results=20,
            )
            self._detector = mp_vision.ObjectDetector.create_from_options(options)
            log.info("MediaPipe ObjectDetector ready: %s", model_path.name)
        except Exception as e:
            log.error("Failed to init ObjectDetector: %s", e)
            raise

    def detect(self, frame: np.ndarray) -> list[Detection]:
        self._ensure_init()

        import mediapipe as mp

        # MediaPipe Tasks expects RGB
        rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        mp_image = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb)

        result = self._detector.detect(mp_image)

        detections: list[Detection] = []
        for i, det in enumerate(result.detections):
            if not det.categories:
                continue
            cat = det.categories[0]
            bb = det.bounding_box
            detections.append(Detection(
                id=i,
                label=cat.category_name or str(cat.index),
                confidence=cat.score,
                x=int(bb.origin_x),
                y=int(bb.origin_y),
                w=int(bb.width),
                h=int(bb.height),
            ))

        return detections
