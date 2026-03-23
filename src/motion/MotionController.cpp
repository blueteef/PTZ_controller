// =============================================================================
// MotionController.cpp
// =============================================================================

#include "MotionController.h"
#include <Arduino.h>
#include <math.h>

static constexpr uint32_t MOTION_TASK_PERIOD_MS = 20;
static constexpr uint32_t MIN_SPEED_STEPS       = 5;   // stepper mode only

// =============================================================================
// Construction / begin
// =============================================================================

MotionController::MotionController() {}

bool MotionController::begin() {
    _mutex = xSemaphoreCreateMutex();
    if (!_mutex) return false;

    // MKS UART drivers — skipped for axes using A4988 (no UART on A4988)
#if !defined(PAN_DRIVER_A4988)
    _driver[0] = new MKSServo42C(Serial2, MKS_PAN_ADDR);
    _driver[0]->begin(MKS_BAUD_RATE);
#endif
#if !defined(TILT_DRIVER_A4988)
    _driver[1] = new MKSServo42C(Serial1, MKS_TILT_ADDR);
    _driver[1]->begin(MKS_BAUD_RATE);
#endif

#ifdef PTZ_DRIVE_STEPPER
    _engine.init();

    _stepper[0] = _engine.stepperConnectToPin(PAN_STEP_PIN);
    if (!_stepper[0]) return false;
    _stepper[0]->setDirectionPin(PAN_DIR_PIN,  PAN_DIR_INVERT);
    _stepper[0]->setEnablePin(PAN_EN_PIN, true);
    _stepper[0]->setAutoEnable(false);

    _stepper[1] = _engine.stepperConnectToPin(TILT_STEP_PIN);
    if (!_stepper[1]) return false;
    _stepper[1]->setDirectionPin(TILT_DIR_PIN, TILT_DIR_INVERT);
    _stepper[1]->setEnablePin(TILT_EN_PIN, true);
    _stepper[1]->setAutoEnable(false);
#endif

    loadSettings();

#ifdef PTZ_DRIVE_STEPPER
    applyStepperSettings(AxisId::PAN);
    applyStepperSettings(AxisId::TILT);
#endif

    enableAll(true);
    return true;
}

// =============================================================================
// Soft-limit helpers  (shared between both modes)
// =============================================================================

float MotionController::clampToLimits(AxisId axis, float deg) const {
    if (!_settings.softLimitsEnabled) return deg;
    float lo = (axis == AxisId::PAN) ? _settings.panMinDeg  : _settings.tiltMinDeg;
    float hi = (axis == AxisId::PAN) ? _settings.panMaxDeg  : _settings.tiltMaxDeg;
    if (deg < lo) return lo;
    if (deg > hi) return hi;
    return deg;
}

bool MotionController::atMinLimit(AxisId axis, float deg) const {
    if (!_settings.softLimitsEnabled) return false;
    float lo = (axis == AxisId::PAN) ? _settings.panMinDeg : _settings.tiltMinDeg;
    return deg <= lo;
}

bool MotionController::atMaxLimit(AxisId axis, float deg) const {
    if (!_settings.softLimitsEnabled) return false;
    float hi = (axis == AxisId::PAN) ? _settings.panMaxDeg : _settings.tiltMaxDeg;
    return deg >= hi;
}

// =============================================================================
// STEPPER mode helpers
// =============================================================================

#ifdef PTZ_DRIVE_STEPPER

float MotionController::stepsPerDeg(AxisId axis) const {
    if (axis == AxisId::PAN)
        return (float)(MOTOR_STEPS_PER_REV * DEFAULT_MICROSTEPS) *
               (float)PAN_GEAR_RATIO_NUM / (360.0f * (float)PAN_GEAR_RATIO_DEN);
    return (float)(MOTOR_STEPS_PER_REV * DEFAULT_MICROSTEPS) *
           (float)TILT_GEAR_RATIO_NUM / (360.0f * (float)TILT_GEAR_RATIO_DEN);
}

int32_t MotionController::degToSteps(AxisId axis, float deg) const {
    return (int32_t)roundf(deg * stepsPerDeg(axis));
}

float MotionController::stepsToDeg(AxisId axis, int32_t steps) const {
    return (float)steps / stepsPerDeg(axis);
}

void MotionController::applyStepperSettings(AxisId axis) {
    int idx = (int)axis;
    if (!_stepper[idx]) return;
    uint32_t speedHz  = (uint32_t)(_settings.maxSpeedDegS * stepsPerDeg(axis));
    uint32_t accelHz2 = (uint32_t)(_settings.accelDegS2   * stepsPerDeg(axis));
    _stepper[idx]->setSpeedInHz(speedHz  ? speedHz  : 1);
    _stepper[idx]->setAcceleration(accelHz2 ? accelHz2 : 1);
}

#endif // PTZ_DRIVE_STEPPER

// =============================================================================
// UART mode helpers
// =============================================================================

#ifdef PTZ_DRIVE_UART

float gearRatioFor(AxisId axis) {
    if (axis == AxisId::PAN)
        return (float)PAN_GEAR_RATIO_NUM  / (float)PAN_GEAR_RATIO_DEN;
    return (float)TILT_GEAR_RATIO_NUM / (float)TILT_GEAR_RATIO_DEN;
}

// Convert output-shaft deg/s to MKS speed byte (0–127).
// Motor RPM = deg_s × gear_ratio × 60 / 360
// speed_byte = motor_RPM × Mstep × 200 / 30000
uint8_t MotionController::degSToSpeedByte(AxisId axis, float degS) const {
    float motorRPM = fabsf(degS) * gearRatioFor(axis) * 60.0f / 360.0f;
    float raw      = motorRPM * (float)DEFAULT_MICROSTEPS * 200.0f / 30000.0f;
    if (raw < 1.0f)   raw = 1.0f;
    if (raw > 127.0f) raw = 127.0f;
    return (uint8_t)raw;
}

// Convert output-shaft degrees to microstep count.
uint32_t MotionController::degToPulses(AxisId axis, float deg) const {
    float stepsPerDegF = (float)(MOTOR_STEPS_PER_REV * DEFAULT_MICROSTEPS) *
                         gearRatioFor(axis) / 360.0f;
    return (uint32_t)fabsf(deg * stepsPerDegF);
}

#endif // PTZ_DRIVE_UART

// =============================================================================
// Enable / disable
// =============================================================================

void MotionController::enableAxis(AxisId axis, bool en) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
#ifdef PTZ_DRIVE_STEPPER
    if (_stepper[(int)axis]) {
        if (en) _stepper[(int)axis]->enableOutputs();
        else    _stepper[(int)axis]->disableOutputs();
    }
#else
    _driver[(int)axis]->enableMotor(en);
#endif
    xSemaphoreGive(_mutex);
}

void MotionController::enableAll(bool en) {
    enableAxis(AxisId::PAN,  en);
    enableAxis(AxisId::TILT, en);
}

// =============================================================================
// Position move
// =============================================================================

void MotionController::moveTo(AxisId axis, float deg, bool relative) {
    if (_estop) return;
    int   idx = (int)axis;
    float target;

    xSemaphoreTake(_mutex, portMAX_DELAY);

    float current = _cachedPosDeg[idx];
    target = relative ? current + deg : deg;
    target = clampToLimits(axis, target);

#ifdef PTZ_DRIVE_STEPPER
    if (_stepper[idx])
        _stepper[idx]->moveTo(degToSteps(axis, target));
#else
    float   delta    = target - current;
    uint32_t pulses  = degToPulses(axis, delta);
    if (pulses > 0) {
        uint8_t spd     = degSToSpeedByte(axis, _settings.maxSpeedDegS);
        uint8_t spdDir  = spd | (delta < 0 ? 0x80 : 0x00);
        if (_driver[idx]->runPulses(spdDir, pulses)) {
            _moving[idx] = true;
            _cachedPosDeg[idx] = target; // optimistically update; encoder will correct
        }
    }
#endif

    xSemaphoreGive(_mutex);
}

// =============================================================================
// Velocity control
// =============================================================================

void MotionController::setVelocity(AxisId axis, float degS) {
    if (_estop) return;
    int idx = (int)axis;

    xSemaphoreTake(_mutex, portMAX_DELAY);

    if (_homing[idx]) { xSemaphoreGive(_mutex); return; }

    float pos = _cachedPosDeg[idx];
    float v   = degS;
    if (atMinLimit(axis, pos) && v < 0.0f) v = 0.0f;
    if (atMaxLimit(axis, pos) && v > 0.0f) v = 0.0f;

#ifdef PTZ_DRIVE_STEPPER
    if (!_stepper[idx]) { xSemaphoreGive(_mutex); return; }
    if (fabsf(v) < 0.1f) {
        _stepper[idx]->stopMove();
    } else {
        uint32_t speedHz = (uint32_t)(fabsf(v) * stepsPerDeg(axis));
        if (speedHz < MIN_SPEED_STEPS) speedHz = MIN_SPEED_STEPS;
        _stepper[idx]->setSpeedInHz(speedHz);
        _stepper[idx]->applySpeedAcceleration();
        if (v > 0.0f) _stepper[idx]->runForward();
        else           _stepper[idx]->runBackward();
    }
#else
    if (fabsf(v) < 0.1f) {
        _driver[idx]->stopMotor();
        _moving[idx] = false;
    } else {
        uint8_t spd    = degSToSpeedByte(axis, v);
        uint8_t spdDir = spd | (v < 0.0f ? 0x80 : 0x00);
        _driver[idx]->runVelocity(spdDir);
        _moving[idx] = true;
    }
#endif

    xSemaphoreGive(_mutex);
}

// =============================================================================
// Stop
// =============================================================================

void MotionController::stop(AxisId axis) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
#ifdef PTZ_DRIVE_STEPPER
    if (_stepper[(int)axis]) _stepper[(int)axis]->stopMove();
#else
    _driver[(int)axis]->stopMotor();
    _moving[(int)axis] = false;
#endif
    xSemaphoreGive(_mutex);
}

void MotionController::stopAll() {
    stop(AxisId::PAN);
    stop(AxisId::TILT);
}

void MotionController::emergencyStop() {
    _estop = true;
    xSemaphoreTake(_mutex, portMAX_DELAY);
#ifdef PTZ_DRIVE_STEPPER
    for (int i = 0; i < 2; i++)
        if (_stepper[i])
            _stepper[i]->forceStopAndNewPosition(_stepper[i]->getCurrentPosition());
#else
    for (int i = 0; i < 2; i++) { _driver[i]->stopMotor(); _moving[i] = false; }
#endif
    xSemaphoreGive(_mutex);
    if (_driver[0]) _driver[0]->emergencyStop();
    if (_driver[1]) _driver[1]->emergencyStop();
}

// =============================================================================
// Homing
// =============================================================================

bool MotionController::startHoming(AxisId axis) {
    int idx = (int)axis;
#ifdef PTZ_DRIVE_STEPPER
    xSemaphoreTake(_mutex, portMAX_DELAY);
    if (_stepper[idx])
        _stepper[idx]->forceStopAndNewPosition(_stepper[idx]->getCurrentPosition());
    xSemaphoreGive(_mutex);
#else
    stop(axis);
#endif
    if (!_driver[idx]) return false;   // A4988: no homing support
    if (!_driver[idx]->goHome()) return false;
    _homing[idx]          = true;
    _homingStartMs[idx]   = millis();
    _homingSeenBit[idx]   = false;
    _homingSoftware[idx]  = false;
    _homingPrevAngle[idx] = -1.0f;
    return true;
}

bool MotionController::isHoming(AxisId axis) const {
    return _homing[(int)axis];
}

void MotionController::pollHoming(AxisId axis) {
    int idx = (int)axis;

    // A4988 has no UART — startHoming() returns false so _homing is never set,
    // but guard here for safety.
    if (!_driver[idx]) { _homing[idx] = false; return; }

    // ── Software homing path ──────────────────────────────────────────────
    // Used when the driver didn't respond to 0x91 (older firmware).
    // Run motor slowly; declare home when the encoder wraps through 0°
    // (that transition IS the hall-effect sensor reference position).
    if (_homingSoftware[idx]) {
        float angle;
        if (!_driver[idx]->readEncoderAngle(angle)) return;

        bool wrapDetected = false;
        if (_homingPrevAngle[idx] >= 0.0f) {
            float delta = _homingPrevAngle[idx] - angle;
            // Wrap: prev was near 360°, now near 0° (large positive jump in reverse)
            if (delta > HOMING_SW_WRAP_THRESH) wrapDetected = true;
        }
        _homingPrevAngle[idx] = angle;

        bool timedOut = (uint32_t)(millis() - _homingStartMs[idx]) > HOMING_TIMEOUT_MS;

        if (wrapDetected || timedOut) {
            xSemaphoreTake(_mutex, portMAX_DELAY);
            _driver[idx]->stopMotor();
#ifdef PTZ_DRIVE_STEPPER
            if (_stepper[idx]) _stepper[idx]->forceStopAndNewPosition(0);
#endif
            _cachedPosDeg[idx]      = 0.0f;
            _homing[idx]            = false;
            _homingSoftware[idx]    = false;
            _homingPrevAngle[idx]   = -1.0f;
            xSemaphoreGive(_mutex);
        }
        return;
    }

    // ── Driver-based homing path ──────────────────────────────────────────
    uint8_t status;
    if (!_driver[idx]->readStatus(status)) return;

    bool homingBitNow = (status & MKS_STATUS_HOMING) != 0;
    if (homingBitNow) _homingSeenBit[idx] = true;

    // Condition A: firmware set the HOMING bit and has now cleared it — done.
    bool doneByBit = _homingSeenBit[idx] && !homingBitNow;

    // Condition B: HOMING bit never appeared within HOMING_BIT_WAIT_MS.
    // This means the firmware doesn't support driver-based homing. Switch to
    // software homing: run the motor slowly toward the encoder wrap point.
    bool switchToSoftware = !_homingSeenBit[idx] &&
                            (uint32_t)(millis() - _homingStartMs[idx]) > HOMING_BIT_WAIT_MS;

    // Hard timeout safety valve.
    bool timedOut = (uint32_t)(millis() - _homingStartMs[idx]) > HOMING_TIMEOUT_MS;

    if (doneByBit || timedOut) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        _driver[idx]->stopMotor();
#ifdef PTZ_DRIVE_STEPPER
        if (_stepper[idx]) _stepper[idx]->forceStopAndNewPosition(0);
#endif
        _cachedPosDeg[idx]  = 0.0f;
        _homing[idx]        = false;
        _homingSeenBit[idx] = false;
        xSemaphoreGive(_mutex);

    } else if (switchToSoftware) {
        // Transition to software homing
        _homingSoftware[idx]   = true;
        _homingPrevAngle[idx]  = -1.0f;
        _homingStartMs[idx]    = millis();   // reset timer for software phase
#ifdef PTZ_DRIVE_STEPPER
        if (_stepper[idx]) {
            uint32_t speedHz = (uint32_t)(HOMING_SW_SPEED_DEG_S * stepsPerDeg(axis));
            _stepper[idx]->setSpeedInHz(speedHz);
            _stepper[idx]->applySpeedAcceleration();
            _stepper[idx]->runForward();    // CW — adjust if axis homes CCW
        }
#else
        uint8_t spd    = degSToSpeedByte(axis, HOMING_SW_SPEED_DEG_S);
        uint8_t spdDir = spd | 0x00;        // CW — adjust if axis homes CCW
        _driver[idx]->runVelocity(spdDir);
#endif
    }
}

bool MotionController::calibrateEncoder(AxisId axis) {
    if (!_driver[(int)axis]) return false;
    stop(axis);
    return _driver[(int)axis]->calibrateEncoder();
}

// =============================================================================
// Telemetry
// =============================================================================

void MotionController::updateCachedPosition(AxisId axis) {
    float angle;
#ifdef PTZ_DRIVE_STEPPER
    if (_stepper[(int)axis])
        _cachedPosDeg[(int)axis] = stepsToDeg(axis,
            _stepper[(int)axis]->getCurrentPosition());
#else
    if (_driver[(int)axis]->readEncoderAngle(angle))
        _cachedPosDeg[(int)axis] = angle;
#endif
    (void)angle;
}

float MotionController::getPositionDeg(AxisId axis) const {
    return _cachedPosDeg[(int)axis];
}

bool MotionController::isRunning(AxisId axis) const {
#ifdef PTZ_DRIVE_STEPPER
    return _stepper[(int)axis] && _stepper[(int)axis]->isRunning();
#else
    return _moving[(int)axis];
#endif
}

bool MotionController::readDriverStatus(AxisId axis, uint8_t& statusByte) {
    if (!_driver[(int)axis]) return false;
    return _driver[(int)axis]->readStatus(statusByte);
}

bool MotionController::readEncoderAngle(AxisId axis, float& angleDeg) {
    if (!_driver[(int)axis]) return false;
    return _driver[(int)axis]->readEncoderAngle(angleDeg);
}

bool MotionController::pingDriver(AxisId axis) {
    if (!_driver[(int)axis]) return false;
    return _driver[(int)axis]->ping();
}

void MotionController::diagAxis(AxisId axis, uint8_t func, uint32_t timeoutMs) {
    if (!_driver[(int)axis]) return;
    _driver[(int)axis]->sendRaw(func, nullptr, 0, timeoutMs);
}

// =============================================================================
// Settings
// =============================================================================

void MotionController::loadSettings() {
    _prefs.begin(NVS_NAMESPACE, true);
    _settings.maxSpeedDegS      = _prefs.getFloat("speed",     DEFAULT_MAX_SPEED_DEG_S);
    _settings.accelDegS2        = _prefs.getFloat("accel",     DEFAULT_ACCEL_DEG_S2);
    _settings.fineSpeedScale    = _prefs.getFloat("fine",      DEFAULT_FINE_SPEED_SCALE);
    _settings.panMinDeg         = _prefs.getFloat("pan_min",   PAN_SOFT_LIMIT_MIN);
    _settings.panMaxDeg         = _prefs.getFloat("pan_max",   PAN_SOFT_LIMIT_MAX);
    _settings.tiltMinDeg        = _prefs.getFloat("tilt_min",  TILT_SOFT_LIMIT_MIN);
    _settings.tiltMaxDeg        = _prefs.getFloat("tilt_max",  TILT_SOFT_LIMIT_MAX);
    _settings.softLimitsEnabled = _prefs.getBool ("limits_en", SOFT_LIMITS_ENABLED);
    _prefs.end();
}

void MotionController::saveSettings() {
    _prefs.begin(NVS_NAMESPACE, false);
    _prefs.putFloat("speed",     _settings.maxSpeedDegS);
    _prefs.putFloat("accel",     _settings.accelDegS2);
    _prefs.putFloat("fine",      _settings.fineSpeedScale);
    _prefs.putFloat("pan_min",   _settings.panMinDeg);
    _prefs.putFloat("pan_max",   _settings.panMaxDeg);
    _prefs.putFloat("tilt_min",  _settings.tiltMinDeg);
    _prefs.putFloat("tilt_max",  _settings.tiltMaxDeg);
    _prefs.putBool ("limits_en", _settings.softLimitsEnabled);
    _prefs.end();
}

void MotionController::resetSettings() {
    _settings = MotionSettings{};
#ifdef PTZ_DRIVE_STEPPER
    applyStepperSettings(AxisId::PAN);
    applyStepperSettings(AxisId::TILT);
#endif
}

MotionSettings MotionController::getSettings() const { return _settings; }

void MotionController::applySettings(const MotionSettings& s) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _settings = s;
#ifdef PTZ_DRIVE_STEPPER
    applyStepperSettings(AxisId::PAN);
    applyStepperSettings(AxisId::TILT);
#endif
    xSemaphoreGive(_mutex);
}

// =============================================================================
// FreeRTOS task
// =============================================================================

void MotionController::motionTask(void* param) {
    MotionController* mc = static_cast<MotionController*>(param);

    for (;;) {
        // Update cached positions from encoder (UART mode) or step counter (stepper).
        for (int i = 0; i < 2; i++)
            mc->updateCachedPosition((AxisId)i);

        // Poll homing status.
        for (int i = 0; i < 2; i++)
            if (mc->_homing[i]) mc->pollHoming((AxisId)i);

#ifdef PTZ_DRIVE_UART
        // Poll for runPulses completion.
        for (int i = 0; i < 2; i++) {
            if (!mc->_moving[i]) continue;
            bool done = false;
            mc->_driver[i]->pollRunPulses(done);
            if (done) mc->_moving[i] = false;
        }
#endif

#ifdef PTZ_DRIVE_STEPPER
        // Enforce soft limits for continuous velocity moves.
        if (!mc->_estop) {
            for (int i = 0; i < 2; i++) {
                if (!mc->_stepper[i] || !mc->_stepper[i]->isRunning()) continue;
                AxisId axis = (AxisId)i;
                float  pos  = mc->_cachedPosDeg[i];
                if (mc->atMinLimit(axis, pos) || mc->atMaxLimit(axis, pos)) {
                    xSemaphoreTake(mc->_mutex, portMAX_DELAY);
                    mc->_stepper[i]->stopMove();
                    xSemaphoreGive(mc->_mutex);
                }
            }
        }
#endif

        vTaskDelay(pdMS_TO_TICKS(MOTION_TASK_PERIOD_MS));
    }
}
