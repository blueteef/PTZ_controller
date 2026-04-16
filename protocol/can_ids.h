#pragma once
#include <stdint.h>

// =============================================================================
// PTZ Controller v2 — CAN Protocol Definitions
// 11-bit standard CAN IDs:  [2:0] source node  [6:3] message type  [10:7] reserved
// =============================================================================

// ---------------------------------------------------------------------------
// Node IDs
// ---------------------------------------------------------------------------
#define NODE_STATIONARY     0x0
#define NODE_PAN            0x1
#define NODE_TILT           0x2
// 0x3–0x7 reserved

// ---------------------------------------------------------------------------
// Message type IDs  (4 bits, 0x0–0xF)
// ---------------------------------------------------------------------------
#define MSG_HEARTBEAT       0x0     // periodic alive signal
#define MSG_ESTOP           0x1     // immediate stop all motion, any node can send
#define MSG_VEL_CMD         0x2     // velocity command to a specific axis
#define MSG_POS_CMD         0x3     // absolute position command
#define MSG_POS_REPORT      0x4     // position + velocity feedback
#define MSG_SENSOR_IMU      0x5     // roll/pitch/yaw (from pan node)
#define MSG_SENSOR_GPS      0x6     // lat/lon/fix (from pan node)
#define MSG_SENSOR_POWER    0x7     // voltage/current (INA226)
#define MSG_SENSOR_ENV      0x8     // temperature/pressure (BMP280)
#define MSG_LENS_CMD        0x9     // zoom/focus/iris command (to tilt node)
#define MSG_LENS_REPORT     0xA     // lens position feedback
#define MSG_FAULT           0xB     // fault/status flags
// 0xC–0xF reserved

// ---------------------------------------------------------------------------
// CAN ID construction
// ---------------------------------------------------------------------------
#define CAN_ID(node, msg_type)  (((msg_type) << 3) | (node))

// ---------------------------------------------------------------------------
// Axis IDs (packed into velocity/position frame)
// ---------------------------------------------------------------------------
#define AXIS_PAN            0x00
#define AXIS_TILT           0x01
#define AXIS_ZOOM           0x10
#define AXIS_FOCUS          0x11
#define AXIS_IRIS           0x12
