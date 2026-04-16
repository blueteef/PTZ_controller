#pragma once

#include <stdint.h>

// =============================================================================
// PTZ Controller — Common types
// =============================================================================

enum class AxisId { PAN = 0, TILT = 1, COUNT = 2 };

enum class MotionMode { IDLE, VELOCITY, POSITION };

// Who currently has control of the gimbal.
enum class ControlSource { NONE, JOG, CLI, TRACKING };
