"""
bridge.py — CAN bus bridge (replaces serial_bridge for v2 hardware).

Uses python-can against SocketCAN (can0 via MCP2515).
Runs one RX thread and sends heartbeats on a fixed interval.
Auto-reconnects if the interface goes into an error/bus-off state.
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

_HEARTBEAT_INTERVAL  = 1.0    # seconds between Pi heartbeat frames
_CAN_TIMEOUT_S       = 3.0    # seconds without a frame before marking bus offline
_MAX_CONSECUTIVE_ERR = 3      # errors before attempting reconnect
_RECONNECT_DELAY_S   = 2.0    # seconds to wait before reconnect attempt

_bus:       can.BusABC | None = None
_rx_thread: threading.Thread | None = None
_hb_thread: threading.Thread | None = None
_running    = False
_lock       = threading.Lock()


def start() -> None:
    global _bus, _rx_thread, _hb_thread, _running
    try:
        _bus = _open_bus()
        if _bus is None:
            log.error("CAN bridge failed to open %s", config.CAN_CHANNEL)
            return
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
    with _lock:
        if _bus:
            try:
                _bus.shutdown()
            except Exception:
                pass
            _bus = None
    log.info("CAN bridge stopped")


def send(msg: can.Message) -> None:
    with _lock:
        if _bus is None:
            return
        try:
            _bus.send(msg)
        except Exception as exc:
            log.warning("CAN send error: %s", exc)


def send_estop() -> None:
    send(build_estop())


# ---------------------------------------------------------------------------
# Internal helpers
# ---------------------------------------------------------------------------

def _open_bus() -> can.BusABC | None:
    try:
        return can.interface.Bus(
            channel=config.CAN_CHANNEL,
            bustype="socketcan",
            bitrate=config.CAN_BITRATE,
        )
    except Exception as exc:
        log.warning("CAN open failed: %s", exc)
        return None


def _reconnect() -> None:
    global _bus
    log.warning("CAN bridge reconnecting...")
    state.serial_connected = False
    with _lock:
        if _bus:
            try:
                _bus.shutdown()
            except Exception:
                pass
            _bus = None
    time.sleep(_RECONNECT_DELAY_S)
    new_bus = _open_bus()
    with _lock:
        _bus = new_bus
    if _bus:
        log.info("CAN bridge reconnected on %s", config.CAN_CHANNEL)
    else:
        log.error("CAN bridge reconnect failed — will retry")


def _rx_loop() -> None:
    last_rx           = time.monotonic()
    consecutive_errors = 0

    while _running:
        if _bus is None:
            time.sleep(0.5)
            continue
        try:
            msg = _bus.recv(timeout=1.0)
            if msg:
                last_rx            = time.monotonic()
                consecutive_errors = 0
                state.serial_connected = True
                parse_frame(msg)
            elif time.monotonic() - last_rx > _CAN_TIMEOUT_S:
                state.serial_connected = False
        except Exception as exc:
            consecutive_errors += 1
            if _running:
                log.warning("CAN rx error (%d): %s", consecutive_errors, exc)
            if consecutive_errors >= _MAX_CONSECUTIVE_ERR:
                _reconnect()
                consecutive_errors = 0
                last_rx = time.monotonic()


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
