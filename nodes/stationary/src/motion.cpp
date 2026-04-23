#include <Arduino.h>
#include <SPI.h>
#include <FastAccelStepper.h>
#include <TMCStepper.h>
#include "config.h"
#include "motion.h"
#include "can_ids.h"

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

// Steps per degree at output shaft = (steps/rev_motor * microsteps) / (gear_ratio * 360)
// NEMA17 = 200 steps/rev, 16 microsteps, 26:1 gearbox
// → 200 * 16 / (26 * 360) = 3200 / 9360 ≈ 0.342 steps/degree
// Inverted: degrees/step = 9360 / 3200 = 2.925 deg/step
// Centidegrees per step = 292.5
static constexpr float STEPS_PER_CDEG  = (200.0f * 16.0f) / (26.0f * 36000.0f);
static constexpr float CDEG_PER_STEP   = 1.0f / STEPS_PER_CDEG;

// Encoder: 14-bit absolute on output shaft
static constexpr float CDEG_PER_ENC_COUNT = 36000.0f / 16384.0f;  // ~2.197 cdeg/count

// Motion defaults (overridden by MSG_SETTINGS)
static uint16_t _max_speed_cdeg_s = 4500;   // 45 deg/s
static uint16_t _accel_cdeg_s2    = 12000;  // 120 deg/s²

// ---------------------------------------------------------------------------
// Stepper
// ---------------------------------------------------------------------------
static FastAccelStepperEngine _engine;
static FastAccelStepper       *_stepper = nullptr;
static HardwareSerial          _tmc_serial(2);   // SERIAL2
static TMC2209Stepper           _driver(&_tmc_serial, 0.11f, TMC_ADDR);

// ---------------------------------------------------------------------------
// Encoder
// ---------------------------------------------------------------------------
static SPIClass _enc_spi(VSPI);

static uint16_t _enc_read_raw() {
    uint8_t hi, lo;
    digitalWrite(ENC_CS_PIN, LOW);
    delayMicroseconds(1);
    hi = _enc_spi.transfer(0x83);   // read angle register
    lo = _enc_spi.transfer(0x00);
    delayMicroseconds(1);
    digitalWrite(ENC_CS_PIN, HIGH);
    return (((uint16_t)hi << 8) | lo) >> 2;   // 14-bit angle
}

// Track encoder revolutions for multi-turn position
static int32_t  _enc_turns      = 0;
static uint16_t _enc_prev_raw   = 0;
static int32_t  _enc_pos_cdeg   = 0;   // absolute position in centidegrees
static int32_t  _home_offset_cdeg = 0; // subtracted from enc_pos to give output position

static void _enc_update() {
    uint16_t raw = _enc_read_raw();
    int16_t  delta = (int16_t)(raw - _enc_prev_raw);

    // Unwrap: if delta jumps more than half-revolution, it crossed zero
    if (delta >  8192) _enc_turns--;
    if (delta < -8192) _enc_turns++;

    _enc_prev_raw  = raw;
    _enc_pos_cdeg  = (int32_t)((_enc_turns * 16384 + raw) * CDEG_PER_ENC_COUNT);
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
enum class MotionMode : uint8_t { IDLE, VELOCITY, POSITION, HOMING };
static MotionMode _mode    = MotionMode::IDLE;
static int32_t    _target_cdeg = 0;
static int16_t    _last_vel_cdeg_s = 0;
static bool       _homed   = false;
static bool       _fault   = false;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static int32_t _cdeg_to_steps(int32_t cdeg) {
    return (int32_t)((float)cdeg * STEPS_PER_CDEG);
}

static void _apply_settings() {
    if (!_stepper) return;
    uint32_t steps_per_s  = (uint32_t)((float)_max_speed_cdeg_s  * STEPS_PER_CDEG);
    uint32_t steps_per_s2 = (uint32_t)((float)_accel_cdeg_s2     * STEPS_PER_CDEG);
    _stepper->setSpeedInHz(max(steps_per_s, (uint32_t)1));
    _stepper->setAcceleration(max(steps_per_s2, (uint32_t)1));
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void motion_init() {
    // TMC2209 UART
    _tmc_serial.begin(TMC_UART_BAUD, SERIAL_8N1, TMC_UART_RX, TMC_UART_TX);
    delay(100);
    _driver.begin();
    _driver.toff(5);
    _driver.rms_current(800);        // mA — tune for your motor
    _driver.microsteps(16);
    _driver.en_spreadCycle(false);   // StealthChop
    _driver.pwm_autoscale(true);

    // Stepper
    _engine.init();
    pinMode(EN_PIN, OUTPUT);
    digitalWrite(EN_PIN, LOW);       // enable driver (active low)
    _stepper = _engine.stepperConnectToPin(STEP_PIN);
    if (_stepper) {
        _stepper->setDirectionPin(DIR_PIN);
        _stepper->setEnablePin(EN_PIN);
        _stepper->setAutoEnable(true);
        _apply_settings();
    }

    // Encoder SPI
    _enc_spi.begin(ENC_SCK_PIN, ENC_MISO_PIN, ENC_MOSI_PIN, -1);
    _enc_spi.setFrequency(1000000);  // 1 MHz — conservative start
    _enc_spi.setDataMode(SPI_MODE1);
    pinMode(ENC_CS_PIN, OUTPUT);
    digitalWrite(ENC_CS_PIN, HIGH);
    delay(10);

    // Initial encoder read
    _enc_prev_raw = _enc_read_raw();
    _enc_pos_cdeg = (int32_t)(_enc_prev_raw * CDEG_PER_ENC_COUNT);
    _home_offset_cdeg = _enc_pos_cdeg;   // treat startup position as home
    _homed = true;
}

void motion_tick() {
    _enc_update();

    if (_mode == MotionMode::POSITION && _stepper) {
        // Simple position mode — drive to target via encoder feedback
        int32_t rel_pos = _enc_pos_cdeg - _home_offset_cdeg;
        int32_t error   = _target_cdeg - rel_pos;
        if (abs(error) < 10) {  // within 0.1° — stop
            _stepper->stopMove();
            _mode = MotionMode::IDLE;
        } else {
            int32_t target_steps = _cdeg_to_steps(_target_cdeg + _home_offset_cdeg);
            _stepper->moveTo(target_steps);
        }
    }
}

void motion_set_velocity(int16_t vel_cdeg_s) {
    if (!_stepper || _fault) return;
    _last_vel_cdeg_s = vel_cdeg_s;
    _mode = MotionMode::VELOCITY;

    if (vel_cdeg_s == 0) {
        _stepper->stopMove();
        _mode = MotionMode::IDLE;
        return;
    }

    uint32_t speed_steps = (uint32_t)(abs(vel_cdeg_s) * STEPS_PER_CDEG);
    speed_steps = max(speed_steps, (uint32_t)1);
    _stepper->setSpeedInHz(speed_steps);

    if (vel_cdeg_s > 0)
        _stepper->runForward();
    else
        _stepper->runBackward();
}

void motion_set_position(int32_t pos_cdeg) {
    if (!_stepper || _fault) return;
    _target_cdeg = pos_cdeg;
    _mode = MotionMode::POSITION;
    _apply_settings();
}

void motion_stop() {
    if (_stepper) _stepper->stopMove();
    _mode = MotionMode::IDLE;
    _last_vel_cdeg_s = 0;
}

void motion_estop() {
    if (_stepper) _stepper->forceStop();
    _mode = MotionMode::IDLE;
    _last_vel_cdeg_s = 0;
}

void motion_home() {
    _home_offset_cdeg = _enc_pos_cdeg;
    _homed = true;
    motion_stop();
}

void motion_set_settings(uint16_t max_speed_cdeg_s, uint16_t accel_cdeg_s2) {
    _max_speed_cdeg_s = max_speed_cdeg_s;
    _accel_cdeg_s2    = accel_cdeg_s2;
    _apply_settings();
}

int32_t motion_get_pos_cdeg() {
    return _enc_pos_cdeg - _home_offset_cdeg;
}

int16_t motion_get_vel_cdeg_s() {
    return _last_vel_cdeg_s;
}

uint8_t motion_get_flags() {
    uint8_t f = 0;
    if (_stepper && _stepper->isRunning()) f |= POS_FLAG_MOVING;
    if (_fault)                            f |= POS_FLAG_FAULT;
    if (_homed)                            f |= POS_FLAG_HOMED;
    return f;
}
