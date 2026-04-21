"""
router_webcam.py — USB webcam MJPEG stream and status endpoint.

GET /webcam/stream  →  multipart/x-mixed-replace MJPEG
GET /webcam/status  →  JSON { available: bool, device: str | null }
"""

from __future__ import annotations

import asyncio
import logging

import cv2
import numpy as np
from fastapi import APIRouter
from fastapi.responses import JSONResponse, StreamingResponse

from app.camera.webcam import webcam
from app import config

router = APIRouter(prefix="/webcam")
log    = logging.getLogger(__name__)

_BOUNDARY      = b"--frame\r\nContent-Type: image/jpeg\r\n\r\n"
_ENCODE_PARAMS = [cv2.IMWRITE_JPEG_QUALITY, getattr(config, "MJPEG_QUALITY", 75)]
_INTERVAL      = 1.0 / getattr(config, "WEBCAM_FPS", 30)

_blank: np.ndarray | None = None


def _make_blank() -> np.ndarray:
    global _blank
    if _blank is None:
        h = getattr(config, "WEBCAM_HEIGHT", 720)
        w = getattr(config, "WEBCAM_WIDTH",  1280)
        img = np.zeros((h, w, 3), dtype=np.uint8)
        cv2.putText(img, "No Webcam", (w // 2 - 80, h // 2),
                    cv2.FONT_HERSHEY_SIMPLEX, 1.0, (60, 60, 60), 2)
        _blank = img
    return _blank


async def _generator():
    while True:
        frame = webcam.current_frame
        if frame is None:
            frame = _make_blank()
        ok, buf = cv2.imencode(".jpg", frame, _ENCODE_PARAMS)
        if ok:
            yield _BOUNDARY + buf.tobytes() + b"\r\n"
        await asyncio.sleep(_INTERVAL)


@router.get("/stream")
async def webcam_stream():
    return StreamingResponse(
        _generator(),
        media_type="multipart/x-mixed-replace; boundary=frame",
    )


@router.get("/status")
async def webcam_status():
    return JSONResponse({
        "available": webcam.is_available,
        "device":    webcam.device,
    })
