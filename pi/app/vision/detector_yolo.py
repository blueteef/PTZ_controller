"""
detector_yolo.py — YOLOv8n object detection via ONNX Runtime.

Does NOT require PyTorch or ultralytics at runtime — uses onnxruntime only.
The ONNX model must be pre-exported once on a machine that has torch:
    python scripts/export_yolo_onnx.py

Model input:  float32 NCHW [1, 3, 640, 640], normalised 0–1
Model output: float32 [1, 84, 8400]
              cols 0–3  : cx, cy, w, h  (in 640×640 space)
              cols 4–83 : COCO class scores (80 classes)
"""

from __future__ import annotations

import logging
from pathlib import Path

import cv2
import numpy as np

from app import config
from app.state import Detection

log = logging.getLogger(__name__)

_INPUT_SIZE   = 640
_NMS_IOU_THR  = 0.45


class YOLODetector:
    def __init__(self) -> None:
        self._session   = None  # lazy init
        self._in_name   = None
        self._names: dict[int, str] = {}

    def _ensure_init(self) -> None:
        if self._session is not None:
            return

        model_path = Path(config.MODEL_YOLO_PATH).with_suffix(".onnx")
        if not model_path.exists():
            raise FileNotFoundError(
                f"ONNX model not found: {model_path}\n"
                "Run  python scripts/export_yolo_onnx.py  on a PC with torch, "
                "then copy yolov8n.onnx to pi/models/."
            )

        try:
            import onnxruntime as ort
        except ImportError:
            raise ImportError(
                "onnxruntime not installed. Run:  pip install onnxruntime"
            )

        log.info("Loading YOLO ONNX model from %s", model_path)
        opts = ort.SessionOptions()
        opts.inter_op_num_threads = 2
        opts.intra_op_num_threads = 2
        self._session = ort.InferenceSession(
            str(model_path),
            sess_options=opts,
            providers=["CPUExecutionProvider"],
        )
        self._in_name = self._session.get_inputs()[0].name

        # Read COCO class names from model metadata if available
        meta = self._session.get_modelmeta().custom_metadata_map
        names_str = meta.get("names", "")
        if names_str:
            import ast
            try:
                self._names = ast.literal_eval(names_str)
            except Exception:
                pass
        log.info("YOLOv8n ONNX ready  (%d classes)", len(self._names) or 80)

    # ------------------------------------------------------------------

    def detect(self, frame: np.ndarray) -> list[Detection]:
        self._ensure_init()

        orig_h, orig_w = frame.shape[:2]
        scale_x = orig_w / _INPUT_SIZE
        scale_y = orig_h / _INPUT_SIZE

        # Preprocess: resize → RGB → float32 NCHW normalised 0-1
        resized = cv2.resize(frame, (_INPUT_SIZE, _INPUT_SIZE))
        rgb     = cv2.cvtColor(resized, cv2.COLOR_BGR2RGB)
        blob    = rgb.astype(np.float32) / 255.0
        blob    = blob.transpose(2, 0, 1)[np.newaxis]  # [1, 3, 640, 640]

        # Inference
        raw = self._session.run(None, {self._in_name: blob})[0]  # [1, 84, 8400]
        preds = raw[0].T  # [8400, 84]

        # Split bboxes and class scores
        bboxes  = preds[:, :4]          # cx, cy, w, h  (640-space)
        scores  = preds[:, 4:]          # [8400, 80]
        cls_ids = np.argmax(scores, axis=1)
        confs   = scores[np.arange(len(scores)), cls_ids]

        # Confidence filter
        keep = confs >= config.YOLO_CONFIDENCE
        if not keep.any():
            return []

        bboxes  = bboxes[keep]
        confs   = confs[keep]
        cls_ids = cls_ids[keep]

        # Convert cx,cy,w,h → x1,y1,x2,y2 and scale to original frame
        x1 = (bboxes[:, 0] - bboxes[:, 2] / 2) * scale_x
        y1 = (bboxes[:, 1] - bboxes[:, 3] / 2) * scale_y
        x2 = (bboxes[:, 0] + bboxes[:, 2] / 2) * scale_x
        y2 = (bboxes[:, 1] + bboxes[:, 3] / 2) * scale_y

        # NMS via OpenCV
        boxes_xywh = np.stack([x1, y1, x2 - x1, y2 - y1], axis=1).tolist()
        indices = cv2.dnn.NMSBoxes(
            boxes_xywh, confs.tolist(), config.YOLO_CONFIDENCE, _NMS_IOU_THR
        )
        if len(indices) == 0:
            return []

        detections: list[Detection] = []
        for rank, idx in enumerate(np.array(indices).flatten()):
            label = self._names.get(int(cls_ids[idx]), str(int(cls_ids[idx])))
            detections.append(Detection(
                id=rank,
                label=label,
                confidence=float(confs[idx]),
                x=int(x1[idx]), y=int(y1[idx]),
                w=int(x2[idx] - x1[idx]),
                h=int(y2[idx] - y1[idx]),
            ))

        return detections
