"""
CAN frame builders and parsers — mirrors protocol/can_ids.h and can_frames.h.
"""
import struct
import logging
import can

log = logging.getLogger(__name__)

# ---------------------------------------------------------------------------
# Node IDs
# ---------------------------------------------------------------------------
NODE_STATIONARY = 0x0
NODE_PAN        = 0x1
NODE_TILT       = 0x2
NODE_PI         = 0x3   # Pi doesn't have a CAN node ID but uses this for framing

# ---------------------------------------------------------------------------
# Message types
# ---------------------------------------------------------------------------
MSG_HEARTBEAT   = 0x0
MSG_ESTOP       = 0x1
MSG_VEL_CMD     = 0x2
MSG_POS_CMD     = 0x3
MSG_POS_REPORT  = 0x4
MSG_SENSOR_IMU  = 0x5
MSG_SENSOR_GPS  = 0x6
MSG_SENSOR_POWER= 0x7
MSG_SENSOR_ENV  = 0x8
MSG_LENS_CMD    = 0x9
MSG_LENS_REPORT = 0xA
MSG_FAULT       = 0xB

# ---------------------------------------------------------------------------
# Axis IDs
# ---------------------------------------------------------------------------
AXIS_PAN   = 0x00
AXIS_TILT  = 0x01
AXIS_ZOOM  = 0x10
AXIS_FOCUS = 0x11
AXIS_IRIS  = 0x12

# ---------------------------------------------------------------------------
# CAN ID helpers
# ---------------------------------------------------------------------------
def can_id(node: int, msg_type: int) -> int:
    return (msg_type << 3) | node

def decode_id(arb_id: int) -> tuple[int, int]:
    """Returns (node_id, msg_type)."""
    return arb_id & 0x7, (arb_id >> 3) & 0xF

# ---------------------------------------------------------------------------
# Frame builders
# ---------------------------------------------------------------------------
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

def build_estop() -> can.Message:
    return can.Message(
        arbitration_id=can_id(NODE_PI, MSG_ESTOP),
        data=b"",
        is_extended_id=False,
    )

def build_lens_cmd(axis: int, value: int) -> can.Message:
    return can.Message(
        arbitration_id=can_id(NODE_PI, MSG_LENS_CMD),
        data=struct.pack("<Bh", axis, value),
        is_extended_id=False,
    )

# ---------------------------------------------------------------------------
# Frame parser — called from rx loop; updates shared app state
# ---------------------------------------------------------------------------
def parse_frame(msg: can.Message):
    node, msg_type = decode_id(msg.arbitration_id)
    d = msg.data

    try:
        if msg_type == MSG_POS_REPORT:
            axis, pos_cdeg, vel_cdeg_s, flags = struct.unpack("<BiHB", d)
            _handle_pos_report(axis, pos_cdeg, vel_cdeg_s, flags)

        elif msg_type == MSG_SENSOR_IMU:
            roll, pitch, yaw = struct.unpack("<hhh", d)
            _handle_imu(roll / 100.0, pitch / 100.0, yaw / 100.0)

        elif msg_type == MSG_SENSOR_GPS:
            lat, lon = struct.unpack("<ii", d)
            _handle_gps(lat / 1e6, lon / 1e6)

        elif msg_type == MSG_SENSOR_POWER:
            voltage_mv, current_ma = struct.unpack("<Hh", d)
            _handle_power(voltage_mv / 1000.0, current_ma / 1000.0)

        elif msg_type == MSG_FAULT:
            node_id, fault_code = struct.unpack("<BB", d)
            log.warning("Fault from node %d: code 0x%02X", node_id, fault_code)

    except struct.error as e:
        log.debug("CAN parse error (node=%d type=0x%X): %s", node, msg_type, e)


# ---------------------------------------------------------------------------
# Handlers — import state here to avoid circular imports
# ---------------------------------------------------------------------------
def _handle_pos_report(axis, pos_cdeg, vel_cdeg_s, flags):
    from app.state import app_state
    if axis == AXIS_PAN:
        app_state.pan_deg = pos_cdeg / 100.0
    elif axis == AXIS_TILT:
        app_state.tilt_deg = pos_cdeg / 100.0

def _handle_imu(roll, pitch, yaw):
    pass  # TODO: feed into stabilization if needed

def _handle_gps(lat, lon):
    pass  # TODO: store in app_state if needed

def _handle_power(voltage, current):
    pass  # TODO: store in app_state if needed
