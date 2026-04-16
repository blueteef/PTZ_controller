"""
CAN bus bridge — replaces serial_bridge for v2 hardware.
Uses python-can against socketcan (can0 via MCP2515 or equivalent).
"""
import logging
import threading
import can
from app.can.protocol import (
    build_vel_cmd, build_pos_cmd, build_estop,
    parse_frame, NODE_PI,
)
from app.config import CAN_CHANNEL, CAN_BITRATE

log = logging.getLogger(__name__)

_bus: can.BusABC | None = None
_rx_thread: threading.Thread | None = None
_running = False


def start():
    global _bus, _rx_thread, _running
    try:
        _bus = can.interface.Bus(channel=CAN_CHANNEL, bustype="socketcan", bitrate=CAN_BITRATE)
        _running = True
        _rx_thread = threading.Thread(target=_rx_loop, daemon=True, name="can-rx")
        _rx_thread.start()
        log.info("CAN bridge started on %s @ %d bps", CAN_CHANNEL, CAN_BITRATE)
    except Exception as e:
        log.error("CAN bridge failed to start: %s", e)


def stop():
    global _running, _bus
    _running = False
    if _bus:
        _bus.shutdown()
        _bus = None
    log.info("CAN bridge stopped")


def send_vel(axis: int, vel_cdeg_s: int):
    _send(build_vel_cmd(axis, vel_cdeg_s))


def send_pos(axis: int, pos_cdeg: int):
    _send(build_pos_cmd(axis, pos_cdeg))


def send_estop():
    _send(build_estop())


def _send(msg: can.Message):
    if _bus is None:
        return
    try:
        _bus.send(msg)
    except can.CanError as e:
        log.warning("CAN send error: %s", e)


def _rx_loop():
    while _running and _bus:
        try:
            msg = _bus.recv(timeout=1.0)
            if msg:
                parse_frame(msg)
        except Exception as e:
            if _running:
                log.warning("CAN rx error: %s", e)
