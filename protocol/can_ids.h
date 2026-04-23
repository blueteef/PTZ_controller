#pragma once
#include <stdint.h>

// =============================================================================
// PTZ Controller v2 — CAN Protocol Definitions
//
// 11-bit standard CAN ID:  bits[2:0] = source node,  bits[10:3] = message type
//
// CAN_ID(node, msg_type) = (msg_type << 3) | (node & 0x7)
//
// Must match pi/app/can/protocol.py exactly.
// =============================================================================

// ---------------------------------------------------------------------------
// Node IDs  (3-bit field, 0–7)
// ---------------------------------------------------------------------------
#define NODE_STATIONARY     0x0   // stationary base — vehicle IMU, GPS, compass, pan motor
#define NODE_PAN            0x1   // pan axis node
#define NODE_TILT           0x2   // tilt axis node
#define NODE_PI             0x7   // Raspberry Pi brain

// ---------------------------------------------------------------------------
// Message type IDs  (8-bit field, shifted << 3 in CAN ID)
// Commands (Pi → nodes)
// ---------------------------------------------------------------------------
#define MSG_HEARTBEAT       0x00  // periodic alive signal — 0 bytes
#define MSG_ESTOP           0x01  // emergency stop, broadcast — 0 bytes
#define MSG_STOP            0x02  // controlled stop — u8 axis (0xFF = all)
#define MSG_VEL_CMD         0x03  // velocity command — u8 axis, i16 vel_cdeg_s
#define MSG_POS_CMD         0x04  // absolute position — u8 axis, i32 pos_cdeg
#define MSG_HOME_CMD        0x05  // home axis — u8 axis (0xFF = all)
#define MSG_SETTINGS        0x06  // motion settings — u16 max_speed_cdeg_s, u16 accel_cdeg_s2

// Telemetry (nodes → Pi)
#define MSG_POS_REPORT      0x10  // position feedback — u8 axis, i32 pos_cdeg, i16 vel_cdeg_s, u8 flags
#define MSG_SENSOR_IMU      0x11  // vehicle IMU — i16 roll_cd, i16 pitch_cd, i16 yaw_cd
#define MSG_SENSOR_MAG      0x12  // compass — i16 hdg_cd, u8 ok
#define MSG_SENSOR_GPS      0x13  // GPS position — i32 lat_1e6, i32 lon_1e6
#define MSG_SENSOR_GPS2     0x14  // GPS extended — u8 fix, u8 sats, i16 hdg_cd, i16 spd_cm_s
#define MSG_SENSOR_POWER    0x15  // power monitor — u16 voltage_mv, i16 current_ma
#define MSG_SENSOR_ENV      0x16  // environment — i16 temp_cdC, u16 press_hPa
#define MSG_RELAY_CMD       0x07  // relay control — u8 relay_id, u8 state (1=on, 0=off)
#define MSG_FAULT           0x1F  // fault report — u8 node_id, u8 fault_code

// ---------------------------------------------------------------------------
// CAN ID construction / decoding
// ---------------------------------------------------------------------------
#define CAN_ID(node, msg_type)      (((msg_type) << 3) | ((node) & 0x7))
#define CAN_ID_NODE(arb_id)         ((arb_id) & 0x7)
#define CAN_ID_MSG_TYPE(arb_id)     (((arb_id) >> 3) & 0xFF)

// ---------------------------------------------------------------------------
// Axis IDs
// ---------------------------------------------------------------------------
#define AXIS_PAN            0x00
#define AXIS_TILT           0x01
#define AXIS_ALL            0xFF

// ---------------------------------------------------------------------------
// Flags in MSG_POS_REPORT
// ---------------------------------------------------------------------------
#define POS_FLAG_MOVING     (1 << 0)
#define POS_FLAG_FAULT      (1 << 1)
#define POS_FLAG_HOMED      (1 << 2)

// ---------------------------------------------------------------------------
// Fault codes in MSG_FAULT
// ---------------------------------------------------------------------------
#define FAULT_ENCODER_TIMEOUT   0x01
#define FAULT_STALL             0x02
#define FAULT_OVERCURRENT       0x03
#define FAULT_OVER_TEMP         0x04
#define FAULT_CAN_TIMEOUT       0x05
