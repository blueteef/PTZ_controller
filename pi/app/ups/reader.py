"""
ups/reader.py — MakerFocus UPSPack V3P battery monitor.

Reads the continuous ASCII stream from the UPS over UART:
    $ SmartUPS 3.2, Vin GOOD, BATCAP 85, Vout 5000 $

Also monitors the STA pin (GPIO4 by default) for a low-battery shutdown
signal — the UPS pulls STA LOW when battery is critically depleted, giving
the Pi a few seconds to shut down gracefully.

Prerequisites in /boot/firmware/config.txt:
    enable_uart=1
    dtoverlay=disable-bt       # frees ttyAMA0 from Bluetooth
"""

from __future__ import annotations

import logging
import os
import re
import subprocess
import threading
import time
from typing import Optional

from app import config
from app.state import state

log = logging.getLogger(__name__)

_PATTERN = re.compile(
    r'\$\s*SmartUPS\s+([\w.]+),\s*Vin\s+(\w+),\s*BATCAP\s+(\d+),\s*Vout\s+(\d+)\s*\$'
)


class UPSReader:
    """Background UART reader for MakerFocus UPSPack V3P."""

    def __init__(self) -> None:
        self._stop   = threading.Event()
        self._thread: Optional[threading.Thread] = None
        self._available = False

    @property
    def is_available(self) -> bool:
        return self._available

    def start(self) -> None:
        port = getattr(config, "UPS_PORT", "/dev/ttyAMA0")
        if str(port).lower() == "none":
            log.info("UPS disabled (UPS_PORT=none)")
            return

        try:
            import serial as _serial_test  # noqa: F401 — verify pyserial present
        except ImportError:
            log.warning("pyserial not installed — UPS monitor disabled")
            return

        self._available = True
        self._stop.clear()
        self._thread = threading.Thread(
            target=self._run, daemon=True, name="ups-reader"
        )
        self._thread.start()
        self._start_sta_monitor()
        log.info("UPS reader started on %s", port)

    def stop(self) -> None:
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=3.0)
        self._available = False

    # ── UART reader ───────────────────────────────────────────────────

    def _run(self) -> None:
        import serial

        port     = getattr(config, "UPS_PORT", "/dev/ttyAMA0")
        buf      = ""
        failures = 0

        while not self._stop.is_set():
            try:
                with serial.Serial(port, 9600, timeout=2.0) as ser:
                    failures = 0
                    log.info("UPS UART opened on %s", port)
                    while not self._stop.is_set():
                        chunk = ser.read(100).decode("ascii", errors="ignore")
                        if not chunk:
                            continue
                        buf += chunk
                        # Keep buffer from growing unbounded
                        if len(buf) > 512:
                            buf = buf[-256:]
                        self._parse(buf)
                        # Trim processed content — keep only from last '$'
                        last_dollar = buf.rfind("$")
                        if last_dollar > 0:
                            buf = buf[last_dollar:]

            except Exception as exc:
                failures += 1
                log.warning("UPS UART error (%d): %s", failures, exc)
                if not self._stop.is_set():
                    time.sleep(min(failures * 2, 30))

    def _parse(self, buf: str) -> None:
        m = _PATTERN.search(buf)
        if not m:
            return
        version, vin_status, batcap_str, vout_str = m.groups()
        state.sensor_ups = {
            "pct":    int(batcap_str),
            "vout_mv": int(vout_str),
            "vin_ok": vin_status.upper() != "NG",
            "version": version,
        }

    # ── STA pin shutdown monitor ──────────────────────────────────────

    def _start_sta_monitor(self) -> None:
        sta_gpio = getattr(config, "UPS_STA_GPIO", 4)
        try:
            from gpiozero import Button
            sta = Button(sta_gpio, pull_up=False, bounce_time=1.0)
            sta.when_deactivated = self._on_sta_low
            log.info("UPS STA monitor active on GPIO%d", sta_gpio)
        except Exception as exc:
            log.warning("UPS STA monitor unavailable (GPIO%d): %s", sta_gpio, exc)

    def _on_sta_low(self) -> None:
        log.critical("UPS STA LOW — battery critically low, shutting down in 10s")
        state.sensor_ups = {**state.sensor_ups, "pct": 0, "vin_ok": False}
        time.sleep(10)
        log.critical("Executing shutdown now")
        subprocess.run(["sudo", "shutdown", "-h", "now"], check=False)


ups_reader = UPSReader()
