#pragma once

#include <stdint.h>

// =============================================================================
// PTZ Controller — Common Types, Enums, and Structs
// =============================================================================

enum class AxisId { PAN = 0, TILT = 1, COUNT = 2 };

enum class MotionMode { IDLE, VELOCITY, POSITION, HOMING };

enum class DriverStatus { UNKNOWN, OK, ERROR, STALL };

enum class ControlSource { NONE, GAMEPAD, CLI, AUTONOMOUS };

// State for a single axis
struct AxisState {
    float        positionDeg;
    float        velocityDegS;
    MotionMode   mode;
    DriverStatus driverStatus;
    bool         enabled;
    bool         homed;
};

// Full system state snapshot
struct SystemState {
    AxisState     axes[2];          // index 0 = PAN, 1 = TILT
    ControlSource activeSource;
    bool          emergencyStop;
    uint32_t      uptimeMs;
};

// Command issued to the motion controller
struct MoveCommand {
    AxisId axis;
    float  targetDeg;      // absolute position (position mode)
    float  velocityDegS;   // continuous velocity (velocity mode)
    bool   relative;       // if true, targetDeg is a delta
    bool   isVelocity;     // if true, use velocityDegS field
};
