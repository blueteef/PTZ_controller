#include <Arduino.h>
#include <SPI.h>
#include "config.h"
#include "motion.h"
#include "can_ids.h"

// ---------------------------------------------------------------------------
// Encoder — MT6816, 14-bit absolute on PINION axle (read-only, no MOSI)
// SPI Mode 3 (CPOL=1 CPHA=1), CS active-low.
// Pinion turns ENCODER_GEAR_RATIO times per pan shaft revolution.
// Pan position = (rev_count * 36000) + (pinion_angle_cdeg / ENCODER_GEAR_RATIO)
// ---------------------------------------------------------------------------
static constexpr float CDEG_PER_COUNT =
    36000.0f / 16384.0f;                    // centideg per raw encoder count
static constexpr float PAN_CDEG_PER_COUNT =
    CDEG_PER_COUNT / ENCODER_GEAR_RATIO;    // corrected for gear ratio

static SPIClass _enc_spi(VSPI);

static uint16_t _enc_read_raw() {
    SPISettings cfg(1000000UL, MSBFIRST, SPI_MODE3);
    _enc_spi.beginTransaction(cfg);

    // Reg 0x03: angle[13:6] — CS falling edge latches snapshot
    digitalWrite(ENC_CS_PIN, LOW);
    _enc_spi.transfer(0x83);
    uint8_t hi = _enc_spi.transfer(0x00);
    digitalWrite(ENC_CS_PIN, HIGH);
    delayMicroseconds(1);

    // Reg 0x04: angle[5:0] | NO_MAG | parity — reads from same latch
    digitalWrite(ENC_CS_PIN, LOW);
    _enc_spi.transfer(0x84);
    uint8_t lo = _enc_spi.transfer(0x00);
    digitalWrite(ENC_CS_PIN, HIGH);
    delayMicroseconds(1);

    _enc_spi.endTransaction();

    if (lo & 0x02) Serial.println("[enc] NO_MAG");
    return ((uint16_t)hi << 6) | (lo >> 2);
}

// ---------------------------------------------------------------------------
// Hall sensor — revolution counter
// Interrupt-driven, direction inferred from motor command state.
// ---------------------------------------------------------------------------
static volatile int32_t _hall_revs = 0;   // signed revolution count
static volatile bool     _hall_dir  = true; // true = forward (RPWM active)

static void IRAM_ATTR _hall_isr() {
    static uint32_t last_ms = 0;
    uint32_t now = millis();
    if (now - last_ms < 100) return;   // 100ms debounce — ignore noise
    last_ms = now;
    if (_hall_dir) _hall_revs++;
    else           _hall_revs--;
}

// ---------------------------------------------------------------------------
// Combined position state
// ---------------------------------------------------------------------------
uint16_t _enc_prev_raw = 0;
static int32_t  _enc_abs_cdeg = 0;   // absolute pan position in centidegrees
static int32_t  _home_offset  = 0;

static void _enc_update() {
    uint16_t raw = _enc_read_raw();
    // Use hall rev count for full revolutions, encoder for fractional angle
    int32_t revs      = _hall_revs;
    float   pinion_cdeg = raw * CDEG_PER_COUNT;
    _enc_abs_cdeg = (int32_t)(revs * 36000.0f + pinion_cdeg / ENCODER_GEAR_RATIO);
    _enc_prev_raw = raw;
}

// ---------------------------------------------------------------------------
// BTS7960 H-bridge
// ---------------------------------------------------------------------------
static void _pwm_init() {
    ledcSetup(0, MOTOR_PWM_FREQ, MOTOR_PWM_BITS);
    ledcSetup(1, MOTOR_PWM_FREQ, MOTOR_PWM_BITS);
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
    _hall_dir = (duty >= 0);
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
// Velocity measurement — derived from raw encoder angle change only.
// Must NOT use _enc_abs_cdeg (which jumps discontinuously on hall triggers).
// ---------------------------------------------------------------------------
static float    _vel_cdeg_s   = 0.0f;
static uint16_t _vel_prev_raw = 0;
static uint32_t _prev_tick_us = 0;
static constexpr float VEL_LPF_ALPHA = 0.25f;

static void _vel_update() {
    uint32_t now_us = micros();
    float dt = (now_us - _prev_tick_us) * 1e-6f;
    if (dt < 0.002f) return;

    // Differentiate raw encoder counts — unwrap to handle 0/16383 rollover
    int16_t delta = (int16_t)(_enc_prev_raw - _vel_prev_raw);
    if (delta >  8192) delta -= 16384;
    if (delta < -8192) delta += 16384;

    float raw_vel = (delta * PAN_CDEG_PER_COUNT) / dt;
    _vel_cdeg_s   = VEL_LPF_ALPHA * raw_vel + (1.0f - VEL_LPF_ALPHA) * _vel_cdeg_s;

    _vel_prev_raw = _enc_prev_raw;
    _prev_tick_us = now_us;
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
    pinMode(ENC_CS_PIN, OUTPUT);
    digitalWrite(ENC_CS_PIN, HIGH);
    _enc_spi.begin(ENC_SCK_PIN, ENC_MISO_PIN, ENC_MOSI_PIN, ENC_CS_PIN);
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
    _vel_prev_raw  = r2;
    _hall_revs     = 0;
    _enc_abs_cdeg  = (int32_t)(r2 * PAN_CDEG_PER_COUNT);
    _home_offset   = _enc_abs_cdeg;
    _prev_tick_us  = micros();
    _homed = true;
    Serial.println("[motion] encoder OK");

    // Attach hall interrupt AFTER home offset is set — avoids startup noise
    // corrupting the revolution count before the reference is established
    pinMode(HALL_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(HALL_PIN), _hall_isr, FALLING);

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
