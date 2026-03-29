#!/bin/bash
cd ~/PTZ_controller/pi
source venv/bin/activate
uvicorn app.main:app --host 0.0.0.0 --port 8000
