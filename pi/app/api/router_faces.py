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

    # Collect FACE_ENROLL_FRAMES frames worth of encodings and average them
    # for a more robust enrollment sample.
    import time
    from app import config as cfg

    all_encodings = []
    frames_checked = 0
    deadline = time.monotonic() + 2.0   # 2 second window

    while len(all_encodings) < cfg.FACE_ENROLL_FRAMES and time.monotonic() < deadline:
        f = state.current_frame
        if f is None:
            await asyncio.sleep(0.05)
            continue

        face_dets_now = [d for d in state.last_detections if d.label == "face"]
        if not face_dets_now:
            await asyncio.sleep(0.05)
            continue

        best = max(face_dets_now, key=lambda d: d.w * d.h)
        rgb  = cv2.cvtColor(f, cv2.COLOR_BGR2RGB)
        loc  = [(best.y, best.x + best.w, best.y + best.h, best.x)]

        try:
            loop = asyncio.get_event_loop()
            encs = await loop.run_in_executor(None, partial(fr.face_encodings, rgb, loc))
            if encs:
                all_encodings.append(encs[0])
        except Exception as e:
            log.warning("Encoding frame failed: %s", e)

        frames_checked += 1
        await asyncio.sleep(0.1)   # ~10 fps sampling

    if not all_encodings:
        raise HTTPException(400, "Could not compute face encoding — is the face clearly visible?")

    # Average all collected encodings into one representative vector
    avg_encoding = np.mean(all_encodings, axis=0)
    face_id = db.enroll_face(req.name, avg_encoding)
    log.info("Enrolled face: %s (id=%d, samples=%d)", req.name, face_id, len(all_encodings))
    return {"ok": True, "id": face_id, "name": req.name, "samples": len(all_encodings)}


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
