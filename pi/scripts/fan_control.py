#!/usr/bin/env python3
"""
fan_control.py — CPU temperature-driven fan controller daemon.

GPIO17, high-active MOSFET switch (high = fan ON, low = fan OFF).
Reads temperature from /sys/class/thermal/thermal_zone0/temp.

Hardening:
  - Fan turns ON at startup before first temp read.
  - Fan turns ON if temperature cannot be read (fail-safe).
  - Fan turns ON on SIGTERM/SIGINT before exiting (daemon crash = fan on).
  - Hysteresis prevents rapid cycling (separate on/off thresholds).

Manual override via /run/fan_override:
  echo on   > /run/fan_override    # force on
  echo off  > /run/fan_override    # force off  ← suspends thermal protection
  echo auto > /run/fan_override    # return to thermal control
  rm /run/fan_override             # same as auto

Or use: fan-ctl {on|off|auto|status}

Environment variables (set in systemd unit or shell):
  FAN_GPIO       GPIO pin number (default 17)
  FAN_ON_TEMP    Turn ON above this °C (default 65)
  FAN_OFF_TEMP   Turn OFF below this °C (default 55)
  FAN_POLL_S     Poll interval in seconds (default 5)
"""

from __future__ import annotations

import logging
import os
import signal
import sys
import time
from pathlib import Path

# ── Config ────────────────────────────────────────────────────────────────────
FAN_GPIO        = int(os.getenv("FAN_GPIO",      "17"))
FAN_ON_TEMP     = float(os.getenv("FAN_ON_TEMP",  "65.0"))   # °C — turn on above
FAN_OFF_TEMP    = float(os.getenv("FAN_OFF_TEMP", "55.0"))   # °C — turn off below
FAN_POLL_S      = float(os.getenv("FAN_POLL_S",   "5.0"))    # seconds between checks
MAX_READ_FAILS  = 3                                           # before fail-safe fires
OVERRIDE_FILE   = Path("/run/fan_override")
TEMP_PATH       = Path("/sys/class/thermal/thermal_zone0/temp")

# ── Logging ───────────────────────────────────────────────────────────────────
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s  %(levelname)-8s  fan_control  %(message)s",
)
log = logging.getLogger("fan_control")

# ── GPIO init — fan ON immediately so we're safe before first temp read ───────
try:
    from gpiozero import OutputDevice
    _fan = OutputDevice(FAN_GPIO, active_high=True, initial_value=True)
    log.info("GPIO%d initialised — fan ON (startup safe state)", FAN_GPIO)
except Exception as exc:
    log.critical("GPIO init failed: %s — cannot control fan, exiting", exc)
    sys.exit(1)


# ── Helpers ───────────────────────────────────────────────────────────────────

def _set_fan(on: bool, reason: str) -> None:
    """Drive GPIO and log only when state actually changes."""
    current = bool(_fan.value)
    if current == on:
        return
    _fan.on() if on else _fan.off()
    level = logging.INFO if on else logging.INFO
    log.log(level, "Fan %-3s  reason: %s", "ON" if on else "OFF", reason)


def _read_temp() -> float | None:
    try:
        return int(TEMP_PATH.read_text().strip()) / 1000.0
    except Exception as exc:
        log.warning("Temperature read error: %s", exc)
        return None


def _read_override() -> str | None:
    """Return 'on', 'off', 'auto', or None (= auto) from override file."""
    try:
        val = OVERRIDE_FILE.read_text().strip().lower()
        if val in ("on", "off", "auto"):
            return val
        log.warning("Ignoring unrecognised override value %r", val)
        return None
    except FileNotFoundError:
        return None
    except Exception as exc:
        log.warning("Override file read error: %s", exc)
        return None


# ── Signal handlers — leave fan ON before exit ────────────────────────────────

def _shutdown(signum: int, _frame) -> None:
    log.warning("Signal %d received — setting fan ON (fail-safe) and exiting", signum)
    try:
        _fan.on()
    except Exception:
        pass
    sys.exit(0)


signal.signal(signal.SIGTERM, _shutdown)
signal.signal(signal.SIGINT,  _shutdown)


# ── Main loop ─────────────────────────────────────────────────────────────────

def run() -> None:
    fan_on     = True    # mirrors initial_value=True in GPIO init
    read_fails = 0
    last_temp  = None

    log.info(
        "Thermal fan control started — GPIO%d  on≥%.0f°C  off<%.0f°C  poll=%.0fs",
        FAN_GPIO, FAN_ON_TEMP, FAN_OFF_TEMP, FAN_POLL_S,
    )

    while True:
        override = _read_override()

        if override == "on":
            _set_fan(True, "manual override: on")
            fan_on     = True
            read_fails = 0

        elif override == "off":
            _set_fan(False, "manual override: off — thermal protection SUSPENDED")
            fan_on     = False
            read_fails = 0

        else:
            # Auto / thermal mode
            temp = _read_temp()

            if temp is None:
                read_fails += 1
                log.warning(
                    "Temp read failed (%d/%d consecutive)", read_fails, MAX_READ_FAILS
                )
                if read_fails >= MAX_READ_FAILS:
                    _set_fan(True, f"fail-safe: {read_fails} consecutive read failures")
                    fan_on = True
            else:
                if read_fails > 0:
                    log.info("Temp reads recovered — %.1f°C", temp)
                read_fails = 0
                last_temp  = temp

                if not fan_on and temp >= FAN_ON_TEMP:
                    fan_on = True
                    _set_fan(True, f"{temp:.1f}°C ≥ on-threshold {FAN_ON_TEMP:.0f}°C")
                elif fan_on and temp < FAN_OFF_TEMP:
                    fan_on = False
                    _set_fan(False, f"{temp:.1f}°C < off-threshold {FAN_OFF_TEMP:.0f}°C")

        time.sleep(FAN_POLL_S)


if __name__ == "__main__":
    run()
