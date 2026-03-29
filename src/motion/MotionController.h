#pragma once

// =============================================================================
// MotionController — FastAccelStepper wrapper for two A4988-driven axes.
//
// Thread-safe: all public methods acquire _mutex; safe to call from any task.
//
// All motion settings are pushed by the Pi on every connect via the CLI
// protocol.  The ESP32 holds no persistent (NVS) state for motion.
// =============================================================================

#include <FastAccelStepper.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "common_types.h"
#include "config.h"

struct MotionSettings {
    // Pi pushes these on every connect; defaults below are safe fallbacks
    // used only if the Pi hasn't connected yet.
    float maxSpeedDegS    = 45.0f;
    float accelDegS2      = 120.0f;
    float fineSpeedScale  = 0.2f;
    float panMinDeg       = -180.0f;
    float panMaxDeg       =  180.0f;
    float tiltMinDeg      =  -45.0f;
    float tiltMaxDeg      =   90.0f;
    bool  softLimitsEnabled = false;
    bool  panDirInvert    = false;
    bool  tiltDirInvert   = false;
};

class MotionController {
public:
    MotionController();
    bool begin();

    // ── Motion commands ───────────────────────────────────────────────────────
    // Continuous velocity — deg/s at output shaft.  Call at ≤50 Hz.
    // Positive = forward/CW, negative = reverse/CCW (adjust DIR_INVERT for axis).
    void setVelocity(AxisId axis, float degS);

    // Move to absolute (or relative) position.
    void moveTo(AxisId axis, float deg, bool relative = false);

    void stop(AxisId axis);
    void stopAll();

    // Hard stop; sets e-stop latch.  Clear with clearEstop() + enableAll().
    void emergencyStop();
    void clearEstop();

    // ── Enable / disable ─────────────────────────────────────────────────────
    void enableAxis(AxisId axis, bool en);
    void enableAll(bool en);

    // ── Telemetry ────────────────────────────────────────────────────────────
    float getPositionDeg(AxisId axis) const;
    bool  isRunning(AxisId axis)      const;
    bool  isEstopped()                const { return _estop; }

    // ── Settings ─────────────────────────────────────────────────────────────
    MotionSettings getSettings()                    const;
    void           applySettings(const MotionSettings& s);
    void           resetSettings();

    // ── FreeRTOS task ────────────────────────────────────────────────────────
    static void motionTask(void* param);

private:
    FastAccelStepperEngine _engine;
    FastAccelStepper*      _stepper[2]      = {nullptr, nullptr};
    MotionSettings         _settings;
    float                  _cachedPosDeg[2] = {0.0f, 0.0f};
    int8_t                 _lastDir[2]      = {0, 0};  // -1, 0, +1 per axis
    bool                   _estop           = false;
    volatile uint32_t      _lastVelCmdMs    = 0;       // watchdog timestamp
    SemaphoreHandle_t      _mutex           = nullptr;

    float   stepsPerDeg(AxisId axis)               const;
    int32_t degToSteps(AxisId axis, float deg)     const;
    float   stepsToDeg(AxisId axis, int32_t steps) const;
    void    applyStepperSettings(AxisId axis);
    float   clampToLimits(AxisId axis, float deg)  const;
    bool    atMinLimit(AxisId axis, float deg)      const;
    bool    atMaxLimit(AxisId axis, float deg)      const;
};
