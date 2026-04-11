"""
main.py — FastAPI application factory and lifespan.

Startup order:
  1. ESPBridge threads start (serial connection to ESP32)
  2. CameraCapture thread starts
  3. VisionPipeline thread starts
  4. GimbalTracker initialised (injected with bridge.send)
  5. FastAPI serves routes

Shutdown: threads stopped in reverse order.

Launch:
  cd pi/
  uvicorn app.main:app --host 0.0.0.0 --port 8000

For development (auto-reload, Pi not required):
  uvicorn app.main:app --reload
"""

from __future__ import annotations

import logging
import signal
import sys
from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI
from fastapi.staticfiles import StaticFiles

from app.serial_bridge.bridge import bridge
from app.imu.reader import imu_reader
from app.camera.capture import camera
from app.camera.thermal import thermal_camera
from app.vision.pipeline import pipeline
import app.vision.tracker as tracker_module
from app.vision.tracker import GimbalTracker
from app.api import router_stream, router_control, router_detection, router_settings, router_faces
from app.api import router_thermal
from app import db

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s  %(levelname)-8s  %(name)s  %(message)s",
)
log = logging.getLogger(__name__)

# Ensure Ctrl+C and systemctl stop both work cleanly
signal.signal(signal.SIGINT,  lambda *_: sys.exit(0))
signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))


@asynccontextmanager
async def lifespan(app: FastAPI):
    # ── Startup ────────────────────────────────────────────────────────
    log.info("PTZ Pi server starting up")

    db.init_db()
    bridge.start()
    imu_reader.start()
    camera.start()
    thermal_camera.start()
    pipeline.start()

    # Inject bridge into tracker
    tracker_module.tracker = GimbalTracker(send_fn=bridge.send)

    log.info("All subsystems started")
    yield

    # ── Shutdown ───────────────────────────────────────────────────────
    log.info("Shutting down")
    if tracker_module.tracker:
        tracker_module.tracker.stop()
    pipeline.stop()
    camera.stop()
    thermal_camera.stop()
    imu_reader.stop()
    bridge.stop()
    log.info("Shutdown complete")


app = FastAPI(
    title="PTZ Controller",
    description="Raspberry Pi vision and control server for the PTZ gimbal",
    version="2.0.0",
    lifespan=lifespan,
)

# ── Routers ────────────────────────────────────────────────────────────────
app.include_router(router_stream.router)
app.include_router(router_control.router)
app.include_router(router_detection.router)
app.include_router(router_settings.router)
app.include_router(router_faces.router)
app.include_router(router_thermal.router)

# ── Static files (frontend) ────────────────────────────────────────────────
_frontend = Path(__file__).parent / "frontend"
app.mount("/", StaticFiles(directory=str(_frontend), html=True), name="frontend")
