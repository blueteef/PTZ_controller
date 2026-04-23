"""
protocol.py — CAN frame builders and parsers.

ID scheme: 11-bit standard CAN ID = (msg_type << 3) | node_id
  - node_id : 3 bits (0–7)
  - msg_type: 8 bits (0–255), shifted left 3

All multi-byte values are little-endian.
Integer angles are in centidegrees (1/100 °) to preserve 0.01° resolution
without floating point on the ESP32.
"""

from __future__ import annotations

import logging
import struct

import can

from app.state import state

log = logging.getLogger(__name__)

# ---------------------------------------------------------------------------
# Node IDs  (3-bit field, 0–7)
# ---------------------------------------------------------------------------
NODE_STATIONARY = 0x0   # stationary base — vehicle IMU, GPS, compass, pan motor
NODE_PAN        = 0x1   # pan axis node
NODE_TILT       = 0x2   # tilt axis node
NODE_PI         = 0x7   # Pi (brain) — used as source ID on outbound frames

# ---------------------------------------------------------------------------
# Message types  (8-bit field, shifted << 3 in CAN ID)
# ---------------------------------------------------------------------------
MSG_HEARTBEAT    = 0x00
MSG_RELAY_CMD    = 0x07  # relay control — u8 relay_id, u8 state  # periodic alive ping
MSG_ESTOP        = 0x01  # emergency stop — zero payload, highest priority
MSG_STOP         = 0x02  # controlled stop  — u8 axis
MSG_VEL_CMD      = 0x03  # velocity command  — u8 axis, i16 vel_cdeg_s
MSG_POS_CMD      = 0x04  # position command  — u8 axis, i32 pos_cdeg
MSG_HOME_CMD     = 0x05  # home command      — u8 axis
MSG_SETTINGS     = 0x06  # motion settings   — u16 max_speed_cdeg_s, u16 accel_cdeg_s2
MSG_POS_REPORT   = 0x10  # position telemetry— u8 axis, i32 pos_cdeg, i16 vel_cdeg_s
MSG_SENSOR_IMU   = 0x11  # vehicle IMU       — i16 roll_cd, i16 pitch_cd, i16 yaw_cd
MSG_SENSOR_MAG   = 0x12  # compass           — i16 hdg_cd, u8 ok
MSG_SENSOR_GPS   = 0x13  # GPS position      — i32 lat_1e6, i32 lon_1e6
MSG_SENSOR_GPS2  = 0x14  # GPS extended      — u8 fix, u8 sats, i16 hdg_cd, i16 spd_cm_s
MSG_SENSOR_POWER = 0x15  # power             — u16 voltage_mv, i16 current_ma
MSG_SENSOR_ENV   = 0x16  # environment       — i16 temp_cdC, u16 press_hPa
MSG_FAULT        = 0x1F  # fault report      — u8 node_id, u8 fault_code

# ---------------------------------------------------------------------------
# Axis IDs
# ---------------------------------------------------------------------------
AXIS_PAN  = 0x00
AXIS_TILT = 0x01

# ---------------------------------------------------------------------------
# CAN ID packing / unpacking
# ---------------------------------------------------------------------------

def can_id(node: int, msg_type: int) -> int:
    return (msg_type << 3) | (node & 0x7)

def decode_id(arb_id: int) -> tuple[int, int]:
    """Returns (node_id, msg_type)."""
    return arb_id & 0x7, (arb_id >> 3) & 0xFF

# ---------------------------------------------------------------------------
# Frame builders  (Pi → nodes)
# ---------------------------------------------------------------------------

def build_estop() -> can.Message:
    return can.Message(
        arbitration_id=can_id(NODE_PI, MSG_ESTOP),
        data=b"",
        is_extended_id=False,
    )

def build_stop(axis: int = 0xFF) -> can.Message:
    return can.Message(
        arbitration_id=can_id(NODE_PI, MSG_STOP),
        data=struct.pack("<B", axis),
        is_extended_id=False,
    )

def build_vel_cmd(axis: int, vel_cdeg_s: int) -> can.Message:
    return can.Message(
        arbitration_id=can_id(NODE_PI, MSG_VEL_CMD),
        data=struct.pack("<Bh", axis, vel_cdeg_s),
        is_extended_id=False,
    )

def build_pos_cmd(axis: int, pos_cdeg: int) -> can.Message:
    return can.Message(
        arbitration_id=can_id(NODE_PI, MSG_POS_CMD),
        data=struct.pack("<Bi", axis, pos_cdeg),
        is_extended_id=False,
    )

def build_home_cmd(axis: int = 0xFF) -> can.Message:
    return can.Message(
        arbitration_id=can_id(NODE_PI, MSG_HOME_CMD),
        data=struct.pack("<B", axis),
        is_extended_id=False,
    )

def build_settings(max_speed_cdeg_s: int, accel_cdeg_s2: int) -> can.Message:
    return can.Message(
        arbitration_id=can_id(NODE_PI, MSG_SETTINGS),
        data=struct.pack("<HH", max_speed_cdeg_s, accel_cdeg_s2),
        is_extended_id=False,
    )

def build_relay_cmd(relay_id: int, state: bool) -> can.Message:
    return can.Message(
        arbitration_id=can_id(NODE_PI, MSG_RELAY_CMD),
        data=struct.pack("<BB", relay_id, 1 if state else 0),
        is_extended_id=False,
    )

def build_heartbeat() -> can.Message:
    return can.Message(
        arbitration_id=can_id(NODE_PI, MSG_HEARTBEAT),
        data=b"",
        is_extended_id=False,
    )

# ---------------------------------------------------------------------------
# Frame parser  (nodes → Pi)  — called from RX loop
# ---------------------------------------------------------------------------

def parse_frame(msg: can.Message) -> None:
    node, msg_type = decode_id(msg.arbitration_id)
    d = bytes(msg.data)

    try:
        if msg_type == MSG_POS_REPORT:
            axis, pos_cdeg, vel_cdeg_s = struct.unpack("<Bih", d[:7])
            _handle_pos(axis, pos_cdeg / 100.0, vel_cdeg_s / 100.0)

        elif msg_type == MSG_SENSOR_IMU:
            roll_cd, pitch_cd, yaw_cd = struct.unpack("<hhh", d[:6])
            state.sensor_imu = {
                "ok":    True,
                "roll":  roll_cd  / 100.0,
                "pitch": pitch_cd / 100.0,
                "yaw":   yaw_cd   / 100.0,
            }

        elif msg_type == MSG_SENSOR_MAG:
            hdg_cd, ok = struct.unpack("<hB", d[:3])
            state.sensor_mag = {
                "ok":  bool(ok),
                "hdg": hdg_cd / 100.0,
            }

        elif msg_type == MSG_SENSOR_GPS:
            lat, lon = struct.unpack("<ii", d[:8])
            state.sensor_gps = {
                **state.sensor_gps,
                "lat": lat / 1e6,
                "lon": lon / 1e6,
            }

        elif msg_type == MSG_SENSOR_GPS2:
            fix, sats, hdg_cd, spd = struct.unpack("<BBhh", d[:6])
            state.sensor_gps = {
                **state.sensor_gps,
                "fix":  bool(fix),
                "sats": sats,
                "hdg":  hdg_cd / 100.0,
                "spd":  spd / 100.0,
            }

        elif msg_type == MSG_SENSOR_POWER:
            voltage_mv, current_ma = struct.unpack("<Hh", d[:4])
            state.sensor_power = {
                "vin":  voltage_mv / 1000.0,
                "curr": current_ma / 1000.0,
                "pwr":  (voltage_mv / 1000.0) * (current_ma / 1000.0),
            }

        elif msg_type == MSG_SENSOR_ENV:
            temp_cdC, press_hPa = struct.unpack("<hH", d[:4])
            state.sensor_env = {
                "temp":  temp_cdC  / 100.0,
                "press": press_hPa,
            }

        elif msg_type == MSG_FAULT:
            fault_node, fault_code = struct.unpack("<BB", d[:2])
            log.warning("Fault from node 0x%X: code 0x%02X", fault_node, fault_code)
            state.serial_connected = False

        elif msg_type == MSG_HEARTBEAT:
            state.serial_connected = True

    except struct.error as e:
        log.debug("CAN parse error (node=0x%X type=0x%X len=%d): %s",
                  node, msg_type, len(d), e)


def _handle_pos(axis: int, pos_deg: float, vel_deg_s: float) -> None:
    if axis == AXIS_PAN:
        state.set_gimbal_position(pos_deg, state.gimbal_tilt_deg)
    elif axis == AXIS_TILT:
        state.set_gimbal_position(state.gimbal_pan_deg, pos_deg)
