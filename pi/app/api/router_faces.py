"""
router_faces.py — REST endpoints for face enrollment and management.

GET    /api/faces              List all enrolled faces
POST   /api/faces/enroll       Enroll a face from the current camera frame
DELETE /api/faces/{face_id}    Delete one encoding by DB id
DELETE /api/faces/name/{name}  Delete all encodings for a person
"""

from __future__ import annotations

import asyncio
import logging
from functools import partial

import cv2
import numpy as np
from fastapi import APIRouter, HTTPException

from app import db
from app.schemas import EnrollPersonRequest
from app.state import state

log = logging.getLogger(__name__)

router = APIRouter(prefix="/api/faces")


@router.get("")
async def list_faces():
    return {"faces": db.list_faces()}


@router.post("/enroll")
async def enroll_face(req: EnrollPersonRequest):
    frame = state.current_frame
    if frame is None:
        raise HTTPException(400, "No camera frame available")

    # Only enroll from detected faces — make sure face detection is active
    face_dets = [d for d in state.last_detections if d.label == "face"]
    if not face_dets:
        raise HTTPException(
            400,
            "No face detected. Enable face detection mode and make sure a face is visible."
        )

    # Use the largest face in frame
    det = max(face_dets, key=lambda d: d.w * d.h)

    try:
        import face_recognition as fr
    except ImportError:
        raise HTTPException(
            500,
            "face_recognition library not installed. Run: pip install face-recognition"
        )

    rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
    location = [(det.y, det.x + det.w, det.y + det.h, det.x)]

    try:
        # Run CPU-intensive encoding in thread pool so it doesn't block the event loop
        loop = asyncio.get_event_loop()
        encodings = await loop.run_in_executor(
            None, partial(fr.face_encodings, rgb, location)
        )
    except Exception as e:
        log.error("face_encodings failed: %s", e)
        raise HTTPException(500, f"Encoding failed: {e}")

    if not encodings:
        raise HTTPException(400, "Could not compute face encoding — is the face clearly visible?")

    face_id = db.enroll_face(req.name, encodings[0])
    log.info("Enrolled face: %s (id=%d)", req.name, face_id)
    return {"ok": True, "id": face_id, "name": req.name}


@router.delete("/name/{name}")
async def delete_face_by_name(name: str):
    count = db.delete_face_by_name(name)
    if count == 0:
        raise HTTPException(404, f"No enrolled face found for '{name}'")
    return {"ok": True, "deleted": count}


@router.delete("/{face_id}")
async def delete_face(face_id: int):
    if not db.delete_face(face_id):
        raise HTTPException(404, "Face not found")
    return {"ok": True}
