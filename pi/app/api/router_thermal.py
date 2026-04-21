"""
router_thermal.py — Thermal camera MJPEG stream and status endpoints.

GET /thermal/stream  →  multipart/x-mixed-replace MJPEG (same protocol as /stream)
GET /thermal/status  →  JSON { available: bool, device: str | null }
"""

from __future__ import annotations

import asyncio
import logging
from typing import Optional

import cv2
import numpy as np
from fastapi import APIRouter
from fastapi.responses import JSONResponse, StreamingResponse
from pydantic import BaseModel, Field

from app.camera.thermal import thermal_camera, COLORMAPS

router = APIRouter(prefix="/thermal")
log    = logging.getLogger(__name__)

_BOUNDARY      = b"--frame\r\nContent-Type: image/jpeg\r\n\r\n"
_BLANK         = np.zeros((192, 256, 3), dtype=np.uint8)
_ENCODE_PARAMS = [cv2.IMWRITE_JPEG_QUALITY, 80]
_INTERVAL      = 1.0 / 25   # TC002 runs at 25 fps


async def _generator():
    while True:
        frame = thermal_camera.current_frame
        if frame is None:
            frame = _make_blank()

        ok, buf = cv2.imencode(".jpg", frame, _ENCODE_PARAMS)
        if ok:
            yield _BOUNDARY + buf.tobytes() + b"\r\n"
        await asyncio.sleep(_INTERVAL)


def _make_blank() -> np.ndarray:
    img = _BLANK.copy()
    cv2.putText(img, "No Thermal", (10, 100),
                cv2.FONT_HERSHEY_SIMPLEX, 0.55, (60, 60, 60), 1)
    return img


@router.get("/stream")
async def thermal_stream():
    return StreamingResponse(
        _generator(),
        media_type="multipart/x-mixed-replace; boundary=frame",
    )


@router.get("/status")
async def thermal_status():
    return JSONResponse({
        "available": thermal_camera.is_available,
        "device":    thermal_camera.device,
        "settings":  thermal_camera.get_settings(),
        "colormaps": COLORMAPS,
    })


class ThermalSettingsBody(BaseModel):
    colormap:   Optional[str]   = None
    brightness: Optional[int]   = Field(None, ge=-100, le=100)
    contrast:   Optional[float] = Field(None, ge=0.5,  le=3.0)


@router.put("/settings")
async def update_thermal_settings(body: ThermalSettingsBody):
    updates = {k: v for k, v in body.model_dump().items() if v is not None}
    if "colormap" in updates and updates["colormap"] not in COLORMAPS:
        return JSONResponse({"error": f"unknown colormap"}, status_code=400)
    thermal_camera.update_settings(**updates)
    return JSONResponse(thermal_camera.get_settings())
