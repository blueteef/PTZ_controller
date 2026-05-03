#include <Arduino.h>
#include <SPI.h>
#include "config.h"
#include "motion.h"
#include "can_ids.h"

// Identical to stationary node motion.cpp — BTS7960 + MT6816 bit-bang + hall homing.
// Axis reported is AXIS_TILT (handled in main.cpp pos report).

static constexpr float CDEG_PER_COUNT =
    36000.0f / 16384.0f;
static constexpr float PAN_CDEG_PER_COUNT =
    CDEG_PER_COUNT / ENCODER_GEAR_RATIO;

static SPIClass _enc_spi(VSPI);

static uint8_t _bb_transfer(uint8_t val) {
    uint8_t result = 0;
    for (int i = 7; i >= 0; i--) {
        digitalWrite(ENC_MOSI_PIN, (val >> i) & 1);
        delayMicroseconds(1);
        digitalWrite(ENC_SCK_PIN, LOW);
        delayMicroseconds(1);
        result |= (digitalRead(ENC_MISO_PIN) << i);
        digitalWrite(ENC_SCK_PIN, HIGH);
        delayMicroseconds(1);
    }
    return result;
}

static uint8_t _bb_read_reg(uint8_t reg) {
    digitalWrite(ENC_CS_PIN, LOW);
    delayMicroseconds(1);
    _bb_transfer(0x80 | reg);
    uint8_t data = _bb_transfer(0x00);
    digitalWrite(ENC_CS_PIN, HIGH);
    delayMicroseconds(2);
    return data;
}

static volatile int32_t _hall_revs   = 0;
static volatile bool    _hall_dir    = true;
static volatile bool    _hall_fired  = false;

static void IRAM_ATTR _hall_isr() {
    static uint32_t last_ms = 0;
    uint32_t now = millis();
    if (now - last_ms < 100) return;
    last_ms = now;
    _hall_fired = true;
    if (_hall_dir) _hall_revs++;
    else           _hall_revs--;
}

static uint16_t _enc_prev_raw = 0;
static int16_t  _enc_turns    = 0;
static int32_t  _enc_abs_cdeg = 0;
static int32_t  _home_offset  = 0;

static bool _enc_parity_ok(uint8_t hi, uint8_t lo) {
    uint16_t word = ((uint16_t)hi << 8) | lo;
    uint8_t ones = 0;
    for (int i = 1; i <= 15; i++)
        if (word & (1 << i)) ones++;
    return (word & 0x01) == (ones % 2 == 0 ? 0 : 1);
}

static uint16_t _enc_read_raw() {
    uint8_t hi = _bb_read_reg(0x03);
    uint8_t lo = _bb_read_reg(0x04);
    if ((lo & 0x02) || !_enc_parity_ok(hi, lo))
        return _enc_prev_raw;
    return ((uint16_t)hi << 6) | (lo >> 2);
}

static void _enc_update() {
    uint16_t raw   = _enc_read_raw();
    int16_t  delta = (int16_t)(raw - _enc_prev_raw);
    if (abs(delta) > 300 && abs(delta) < 16000) return;
    if (delta >  8192) _enc_turns--;
    if (delta < -8192) _enc_turns++;
    _enc_prev_raw = raw;
    int32_t enc_total = (int32_t)_enc_turns * 16384 + (int32_t)raw;
    _enc_abs_cdeg = (int32_t)((float)enc_total * CDEG_PER_COUNT / ENCODER_GEAR_RATIO);
}

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
    ledcWrite(0, 0); ledcWrite(1, 0);
    digitalWrite(MOTOR_REN_PIN, LOW);
    digitalWrite(MOTOR_LEN_PIN, LOW);
}

static void _motor_enable() {
    digitalWrite(MOTOR_REN_PIN, HIGH);
    digitalWrite(MOTOR_LEN_PIN, HIGH);
}

struct PID {
    float kp, ki, kd, integral, prev_error, limit;
    float compute(float error, float dt) {
        if (dt <= 0 || dt > 0.5f) return 0;
        integral += error * dt;
        integral  = constrain(integral, -limit/max(ki,0.001f), limit/max(ki,0.001f));
        float d   = (error - prev_error) / dt;
        prev_error = error;
        return constrain(kp*error + ki*integral + kd*d, -limit, limit);
    }
    void reset() { integral = 0; prev_error = 0; }
};

static PID _pos_pid = { 0.8f, 0.05f, 0.3f,  0.0f, 0.0f, 255.0f };
static PID _vel_pid = { 1.2f, 0.1f,  0.05f, 0.0f, 0.0f, 255.0f };

static float    _vel_cdeg_s   = 0.0f;
static uint16_t _vel_prev_raw = 0;
static uint32_t _prev_tick_us = 0;
static constexpr float VEL_LPF_ALPHA = 0.25f;

static void _vel_update() {
    uint32_t now_us = micros();
    float dt = (now_us - _prev_tick_us) * 1e-6f;
    if (dt < 0.002f) return;
    int16_t delta = (int16_t)(_enc_prev_raw - _vel_prev_raw);
    if (delta >  8192) delta -= 16384;
    if (delta < -8192) delta += 16384;
    float raw_vel = (delta * PAN_CDEG_PER_COUNT) / dt;
    _vel_cdeg_s   = VEL_LPF_ALPHA * raw_vel + (1.0f - VEL_LPF_ALPHA) * _vel_cdeg_s;
    _vel_prev_raw = _enc_prev_raw;
    _prev_tick_us = now_us;
}

enum class MotionMode : uint8_t { IDLE, VELOCITY, POSITION, HOMING };
static MotionMode _mode          = MotionMode::IDLE;
static uint32_t   _home_start_ms = 0;
static int16_t    _cmd_vel       = 0;
static float      _ramp_vel      = 0.0f;
static int32_t    _target_cdeg   = 0;
static bool       _homed         = false;
static bool       _fault         = false;
static bool       _enabled       = false;

static float _max_speed_cdeg_s = 4500.0f;
static float _accel_cdeg_s2    = 12000.0f;

void motion_init() {
    pinMode(ENC_CS_PIN,   OUTPUT); digitalWrite(ENC_CS_PIN,   HIGH);
    pinMode(ENC_SCK_PIN,  OUTPUT); digitalWrite(ENC_SCK_PIN,  HIGH);
    pinMode(ENC_MOSI_PIN, OUTPUT); digitalWrite(ENC_MOSI_PIN, LOW);
    pinMode(ENC_MISO_PIN, INPUT);
    delay(10);

    uint16_t r1 = _enc_read_raw();
    delay(2);
    uint16_t r2 = _enc_read_raw();
    if (abs((int16_t)(r1 - r2)) > 45) {
        Serial.println("[motion] ERROR: encoder not detected — motor disabled");
        _fault = true;
        return;
    }

    _enc_prev_raw = r2;
    _vel_prev_raw = r2;
    _enc_turns    = 0;
    _hall_revs    = 0;
    _enc_abs_cdeg = (int32_t)(r2 * PAN_CDEG_PER_COUNT);
    _home_offset  = _enc_abs_cdeg;
    _prev_tick_us = micros();
    _homed = true;
    Serial.println("[motion] encoder OK");

    pinMode(HALL_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(HALL_PIN), _hall_isr, FALLING);
    _pwm_init();
    _enabled = true;
}

void motion_tick() {
    _enc_update();
    _vel_update();
    if (!_enabled || _fault) return;

    int16_t duty = 0;

    if (_mode == MotionMode::VELOCITY) {
        static uint32_t last_ramp_us = 0;
        uint32_t now_us = micros();
        float dt_ramp = (now_us - last_ramp_us) * 1e-6f;
        if (dt_ramp > 0.0f && dt_ramp < 0.5f) {
            float step = _accel_cdeg_s2 * dt_ramp;
            float target = (float)_cmd_vel;
            if (_ramp_vel < target) _ramp_vel = fminf(_ramp_vel + step, target);
            else if (_ramp_vel > target) _ramp_vel = fmaxf(_ramp_vel - step, target);
        }
        last_ramp_us = now_us;
        duty = (int16_t)_vel_pid.compute(_ramp_vel - _vel_cdeg_s, 0.01f);
        if (_cmd_vel == 0 && fabsf(_ramp_vel) < 10.0f && fabsf(_vel_cdeg_s) < 50.0f) {
            _ramp_vel = 0.0f; _motor_set(0); _mode = MotionMode::IDLE; return;
        }
    } else if (_mode == MotionMode::POSITION) {
        float error = (float)(_target_cdeg - motion_get_pos_cdeg());
        duty = (int16_t)_pos_pid.compute(error, 0.01f);
        if (fabsf(error) < 10.0f) {
            _motor_set(0); _mode = MotionMode::IDLE; _pos_pid.reset(); return;
        }
    } else if (_mode == MotionMode::HOMING) {
        if (_hall_fired) {
            _motor_set(0);
            _enc_turns = 0; _hall_revs = 0; _hall_fired = false;
            _enc_prev_raw = _enc_read_raw();
            _enc_abs_cdeg = (int32_t)(_enc_prev_raw * PAN_CDEG_PER_COUNT);
            _home_offset  = _enc_abs_cdeg;
            _homed = true; _mode = MotionMode::IDLE;
            Serial.println("[motion] homing complete");
        } else if (millis() - _home_start_ms > HOME_TIMEOUT_MS) {
            _motor_set(0); _fault = true; _mode = MotionMode::IDLE;
            Serial.println("[motion] homing TIMEOUT — fault");
        } else {
            _motor_set(HOME_DIRECTION > 0 ? HOME_DUTY : -HOME_DUTY);
        }
        return;
    } else { return; }

    _motor_set(duty);
}

void motion_set_velocity(int16_t vel_cdeg_s) {
    if (_fault) return;
    _cmd_vel = (int16_t)constrain((float)vel_cdeg_s, -_max_speed_cdeg_s, _max_speed_cdeg_s);
    _vel_pid.reset(); _motor_enable(); _enabled = true;
    _mode = (_cmd_vel != 0) ? MotionMode::VELOCITY : MotionMode::IDLE;
    if (_cmd_vel == 0) _motor_set(0);
}

void motion_set_position(int32_t pos_cdeg) {
    if (_fault) return;
    _target_cdeg = pos_cdeg; _pos_pid.reset();
    _motor_enable(); _enabled = true; _mode = MotionMode::POSITION;
}

void motion_stop() {
    _mode = MotionMode::IDLE; _cmd_vel = 0; _ramp_vel = 0.0f;
    _motor_set(0); _vel_pid.reset(); _pos_pid.reset();
}

void motion_estop() {
    _mode = MotionMode::IDLE; _cmd_vel = 0; _ramp_vel = 0.0f;
    _motor_brake(); _vel_pid.reset(); _pos_pid.reset();
}

void motion_home() {
    if (_fault) return;
    _vel_pid.reset(); _pos_pid.reset();
    _hall_fired = false; _home_start_ms = millis();
    _motor_enable(); _enabled = true; _mode = MotionMode::HOMING;
    Serial.println("[motion] homing sweep started");
}

bool motion_is_homing() { return _mode == MotionMode::HOMING; }

void motion_clear_can_fault() {
    if (_fault) { _fault = false; Serial.println("[motion] CAN reconnected — fault cleared"); }
}

void motion_set_settings(uint16_t max_speed_cdeg_s, uint16_t accel_cdeg_s2) {
    _max_speed_cdeg_s = (float)max_speed_cdeg_s;
    _accel_cdeg_s2    = (float)accel_cdeg_s2;
}

int32_t  motion_get_pos_cdeg()  { return _enc_abs_cdeg - _home_offset; }
int16_t  motion_get_vel_cdeg_s(){ return (int16_t)_vel_cdeg_s; }
uint16_t motion_get_enc_raw()   { return _enc_prev_raw; }

uint8_t motion_get_flags() {
    uint8_t f = 0;
    if (_mode != MotionMode::IDLE) f |= POS_FLAG_MOVING;
    if (_fault)                    f |= POS_FLAG_FAULT;
    if (_homed)                    f |= POS_FLAG_HOMED;
    return f;
}
