// =============================================================================
// MotionController.cpp
// =============================================================================

#include "MotionController.h"
#include <Arduino.h>
#include <math.h>

static constexpr uint32_t MOTION_TASK_PERIOD_MS = 20;

// =============================================================================
// Construction / begin
// =============================================================================

MotionController::MotionController() {}

bool MotionController::begin() {
    _mutex = xSemaphoreCreateMutex();
    if (!_mutex) return false;

    _engine.init();

    // ── Pan axis ──────────────────────────────────────────────────────────────
    _stepper[0] = _engine.stepperConnectToPin(PAN_STEP_PIN);
    if (!_stepper[0]) return false;
    _stepper[0]->setDirectionPin(PAN_DIR_PIN, PAN_DIR_INVERT);
    _stepper[0]->setAutoEnable(false);  // we manage EN pins directly

    // ── Tilt axis ─────────────────────────────────────────────────────────────
    _stepper[1] = _engine.stepperConnectToPin(TILT_STEP_PIN);
    if (!_stepper[1]) return false;
    _stepper[1]->setDirectionPin(TILT_DIR_PIN, TILT_DIR_INVERT);
    _stepper[1]->setAutoEnable(false);

    // Drive EN pins directly — A4988: LOW = enabled, HIGH = disabled.
    pinMode(PAN_EN_PIN,  OUTPUT);
    pinMode(TILT_EN_PIN, OUTPUT);
    digitalWrite(PAN_EN_PIN,  LOW);
    digitalWrite(TILT_EN_PIN, LOW);

    loadSettings();
    applyStepperSettings(AxisId::PAN);
    applyStepperSettings(AxisId::TILT);
    return true;
}

// =============================================================================
// Private helpers
// =============================================================================

float MotionController::stepsPerDeg(AxisId axis) const {
    if (axis == AxisId::PAN)
        return (float)(MOTOR_STEPS_PER_REV * DEFAULT_MICROSTEPS)
               * (float)PAN_GEAR_RATIO_NUM / (360.0f * (float)PAN_GEAR_RATIO_DEN);
    return (float)(MOTOR_STEPS_PER_REV * DEFAULT_MICROSTEPS)
           * (float)TILT_GEAR_RATIO_NUM / (360.0f * (float)TILT_GEAR_RATIO_DEN);
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
// Enable / disable
// =============================================================================

void MotionController::enableAxis(AxisId axis, bool en) {
    uint8_t pin = (axis == AxisId::PAN) ? PAN_EN_PIN : TILT_EN_PIN;
    // A4988: LOW = enabled, HIGH = disabled
    digitalWrite(pin, en ? LOW : HIGH);
}

void MotionController::enableAll(bool en) {
    enableAxis(AxisId::PAN,  en);
    enableAxis(AxisId::TILT, en);
}

// =============================================================================
// Velocity control
// =============================================================================

void MotionController::setVelocity(AxisId axis, float degS) {
    if (_estop) return;
    int idx = (int)axis;

    xSemaphoreTake(_mutex, portMAX_DELAY);

    if (!_stepper[idx]) { xSemaphoreGive(_mutex); return; }

    // Enforce soft limits: zero velocity into a limit wall.
    float pos = _cachedPosDeg[idx];
    if (atMinLimit(axis, pos) && degS < 0.0f) degS = 0.0f;
    if (atMaxLimit(axis, pos) && degS > 0.0f) degS = 0.0f;

    if (fabsf(degS) < MIN_VELOCITY_DEG_S) {
        // Stop command — decelerate to rest.
        _stepper[idx]->stopMove();
        _lastDir[idx] = 0;
    } else {
        int8_t newDir = (degS > 0.0f) ? 1 : -1;

        // Clamp to max speed.
        float maxSpeedDeg = _settings.maxSpeedDegS;
        if (fabsf(degS) > maxSpeedDeg) degS = (float)newDir * maxSpeedDeg;

        uint32_t speedHz = (uint32_t)(fabsf(degS) * stepsPerDeg(axis));
        if (speedHz < 5) speedHz = 5;

        _stepper[idx]->setSpeedInHz(speedHz);

        if (newDir != _lastDir[idx]) {
            // Direction change (or starting from stopped): issue a new run command.
            // setSpeedInHz is picked up automatically by runForward/runBackward.
            if (newDir > 0) _stepper[idx]->runForward();
            else             _stepper[idx]->runBackward();
            _lastDir[idx] = newDir;
        } else {
            // Same direction, just update speed on the running move.
            _stepper[idx]->applySpeedAcceleration();
        }
    }

    xSemaphoreGive(_mutex);
}

// =============================================================================
// Position move
// =============================================================================

void MotionController::moveTo(AxisId axis, float deg, bool relative) {
    if (_estop) return;
    int idx = (int)axis;

    xSemaphoreTake(_mutex, portMAX_DELAY);

    if (!_stepper[idx]) { xSemaphoreGive(_mutex); return; }

    float current = _cachedPosDeg[idx];
    float target  = relative ? current + deg : deg;
    target = clampToLimits(axis, target);

    _stepper[idx]->moveTo(degToSteps(axis, target));
    _lastDir[idx] = 0;  // position move manages its own direction

    xSemaphoreGive(_mutex);
}

// =============================================================================
// Stop / e-stop
// =============================================================================

void MotionController::stop(AxisId axis) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    if (_stepper[(int)axis]) {
        _stepper[(int)axis]->stopMove();
        _lastDir[(int)axis] = 0;
    }
    xSemaphoreGive(_mutex);
}

void MotionController::stopAll() {
    stop(AxisId::PAN);
    stop(AxisId::TILT);
}

void MotionController::emergencyStop() {
    _estop = true;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    for (int i = 0; i < 2; i++) {
        if (_stepper[i]) {
            _stepper[i]->forceStopAndNewPosition(_stepper[i]->getCurrentPosition());
            _lastDir[i] = 0;
        }
    }
    xSemaphoreGive(_mutex);
}

void MotionController::clearEstop() {
    _estop = false;
    enableAll(true);
}

// =============================================================================
// Telemetry
// =============================================================================

float MotionController::getPositionDeg(AxisId axis) const {
    return _cachedPosDeg[(int)axis];
}

bool MotionController::isRunning(AxisId axis) const {
    return _stepper[(int)axis] && _stepper[(int)axis]->isRunning();
}

// =============================================================================
// Settings
// =============================================================================

void MotionController::loadSettings() {
    _prefs.begin(NVS_NAMESPACE, true);
    _settings.maxSpeedDegS      = _prefs.getFloat("speed",    DEFAULT_MAX_SPEED_DEG_S);
    _settings.accelDegS2        = _prefs.getFloat("accel",    DEFAULT_ACCEL_DEG_S2);
    _settings.fineSpeedScale    = _prefs.getFloat("fine",     DEFAULT_FINE_SPEED_SCALE);
    _settings.panMinDeg         = _prefs.getFloat("pan_min",  PAN_SOFT_LIMIT_MIN);
    _settings.panMaxDeg         = _prefs.getFloat("pan_max",  PAN_SOFT_LIMIT_MAX);
    _settings.tiltMinDeg        = _prefs.getFloat("tilt_min", TILT_SOFT_LIMIT_MIN);
    _settings.tiltMaxDeg        = _prefs.getFloat("tilt_max", TILT_SOFT_LIMIT_MAX);
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
    applyStepperSettings(AxisId::PAN);
    applyStepperSettings(AxisId::TILT);
}

MotionSettings MotionController::getSettings() const { return _settings; }

void MotionController::applySettings(const MotionSettings& s) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _settings = s;
    applyStepperSettings(AxisId::PAN);
    applyStepperSettings(AxisId::TILT);
    xSemaphoreGive(_mutex);
}

// =============================================================================
// FreeRTOS task — updates cached position and enforces soft limits
// =============================================================================

void MotionController::motionTask(void* param) {
    MotionController* mc = static_cast<MotionController*>(param);

    for (;;) {
        // Update step-counter based position cache.
        for (int i = 0; i < 2; i++) {
            if (mc->_stepper[i])
                mc->_cachedPosDeg[i] = mc->stepsToDeg(
                    (AxisId)i, mc->_stepper[i]->getCurrentPosition());
        }

        // Enforce soft limits for continuous velocity moves.
        if (!mc->_estop) {
            for (int i = 0; i < 2; i++) {
                if (!mc->_stepper[i] || !mc->_stepper[i]->isRunning()) continue;
                AxisId axis = (AxisId)i;
                float  pos  = mc->_cachedPosDeg[i];
                if (mc->atMinLimit(axis, pos) || mc->atMaxLimit(axis, pos)) {
                    xSemaphoreTake(mc->_mutex, portMAX_DELAY);
                    mc->_stepper[i]->stopMove();
                    mc->_lastDir[i] = 0;
                    xSemaphoreGive(mc->_mutex);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(MOTION_TASK_PERIOD_MS));
    }
}
