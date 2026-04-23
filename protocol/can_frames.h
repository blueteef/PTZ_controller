#pragma once
#include <stdint.h>
#include "can_ids.h"

// =============================================================================
// PTZ Controller v2 — CAN Frame Payload Structs
//
// All structs are packed little-endian; max 8 bytes per frame (CAN 2.0).
// Integer angles in centidegrees (1/100 °) for 0.01° resolution without
// floating point on the ESP32.
// Must match pi/app/can/protocol.py struct formats exactly.
// =============================================================================

#pragma pack(push, 1)

// MSG_HEARTBEAT — 0 bytes (empty frame; node_id from CAN ID)

// MSG_ESTOP — 0 bytes (empty frame)

// MSG_STOP — 1 byte
typedef struct {
    uint8_t  axis;              // AXIS_PAN / AXIS_TILT / AXIS_ALL
} FrameStop;

// MSG_VEL_CMD — 3 bytes
typedef struct {
    uint8_t  axis;              // AXIS_PAN / AXIS_TILT
    int16_t  vel_cdeg_s;        // signed centidegrees/s  (-32768 to +32767)
} FrameVelCmd;

// MSG_POS_CMD — 5 bytes
typedef struct {
    uint8_t  axis;
    int32_t  pos_cdeg;          // absolute centidegrees from home
} FramePosCmd;

// MSG_HOME_CMD — 1 byte
typedef struct {
    uint8_t  axis;              // AXIS_PAN / AXIS_TILT / AXIS_ALL
} FrameHomeCmd;

// MSG_SETTINGS — 4 bytes
typedef struct {
    uint16_t max_speed_cdeg_s;  // max velocity cap
    uint16_t accel_cdeg_s2;     // acceleration
} FrameSettings;

// MSG_POS_REPORT — 8 bytes
typedef struct {
    uint8_t  axis;
    int32_t  pos_cdeg;          // current position (centidegrees from home)
    int16_t  vel_cdeg_s;        // current velocity
    uint8_t  flags;             // POS_FLAG_* bitmask
} FramePosReport;

// MSG_SENSOR_IMU — 6 bytes
typedef struct {
    int16_t  roll_cdeg;
    int16_t  pitch_cdeg;
    int16_t  yaw_cdeg;
} FrameImu;

// MSG_SENSOR_MAG — 3 bytes
typedef struct {
    int16_t  hdg_cdeg;          // 0–35999 centidegrees (0–359.99°)
    uint8_t  ok;                // 1 = valid
} FrameMag;

// MSG_SENSOR_GPS — 8 bytes
typedef struct {
    int32_t  lat_1e6;           // degrees * 1,000,000
    int32_t  lon_1e6;
} FrameGps;

// MSG_SENSOR_GPS2 — 6 bytes
typedef struct {
    uint8_t  fix;               // 0=no fix, 1=fix
    uint8_t  sats;
    int16_t  hdg_cdeg;          // course over ground, centidegrees
    int16_t  spd_cm_s;          // speed cm/s
} FrameGps2;

// MSG_SENSOR_POWER — 4 bytes
typedef struct {
    uint16_t voltage_mv;        // millivolts
    int16_t  current_ma;        // milliamps (signed — negative = discharging)
} FramePower;

// MSG_SENSOR_ENV — 4 bytes
typedef struct {
    int16_t  temp_cdeg;         // centidegrees C (e.g. 2350 = 23.50°C)
    uint16_t press_hPa;         // hectopascals
} FrameEnv;

// MSG_RELAY_CMD — 2 bytes
typedef struct {
    uint8_t relay_id;   // 0 = main power relay
    uint8_t state;      // 1 = on, 0 = off
} FrameRelayCmd;

// MSG_FAULT — 2 bytes
typedef struct {
    uint8_t  node_id;
    uint8_t  fault_code;        // FAULT_* constants
} FrameFault;

#pragma pack(pop)
