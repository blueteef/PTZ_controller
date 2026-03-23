"""
stream.py — MJPEG frame generator for the HTTP streaming endpoint.

Serves the best available frame (annotated > raw > blank) as a multipart
MJPEG stream.  Detection overlay is composited here from AppState so the
video stays at camera FPS even when the vision pipeline runs slower.

Usage (in router_stream.py):
    from fastapi.responses import StreamingResponse
    from app.camera.stream import mjpeg_generator

    @router.get("/stream")
    async def stream():
        return StreamingResponse(mjpeg_generator(),
                                 media_type="multipart/x-mixed-replace; boundary=frame")
"""

from __future__ import annotations

import asyncio
import logging
from typing import AsyncIterator

import cv2
import numpy as np

from app import config
from app.state import state

log = logging.getLogger(__name__)

_BOUNDARY = b"--frame\r\nContent-Type: image/jpeg\r\n\r\n"
_BLANK    = np.zeros((config.CAMERA_HEIGHT, config.CAMERA_WIDTH, 3), dtype=np.uint8)


async def mjpeg_generator() -> AsyncIterator[bytes]:
    """Async generator — yields MJPEG multipart chunks at target FPS."""
    interval = 1.0 / config.MJPEG_TARGET_FPS
    encode_params = [cv2.IMWRITE_JPEG_QUALITY, config.MJPEG_QUALITY]

    while True:
        frame = state.get_servo_frame()
        if frame is None:
            frame = _blank_frame()

        ok, buf = cv2.imencode(".jpg", frame, encode_params)
        if not ok:
            await asyncio.sleep(interval)
            continue

        yield _BOUNDARY + buf.tobytes() + b"\r\n"
        await asyncio.sleep(interval)


def _blank_frame() -> np.ndarray:
    """Return a black frame with a 'No Signal' label."""
    img = _BLANK.copy()
    cv2.putText(img, "No Signal", (50, config.CAMERA_HEIGHT // 2),
                cv2.FONT_HERSHEY_SIMPLEX, 2, (80, 80, 80), 3)
    return img
