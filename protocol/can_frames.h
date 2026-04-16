#pragma once
#include <stdint.h>
#include "can_ids.h"

// =============================================================================
// PTZ Controller v2 — CAN Frame Payload Definitions
// All structs are packed; max 8 bytes per frame (CAN 2.0)
// =============================================================================

#pragma pack(push, 1)

// MSG_HEARTBEAT — 1 byte
typedef struct {
    uint8_t node_id;
} FrameHeartbeat;

// MSG_VEL_CMD — 3 bytes
// velocity in deg/s * 100 (signed), axis ID
typedef struct {
    uint8_t  axis;
    int16_t  vel_cdeg_s;    // centidegrees/sec
} FrameVelCmd;

// MSG_POS_CMD — 5 bytes
typedef struct {
    uint8_t  axis;
    int32_t  pos_cdeg;      // centidegrees from home
} FramePosCmd;

// MSG_POS_REPORT — 8 bytes
typedef struct {
    uint8_t  axis;
    int32_t  pos_cdeg;
    int16_t  vel_cdeg_s;
    uint8_t  flags;         // bit0=moving, bit1=fault, bit2=homed
} FramePosReport;

// MSG_SENSOR_IMU — 6 bytes (roll/pitch/yaw in centidegrees)
typedef struct {
    int16_t  roll_cdeg;
    int16_t  pitch_cdeg;
    int16_t  yaw_cdeg;
} FrameImu;

// MSG_SENSOR_GPS — 8 bytes
typedef struct {
    int32_t  lat_1e6;       // degrees * 1e6
    int32_t  lon_1e6;
} FrameGps;

// MSG_SENSOR_POWER — 4 bytes
typedef struct {
    uint16_t voltage_mv;
    int16_t  current_ma;
} FramePower;

// MSG_SENSOR_ENV — 4 bytes
typedef struct {
    int16_t  temp_cdeg;     // centidegrees C
    uint16_t pressure_pa;   // Pascal (truncated, add 90000 offset on decode)
} FrameEnv;

// MSG_LENS_CMD — 3 bytes
typedef struct {
    uint8_t  axis;          // AXIS_ZOOM / AXIS_FOCUS / AXIS_IRIS
    int16_t  value;         // absolute position or relative delta
} FrameLensCmd;

// MSG_FAULT — 2 bytes
typedef struct {
    uint8_t  node_id;
    uint8_t  fault_code;
} FrameFault;

#pragma pack(pop)
