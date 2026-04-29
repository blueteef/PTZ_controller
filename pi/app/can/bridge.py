"""
bridge.py — CAN bus bridge (replaces serial_bridge for v2 hardware).

Uses python-can against SocketCAN (can0 via MCP2515).
Runs one RX thread and sends heartbeats on a fixed interval.
"""

from __future__ import annotations

import logging
import threading
import time

import can

from app import config
from app.can.protocol import parse_frame, build_heartbeat, build_estop
from app.state import state

log = logging.getLogger(__name__)

_HEARTBEAT_INTERVAL = 1.0   # seconds between Pi heartbeat frames

_bus:       can.BusABC | None = None
_rx_thread: threading.Thread | None = None
_hb_thread: threading.Thread | None = None
_running    = False
_lock       = threading.Lock()


def start() -> None:
    global _bus, _rx_thread, _hb_thread, _running
    try:
        _bus = can.interface.Bus(
            channel=config.CAN_CHANNEL,
            bustype="socketcan",
            bitrate=config.CAN_BITRATE,
        )
        _running = True
        _rx_thread = threading.Thread(target=_rx_loop, daemon=True, name="can-rx")
        _hb_thread = threading.Thread(target=_hb_loop, daemon=True, name="can-heartbeat")
        _rx_thread.start()
        _hb_thread.start()
        log.info("CAN bridge started on %s @ %d bps", config.CAN_CHANNEL, config.CAN_BITRATE)
    except Exception as exc:
        log.error("CAN bridge failed to start: %s", exc)


def stop() -> None:
    global _running, _bus
    _running = False
    if _bus:
        _bus.shutdown()
        _bus = None
    log.info("CAN bridge stopped")


# ---------------------------------------------------------------------------
# Public send helpers
# ---------------------------------------------------------------------------

def send(msg: can.Message) -> None:
    """Send a pre-built CAN message."""
    with _lock:
        if _bus is None:
            return
        try:
            _bus.send(msg)
        except can.CanError as exc:
            log.warning("CAN send error: %s", exc)


def send_estop() -> None:
    send(build_estop())


# ---------------------------------------------------------------------------
# Internal loops
# ---------------------------------------------------------------------------

_CAN_TIMEOUT_S = 3.0   # seconds without a frame before marking bus offline

def _rx_loop() -> None:
    last_rx = time.monotonic()
    while _running and _bus:
        try:
            msg = _bus.recv(timeout=1.0)
            if msg:
                last_rx = time.monotonic()
                state.serial_connected = True
                parse_frame(msg)
            elif time.monotonic() - last_rx > _CAN_TIMEOUT_S:
                state.serial_connected = False
        except Exception as exc:
            if _running:
                log.warning("CAN rx error: %s", exc)


def _hb_loop() -> None:
    while _running:
        send(build_heartbeat())
        time.sleep(_HEARTBEAT_INTERVAL)


# ---------------------------------------------------------------------------
# Module-level singleton
# ---------------------------------------------------------------------------

can_bridge = type("CANBridge", (), {
    "start": staticmethod(start),
    "stop":  staticmethod(stop),
    "send":  staticmethod(send),
})()
