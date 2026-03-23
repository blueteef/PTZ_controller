"""
router_detection.py — REST endpoints for detection mode control.

POST /api/detection/mode   {"mode": "none"|"face"|"yolo"}
GET  /api/detection/state  → current mode and detection count
"""

from fastapi import APIRouter
from app.schemas import DetectionModeRequest
from app.state import state
from app.vision.pipeline import pipeline

router = APIRouter(prefix="/api/detection")


@router.post("/mode")
async def set_detection_mode(req: DetectionModeRequest):
    pipeline.set_mode(req.mode)
    return {"ok": True, "mode": req.mode}


@router.get("/state")
async def get_detection_state():
    return {
        "mode":            state.detection_mode,
        "detection_count": len(state.last_detections),
        "tracking":        state.tracking_enabled,
        "target_id":       state.tracking_target_id,
    }
