// =============================================================================
// GamepadInput.cpp
// =============================================================================

#include "GamepadInput.h"
#include <Arduino.h>

// Task period — 20 ms gives 50 Hz update rate.
static constexpr uint32_t INPUT_TASK_PERIOD_MS = 20;

GamepadInput* GamepadInput::_instance = nullptr;

// -----------------------------------------------------------------------------
// Construction / begin
// -----------------------------------------------------------------------------

GamepadInput::GamepadInput(MotionController& motion)
    : _motion(motion) {
    _instance = this;
}

bool GamepadInput::begin() {
    pinMode(LASER_PIN, OUTPUT);
    digitalWrite(LASER_PIN, LOW);

    BP32.setup(&GamepadInput::onConnected, &GamepadInput::onDisconnected);
    // Forget previously paired devices so the controller always re-pairs fresh.
    // Comment this line out if you want the controller to auto-reconnect after
    // power-cycling without pressing the pairing button again.
    BP32.forgetBluetoothKeys();
    return true;
}

// -----------------------------------------------------------------------------
// Static Bluepad32 callbacks
// -----------------------------------------------------------------------------

void GamepadInput::onConnected(ControllerPtr ctl) {
    if (!_instance) return;
    if (!_instance->_connected) {
        _instance->_controller = ctl;
        _instance->_connected  = true;
        Serial.println("[INPUT] Controller connected");
    }
}

void GamepadInput::onDisconnected(ControllerPtr ctl) {
    if (!_instance) return;
    if (_instance->_controller == ctl) {
        _instance->_controller = nullptr;
        _instance->_connected  = false;
        // Stop motion and laser when the controller disconnects.
        _instance->_motion.stopAll();
        digitalWrite(LASER_PIN, LOW);
        Serial.println("[INPUT] Controller disconnected — motion stopped");
    }
}

// -----------------------------------------------------------------------------
// Deadzone / normalisation helper
//
// Returns a value in [-1, +1] with the deadzone region mapped to 0.
// -----------------------------------------------------------------------------

float GamepadInput::applyDeadzone(int32_t raw, int32_t maxVal,
                                   int32_t deadzone) const {
    if (raw > -deadzone && raw < deadzone) return 0.0f;
    float normalised = (float)raw / (float)maxVal;
    // Rescale so the output starts from 0 at the edge of the deadzone.
    float sign      = (normalised > 0.0f) ? 1.0f : -1.0f;
    float deadNorm  = (float)deadzone / (float)maxVal;
    float rescaled  = (fabsf(normalised) - deadNorm) / (1.0f - deadNorm);
    return sign * rescaled;
}

// -----------------------------------------------------------------------------
// Per-frame controller processing
// -----------------------------------------------------------------------------

void GamepadInput::processController() {
    if (!_controller || !_controller->isConnected()) return;

    MotionSettings settings = _motion.getSettings();

    // ── Speed modifier ──────────────────────────────────────────────────────
    // Hold right bumper (RB / BUTTON_SHOULDER_R) for fine mode.
    bool fineMode = (_controller->buttons() & BUTTON_SHOULDER_R) != 0;
    float speedScale = fineMode ? settings.fineSpeedScale : 1.0f;
    float maxSpeed   = settings.maxSpeedDegS * speedScale;

    // ── Axes ────────────────────────────────────────────────────────────────
    // Left stick X → Pan,  Left stick Y → Tilt
    // axisX/axisY range: -512 to +511 (Bluepad32 default)
    float panNorm  = applyDeadzone(_controller->axisX(),
                                   GAMEPAD_STICK_MAX, GAMEPAD_STICK_DEADZONE);
    float tiltNorm = applyDeadzone(_controller->axisY(),
                                   GAMEPAD_STICK_MAX, GAMEPAD_STICK_DEADZONE);

    // Invert pan direction and Y axis; apply per-axis speed multipliers.
    _motion.setVelocity(AxisId::PAN,  -panNorm  *  maxSpeed * _panSpeedMult);
    _motion.setVelocity(AxisId::TILT,  tiltNorm * -maxSpeed * _tiltSpeedMult);

    // ── Button edge detection ────────────────────────────────────────────────
    uint16_t cur      = _controller->buttons();
    uint16_t rose     = cur & ~_prevButtons;
    _prevButtons      = cur;

    uint8_t miscCur   = _controller->miscButtons();
    uint8_t miscRose  = miscCur & ~_prevMiscButtons;
    _prevMiscButtons  = miscCur;

    // ── D-pad speed control ──────────────────────────────────────────────────
    // D-pad Right/Left  → pan speed multiplier up/down
    // D-pad Up/Down     → tilt speed multiplier up/down
    uint8_t dpad     = _controller->dpad();
    uint8_t dpadRose = dpad & ~_prevDpad;
    _prevDpad        = dpad;

    if (dpadRose & DPAD_RIGHT) {
        _panSpeedMult = fminf(_panSpeedMult + SPEED_MULT_STEP, SPEED_MULT_MAX);
        Serial.printf("[INPUT] Pan speed x%.2f\r\n", _panSpeedMult);
    }
    if (dpadRose & DPAD_LEFT) {
        _panSpeedMult = fmaxf(_panSpeedMult - SPEED_MULT_STEP, SPEED_MULT_MIN);
        Serial.printf("[INPUT] Pan speed x%.2f\r\n", _panSpeedMult);
    }
    if (dpadRose & DPAD_UP) {
        _tiltSpeedMult = fminf(_tiltSpeedMult + SPEED_MULT_STEP, SPEED_MULT_MAX);
        Serial.printf("[INPUT] Tilt speed x%.2f\r\n", _tiltSpeedMult);
    }
    if (dpadRose & DPAD_DOWN) {
        _tiltSpeedMult = fmaxf(_tiltSpeedMult - SPEED_MULT_STEP, SPEED_MULT_MIN);
        Serial.printf("[INPUT] Tilt speed x%.2f\r\n", _tiltSpeedMult);
    }

    // ── Laser control ───────────────────────────────────────────────────────
    // LB rising edge: latch toggle (steady on/off)
    if (rose & BUTTON_SHOULDER_L) {
        _laserLatched = !_laserLatched;
        Serial.printf("[INPUT] Laser latch %s\r\n", _laserLatched ? "ON" : "OFF");
    }

    // Left trigger held past half-travel: strobe ~25 Hz (toggle every 20 ms frame)
    bool triggerHeld = _controller->brake() > (GAMEPAD_TRIGGER_MAX / 2);
    if (triggerHeld) {
        _laserPulsePhase = !_laserPulsePhase;
    } else {
        _laserPulsePhase = false;
    }

    digitalWrite(LASER_PIN,
                 (_laserLatched || (triggerHeld && _laserPulsePhase)) ? HIGH : LOW);

    // A → Stop all motion
    if (rose & BUTTON_A) {
        _motion.stopAll();
        Serial.println("[INPUT] Stop");
    }

    // B → Emergency stop
    if (rose & BUTTON_B) {
        _motion.emergencyStop();
        Serial.println("[INPUT] EMERGENCY STOP");
    }

    // Y → Home both axes
    if (rose & BUTTON_Y) {
        Serial.println("[INPUT] Homing PAN...");
        _motion.startHoming(AxisId::PAN);
        Serial.println("[INPUT] Homing TILT...");
        _motion.startHoming(AxisId::TILT);
    }

    // X → Zero current position
    if (rose & BUTTON_X) {
        // Not directly supported by FastAccelStepper via MotionController API,
        // but we can do it by issuing a moveTo(0) — the controller treats the
        // current encoder position as the reference.  For a true "set-here"
        // zero, the user should use the CLI `zero` command which has a full
        // NVS-backed offset implementation.
        Serial.println("[INPUT] Zero not available via gamepad — use CLI 'zero'");
    }

    // Menu button → Toggle soft limits
    // Xbox One S "Menu" (three-line / hamburger) button is in miscButtons() bit 0.
    if (miscRose & 0x01) {
        MotionSettings s    = _motion.getSettings();
        s.softLimitsEnabled = !s.softLimitsEnabled;
        _motion.applySettings(s);
        Serial.printf("[INPUT] Soft limits %s\r\n",
                      s.softLimitsEnabled ? "ON" : "OFF");
    }
}

// -----------------------------------------------------------------------------
// FreeRTOS task
// -----------------------------------------------------------------------------

void GamepadInput::inputTask(void* param) {
    GamepadInput* gi = static_cast<GamepadInput*>(param);

    for (;;) {
        BP32.update();

        if (gi->_connected) {
            gi->processController();
        }

        vTaskDelay(pdMS_TO_TICKS(INPUT_TASK_PERIOD_MS));
    }
}
