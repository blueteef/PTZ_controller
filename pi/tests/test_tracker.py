"""
test_tracker.py — Unit tests for the tracking PID math.
No hardware, no serial, no camera required.
"""

import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent.parent))

from unittest.mock import patch
from app.state import Detection
import app.config as config


def _make_detection(id=0, cx=640, cy=360, label="face"):
    """Helper: make a Detection with the given centroid."""
    w, h = 60, 80
    return Detection(id=id, label=label, confidence=0.9,
                     x=cx - w // 2, y=cy - h // 2, w=w, h=h)


def test_centroid_calculation():
    d = _make_detection(cx=700, cy=400)
    assert d.cx == 700
    assert d.cy == 400


def test_tracker_stop_on_deadband():
    """If target is within deadband, tracker should send stop."""
    sent = []

    with patch("app.state.state") as mock_state:
        mock_state.tracking_enabled   = True
        mock_state.tracking_target_id = None

        from app.vision.tracker import GimbalTracker
        tracker = GimbalTracker(send_fn=sent.append)
        tracker.start()

        # Target exactly at center — inside deadband
        d = _make_detection(cx=640, cy=360)
        mock_state.tracking_target_id = None
        tracker.update([d], frame_w=1280, frame_h=720)

    # Should have sent a stop command
    assert any("stop" in cmd for cmd in sent), f"Expected stop in {sent}"


def test_tracker_no_detections_coast():
    """Missing detections should not immediately send stop (coast mode)."""
    sent = []

    with patch("app.state.state") as mock_state:
        mock_state.tracking_enabled   = True
        mock_state.tracking_target_id = None

        from app.vision.tracker import GimbalTracker
        tracker = GimbalTracker(send_fn=sent.append)
        tracker.start()

        # No detections — first few calls should coast (no send)
        for _ in range(config.TRACKING_COAST_FRAMES - 1):
            tracker.update([], frame_w=1280, frame_h=720)

        assert len(sent) == 0, f"Should not have sent anything yet: {sent}"
