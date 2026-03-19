#pragma once

// =============================================================================
// GamepadInput — Bluepad32 Xbox One S controller interface
//
// Button mapping
// ──────────────
//   Left  stick X/Y  →  Pan / Tilt  (normal speed)
//   Hold  RB          →  Fine-speed mode (speed scaled by fineSpeedScale)
//   D-pad Left/Right  →  Decrease / Increase pan speed multiplier (step 0.25)
//   D-pad Down/Up     →  Decrease / Increase tilt speed multiplier (step 0.25)
//   Left bumper (LB)  →  Latch laser on/off (toggle)
//   Left trigger      →  Fire laser (strobe ~25 Hz / 1500 rpm while held)
//   A                 →  Stop all motion
//   B                 →  Emergency stop (latching; power-cycle or CLI 'estop'
//                         to clear)
//   Y                 →  Home both axes
//   X                 →  Zero current position (set as new home)
//   Start / Menu      →  Toggle soft limits on/off
// =============================================================================

#include <Bluepad32.h>
#include "motion/MotionController.h"
#include "config.h"

class GamepadInput {
public:
    explicit GamepadInput(MotionController& motion);

    // Call once from setup() — registers Bluepad32 callbacks.
    bool begin();

    // FreeRTOS task entry point.  Calls BP32.update() at ~50 Hz and processes
    // the active controller.
    static void inputTask(void* param);

private:
    MotionController& _motion;
    ControllerPtr     _controller = nullptr;
    bool              _connected  = false;

    // Button edge-detection state (separate fields for face vs misc buttons)
    uint16_t _prevButtons     = 0;
    uint8_t  _prevMiscButtons = 0;
    uint8_t  _prevDpad        = 0;

    // Laser state
    bool     _laserLatched    = false;   // toggled by LB
    bool     _laserPulsePhase = false;   // toggles each frame when trigger held

    // Per-axis speed multipliers — adjusted live via d-pad.
    float    _panSpeedMult    = 4.25f;
    float    _tiltSpeedMult   = 10.0f;
    static constexpr float SPEED_MULT_STEP = 0.25f;
    static constexpr float SPEED_MULT_MIN  = 0.25f;
    static constexpr float SPEED_MULT_MAX  = 10.0f;

    void processController();
    float applyDeadzone(int32_t raw, int32_t maxVal, int32_t deadzone) const;

    static void onConnected(ControllerPtr ctl);
    static void onDisconnected(ControllerPtr ctl);

    // Singleton back-pointer for static callbacks.
    static GamepadInput* _instance;
};
