"""
protocol.py — Pure command-string builders for the ESP32 CLI protocol.

No I/O here — just functions that return the exact bytes to send.
The CLI wire format is: ASCII text line terminated with \\n.
The ESP32 echoes each character and prints 'ptz> ' after each command;
the bridge discards this noise (fire-and-forget path) or reads past it
(query path).

ESP32 CLI command reference (relevant subset):
  vel  <pan|tilt|all>  <deg_s>        Continuous velocity — no response printed
  stop [pan|tilt|all]                 Decelerate to zero
  estop                               Immediate hard stop
  home [pan|tilt|all]                 Trigger homing sequence
  get  position                       Returns: "pan  : X.XXX °\\ntilt : X.XXX °"
  get  speed                          Returns: "max speed : X.XX °/s"
  set  speed <deg_s>                  Set max speed
  set  accel <deg_s2>                 Set acceleration
  set  limits on|off                  Toggle soft limits
  enable  [pan|tilt|all]              Enable driver output stage
  save                                Persist settings to flash
  ping [pan|tilt|all]                 UART link test → "ping pan ... OK"
"""

import re
from typing import Optional


# ---------------------------------------------------------------------------
# Motion commands
# ---------------------------------------------------------------------------

def cmd_vel(axis: str, deg_s: float) -> str:
    """
    Set continuous velocity on an axis.
    axis: "pan" | "tilt" | "all"
    deg_s: signed, degrees per second at output shaft.
           Positive = CW (pan) / up (tilt), negative = opposite.
    No response is printed by the ESP32, so no reply needs to be read.
    """
    return f"vel {axis} {deg_s:.3f}\n"


def cmd_stop(axis: str = "all") -> str:
    """Decelerate axis to zero."""
    return f"stop {axis}\n"


def cmd_estop() -> str:
    """Immediate hard stop. Requires 'enable all' to resume."""
    return "estop\n"


def cmd_home(axis: str = "all") -> str:
    """Trigger homing sequence."""
    return f"home {axis}\n"


def cmd_enable(axis: str = "all") -> str:
    return f"enable {axis}\n"


def cmd_disable(axis: str = "all") -> str:
    return f"disable {axis}\n"


# ---------------------------------------------------------------------------
# Query commands
# ---------------------------------------------------------------------------

def cmd_get_position() -> str:
    return "get position\n"


def cmd_get_speed() -> str:
    return "get speed\n"


def cmd_get_accel() -> str:
    return "get accel\n"


def cmd_ping(axis: str = "all") -> str:
    return f"ping {axis}\n"


# ---------------------------------------------------------------------------
# Settings commands
# ---------------------------------------------------------------------------

def cmd_set_speed(deg_s: float) -> str:
    return f"set speed {deg_s:.2f}\n"


def cmd_set_accel(deg_s2: float) -> str:
    return f"set accel {deg_s2:.2f}\n"


def cmd_save() -> str:
    return "save\n"


# ---------------------------------------------------------------------------
# Response parsers
# ---------------------------------------------------------------------------

# Pattern for "get position" response:
#   pan  : 45.123 °\r\n
#   tilt : -10.456 °\r\n
_PAN_RE  = re.compile(r"pan\s*:\s*([-\d.]+)")
_TILT_RE = re.compile(r"tilt\s*:\s*([-\d.]+)")

def parse_position(response: str) -> Optional[tuple[float, float]]:
    """
    Parse the multi-line response from 'get position'.
    Returns (pan_deg, tilt_deg) or None if the response is malformed.
    """
    pan_m  = _PAN_RE.search(response)
    tilt_m = _TILT_RE.search(response)
    if not pan_m or not tilt_m:
        return None
    try:
        return float(pan_m.group(1)), float(tilt_m.group(1))
    except ValueError:
        return None


_SPEED_RE = re.compile(r"max speed\s*:\s*([\d.]+)")
_ACCEL_RE = re.compile(r"accel\s*:\s*([\d.]+)")

def parse_speed(response: str) -> Optional[float]:
    m = _SPEED_RE.search(response)
    return float(m.group(1)) if m else None


def parse_accel(response: str) -> Optional[float]:
    m = _ACCEL_RE.search(response)
    return float(m.group(1)) if m else None
