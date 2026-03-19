#pragma once

// =============================================================================
// MotionController — manages both pan and tilt axes
//
// Drive mode is selected at compile time via a build flag:
//
//   -DPTZ_DRIVE_UART    (default)
//     All motion via MKS UART commands.  Driver must be in CR_UART work mode.
//     3 wires per driver: Tx / Rx / GND.
//     Position is tracked by polling the onboard encoder.
//
//   -DPTZ_DRIVE_STEPPER
//     Motion via Step/Dir/En pulses (FastAccelStepper).
//     UART used for config, encoder readback, and homing only.
//     Driver must be in CR_vFOC work mode.
//     6 wires per driver: Step / Dir / En / Tx / Rx / GND.
//
// The public API is identical in both modes.
// Thread safety: all public methods acquire _mutex; safe to call from any task.
// =============================================================================

// Ensure exactly one drive mode is defined.
#if !defined(PTZ_DRIVE_UART) && !defined(PTZ_DRIVE_STEPPER)
#  define PTZ_DRIVE_UART
#endif

#ifdef PTZ_DRIVE_STEPPER
#  include <FastAccelStepper.h>
#endif

#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "drivers/MKSServo42C.h"
#include "common_types.h"
#include "config.h"

struct MotionSettings {
    float maxSpeedDegS    = DEFAULT_MAX_SPEED_DEG_S;
    float accelDegS2      = DEFAULT_ACCEL_DEG_S2;
    float fineSpeedScale  = DEFAULT_FINE_SPEED_SCALE;
    float panMinDeg       = PAN_SOFT_LIMIT_MIN;
    float panMaxDeg       = PAN_SOFT_LIMIT_MAX;
    float tiltMinDeg      = TILT_SOFT_LIMIT_MIN;
    float tiltMaxDeg      = TILT_SOFT_LIMIT_MAX;
    bool  softLimitsEnabled = SOFT_LIMITS_ENABLED;
};

class MotionController {
public:
    MotionController();
    bool begin();

    // ── Motion commands ──────────────────────────────────────────────────────
    void moveTo(AxisId axis, float deg, bool relative = false);
    void setVelocity(AxisId axis, float degS);
    void stop(AxisId axis);
    void stopAll();
    void emergencyStop();

    // ── Homing ───────────────────────────────────────────────────────────────
    bool startHoming(AxisId axis);
    bool isHoming(AxisId axis) const;
    bool calibrateEncoder(AxisId axis);

    // ── Enable / disable ─────────────────────────────────────────────────────
    void enableAxis(AxisId axis, bool en);
    void enableAll(bool en);

    // ── Telemetry ────────────────────────────────────────────────────────────
    float getPositionDeg(AxisId axis) const;
    bool  isRunning(AxisId axis)      const;
    bool  readDriverStatus(AxisId axis, uint8_t& statusByte);
    bool  readEncoderAngle(AxisId axis, float& angleDeg);
    bool  pingDriver(AxisId axis);

    // Diagnostic: send arbitrary raw UART frame via the real driver object
    // (mutex-protected) and dump the raw response.  Used by 'diag' CLI command.
    void  diagAxis(AxisId axis, uint8_t func, uint32_t timeoutMs = 300);

    // ── Settings ─────────────────────────────────────────────────────────────
    MotionSettings getSettings()                    const;
    void           applySettings(const MotionSettings& s);
    void           loadSettings();
    void           saveSettings();
    void           resetSettings();

    // ── FreeRTOS task ────────────────────────────────────────────────────────
    static void motionTask(void* param);

private:
    MKSServo42C*      _driver[2]  = {nullptr, nullptr};
    MotionSettings    _settings;
    bool              _homing[2]           = {false, false};
    uint32_t          _homingStartMs[2]   = {0, 0};
    bool              _homingSeenBit[2]   = {false, false}; // HOMING status bit ever seen
    bool              _homingSoftware[2]  = {false, false}; // true = software homing mode
    float             _homingPrevAngle[2] = {-1.0f, -1.0f};
    bool              _moving[2]          = {false, false};
    bool              _estop      = false;
    float             _cachedPosDeg[2] = {0.0f, 0.0f};
    Preferences       _prefs;
    SemaphoreHandle_t _mutex      = nullptr;

#ifdef PTZ_DRIVE_STEPPER
    FastAccelStepperEngine _engine;
    FastAccelStepper*      _stepper[2] = {nullptr, nullptr};
    void  applyStepperSettings(AxisId axis);
    float stepsPerDeg(AxisId axis)               const;
    int32_t degToSteps(AxisId axis, float deg)   const;
    float   stepsToDeg(AxisId axis, int32_t s)   const;
#endif

#ifdef PTZ_DRIVE_UART
    // Convert output-shaft deg/s to MKS speed byte (bits6-0, 0–127).
    uint8_t degSToSpeedByte(AxisId axis, float degS) const;
    // Compute microstep count for a given output-shaft delta in degrees.
    uint32_t degToPulses(AxisId axis, float deg) const;
#endif

    float clampToLimits(AxisId axis, float deg) const;
    bool  atMinLimit(AxisId axis, float deg)    const;
    bool  atMaxLimit(AxisId axis, float deg)    const;
    void  pollHoming(AxisId axis);
    void  updateCachedPosition(AxisId axis);
};
