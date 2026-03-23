"""
router_stream.py — MJPEG video stream endpoint.

GET /stream  →  multipart/x-mixed-replace MJPEG stream.

Compatible with plain <img src="/stream"> in HTML — no JavaScript needed
to display the video.  The browser opens a persistent HTTP connection and
renders each JPEG as it arrives.
"""

from fastapi import APIRouter
from fastapi.responses import StreamingResponse

from app.camera.stream import mjpeg_generator

router = APIRouter()


@router.get("/stream")
async def video_stream():
    return StreamingResponse(
        mjpeg_generator(),
        media_type="multipart/x-mixed-replace; boundary=frame",
    )
