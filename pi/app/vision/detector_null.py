"""
detector_null.py — Pass-through detector when detection mode is "none".

Returns an empty detection list and passes the frame unchanged.
Zero CPU overhead.
"""

from __future__ import annotations

import numpy as np
from app.state import Detection


class NullDetector:
    def detect(self, frame: np.ndarray) -> list[Detection]:
        return []
