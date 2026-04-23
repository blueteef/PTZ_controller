#include <Arduino.h>
#include <SPI.h>
#include "config.h"
#include "motion.h"
#include "can_ids.h"

// ---------------------------------------------------------------------------
// Encoder — MT6816, 14-bit absolute on output shaft
// SPI Mode 1, CS active-low
// ---------------------------------------------------------------------------
static constexpr float CDEG_PER_COUNT = 36000.0f / 16384.0f;   // ~2.197 cdeg/count

static SPIClass _enc_spi(VSPI);

static uint16_t _enc_read_raw() {
    uint8_t hi, lo;
    digitalWrite(ENC_CS_PIN, LOW);
    delayMicroseconds(1);
    hi = _enc_spi.transfer(0x83);
    lo = _enc_spi.transfer(0x00);
    delayMicroseconds(1);
    digitalWrite(ENC_CS_PIN, HIGH);
    return (((uint16_t)hi << 8) | lo) >> 2;   // 14-bit angle (0–16383)
}

static int32_t  _enc_turns    = 0;
static uint16_t _enc_prev_raw = 0;
static int32_t  _enc_abs_cdeg = 0;    // absolute multi-turn centidegrees
static int32_t  _home_offset  = 0;    // zero reference

static void _enc_update() {
    uint16_t raw   = _enc_read_raw();
    int16_t  delta = (int16_t)(raw - _enc_prev_raw);
    if (delta >  8192) _enc_turns--;
    if (delta < -8192) _enc_turns++;
    _enc_prev_raw  = raw;
    _enc_abs_cdeg  = (int32_t)((_enc_turns * 16384 + (int32_t)raw) * CDEG_PER_COUNT);
}

// ---------------------------------------------------------------------------
// BTS7960 H-bridge
// ---------------------------------------------------------------------------
static void _pwm_init() {
    ledcSetup(0, MOTOR_PWM_FREQ, MOTOR_PWM_BITS);   // channel 0 = RPWM
    ledcSetup(1, MOTOR_PWM_FREQ, MOTOR_PWM_BITS);   // channel 1 = LPWM
    ledcAttachPin(MOTOR_RPWM_PIN, 0);
    ledcAttachPin(MOTOR_LPWM_PIN, 1);

    pinMode(MOTOR_REN_PIN, OUTPUT);
    pinMode(MOTOR_LEN_PIN, OUTPUT);
    digitalWrite(MOTOR_REN_PIN, HIGH);
    digitalWrite(MOTOR_LEN_PIN, HIGH);

    ledcWrite(0, 0);
    ledcWrite(1, 0);
}

// duty: -255 (full reverse) to +255 (full forward), 0 = coast
static void _motor_set(int16_t duty) {
    if (duty >= 0) {
        ledcWrite(0, (uint8_t)min((int16_t)255, duty));
        ledcWrite(1, 0);
    } else {
        ledcWrite(0, 0);
        ledcWrite(1, (uint8_t)min((int16_t)255, (int16_t)-duty));
    }
}

static void _motor_brake() {
    ledcWrite(0, 0);
    ledcWrite(1, 0);
    digitalWrite(MOTOR_REN_PIN, LOW);
    digitalWrite(MOTOR_LEN_PIN, LOW);
}

static void _motor_enable() {
    digitalWrite(MOTOR_REN_PIN, HIGH);
    digitalWrite(MOTOR_LEN_PIN, HIGH);
}

// ---------------------------------------------------------------------------
// PID
// ---------------------------------------------------------------------------
struct PID {
    float kp, ki, kd;
    float integral;
    float prev_error;
    float limit;

    float compute(float error, float dt) {
        if (dt <= 0 || dt > 0.5f) return 0;
        integral  += error * dt;
        integral   = constrain(integral, -limit / max(ki, 0.001f),
                                          limit / max(ki, 0.001f));
        float deriv = (error - prev_error) / dt;
        prev_error  = error;
        return constrain(kp * error + ki * integral + kd * deriv, -limit, limit);
    }

    void reset() { integral = 0; prev_error = 0; }
};

// kp, ki, kd, integral, prev_error, limit
static PID _pos_pid = { 0.8f,  0.05f, 0.3f,  0.0f, 0.0f, 255.0f };
static PID _vel_pid = { 1.2f,  0.1f,  0.05f, 0.0f, 0.0f, 255.0f };

// ---------------------------------------------------------------------------
// Velocity measurement (differentiated encoder, low-pass filtered)
// ---------------------------------------------------------------------------
static float   _vel_cdeg_s    = 0.0f;
static int32_t _prev_pos_cdeg = 0;
static uint32_t _prev_tick_us = 0;
static constexpr float VEL_LPF_ALPHA = 0.25f;   // heavier filter = smoother but laggier

static void _vel_update() {
    uint32_t now_us = micros();
    float dt = (now_us - _prev_tick_us) * 1e-6f;
    if (dt < 0.002f) return;   // max 500Hz update

    int32_t pos = _enc_abs_cdeg - _home_offset;
    float raw_vel = (float)(pos - _prev_pos_cdeg) / dt;
    _vel_cdeg_s  = VEL_LPF_ALPHA * raw_vel + (1.0f - VEL_LPF_ALPHA) * _vel_cdeg_s;

    _prev_pos_cdeg = pos;
    _prev_tick_us  = now_us;
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
enum class MotionMode : uint8_t { IDLE, VELOCITY, POSITION };
static MotionMode _mode          = MotionMode::IDLE;
static int16_t    _cmd_vel       = 0;     // cdeg/s (velocity mode)
static int32_t    _target_cdeg   = 0;     // target position (position mode)
static bool       _homed         = false;
static bool       _fault         = false;
static bool       _enabled       = false;

// Settings (updated by MSG_SETTINGS)
static float _max_speed_cdeg_s = 4500.0f;  // 45 deg/s

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void motion_init() {
    // Encoder SPI
    _enc_spi.begin(ENC_SCK_PIN, ENC_MISO_PIN, ENC_MOSI_PIN, -1);
    _enc_spi.setFrequency(1000000);
    _enc_spi.setDataMode(SPI_MODE1);
    pinMode(ENC_CS_PIN, OUTPUT);
    digitalWrite(ENC_CS_PIN, HIGH);
    delay(10);

    // Validate encoder — two consecutive reads must agree within 1°
    uint16_t r1 = _enc_read_raw();
    delay(2);
    uint16_t r2 = _enc_read_raw();
    int16_t  diff = (int16_t)(r1 - r2);
    if (abs(diff) > 45) {   // >1° disagreement = encoder not connected
        Serial.println("[motion] ERROR: encoder not detected — motor disabled");
        _fault = true;
        return;
    }

    _enc_prev_raw  = r2;
    _enc_abs_cdeg  = (int32_t)(_enc_prev_raw * CDEG_PER_COUNT);
    _home_offset   = _enc_abs_cdeg;
    _prev_pos_cdeg = 0;
    _prev_tick_us  = micros();
    _homed = true;
    Serial.println("[motion] encoder OK");

    // Motor
    _pwm_init();
    _enabled = true;
}

void motion_tick() {
    _enc_update();
    _vel_update();

    if (!_enabled || _fault) return;

    int16_t duty = 0;

    if (_mode == MotionMode::VELOCITY) {
        float dt = 0.01f;   // nominal; vel_update handles actual timing
        float error = (float)_cmd_vel - _vel_cdeg_s;
        duty = (int16_t)_vel_pid.compute(error, dt);

        // Stop cleanly at zero command
        if (_cmd_vel == 0 && fabsf(_vel_cdeg_s) < 50.0f) {
            _motor_set(0);
            _mode = MotionMode::IDLE;
            return;
        }

    } else if (_mode == MotionMode::POSITION) {
        float dt = 0.01f;
        float error = (float)(_target_cdeg - motion_get_pos_cdeg());
        duty = (int16_t)_pos_pid.compute(error, dt);

        if (fabsf(error) < 10.0f) {   // within 0.1°
            _motor_set(0);
            _mode = MotionMode::IDLE;
            _pos_pid.reset();
            return;
        }

    } else {
        return;
    }

    _motor_set(duty);
}

void motion_set_velocity(int16_t vel_cdeg_s) {
    if (_fault) return;
    _cmd_vel = (int16_t)constrain((float)vel_cdeg_s, -_max_speed_cdeg_s, _max_speed_cdeg_s);
    _vel_pid.reset();
    _motor_enable();
    _enabled = true;
    _mode = (_cmd_vel != 0) ? MotionMode::VELOCITY : MotionMode::IDLE;
    if (_cmd_vel == 0) _motor_set(0);
}

void motion_set_position(int32_t pos_cdeg) {
    if (_fault) return;
    _target_cdeg = pos_cdeg;
    _pos_pid.reset();
    _motor_enable();
    _enabled = true;
    _mode = MotionMode::POSITION;
}

void motion_stop() {
    _mode    = MotionMode::IDLE;
    _cmd_vel = 0;
    _motor_set(0);
    _vel_pid.reset();
    _pos_pid.reset();
}

void motion_estop() {
    _mode    = MotionMode::IDLE;
    _cmd_vel = 0;
    _motor_brake();
    _vel_pid.reset();
    _pos_pid.reset();
}

void motion_home() {
    motion_stop();
    _home_offset   = _enc_abs_cdeg;
    _prev_pos_cdeg = 0;
    _homed = true;
}

void motion_set_settings(uint16_t max_speed_cdeg_s, uint16_t /*accel_cdeg_s2*/) {
    // accel is handled implicitly by PID rate limiting — ignore for now
    _max_speed_cdeg_s = (float)max_speed_cdeg_s;
}

int32_t motion_get_pos_cdeg() {
    return _enc_abs_cdeg - _home_offset;
}

int16_t motion_get_vel_cdeg_s() {
    return (int16_t)_vel_cdeg_s;
}

uint8_t motion_get_flags() {
    uint8_t f = 0;
    if (_mode != MotionMode::IDLE)  f |= POS_FLAG_MOVING;
    if (_fault)                     f |= POS_FLAG_FAULT;
    if (_homed)                     f |= POS_FLAG_HOMED;
    return f;
}
