#include <Arduino.h>
#include <Wire.h>
#include <driver/twai.h>
#include "can_ids.h"
#include "can_frames.h"
#include "config.h"
#include "motion.h"

// ---------------------------------------------------------------------------
// Telemetry intervals (ms)
// ---------------------------------------------------------------------------
#define INTERVAL_POS_REPORT     50      // 20 Hz
#define INTERVAL_IMU            20      // 50 Hz
#define INTERVAL_HEARTBEAT      1000    // 1 Hz
#define CAN_TIMEOUT_MS          15000

// ---------------------------------------------------------------------------
// IMU — MPU-6050, raw I2C
// ---------------------------------------------------------------------------
#define MPU_ADDR        0x68
#define MPU_PWR_MGMT_1  0x6B
#define MPU_ACCEL_XOUT  0x3B

static bool  _mpu_ok = false;
static float _roll_deg = 0.0f, _pitch_deg = 0.0f;
static uint32_t _mpu_last_us = 0;
static constexpr float ALPHA = 0.96f;

static void _mpu_write(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(reg); Wire.write(val);
    Wire.endTransmission();
}

static void _mpu_update() {
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(MPU_ACCEL_XOUT);
    if (Wire.endTransmission(false) != 0) return;
    if (Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)14) != 14) return;

    int16_t ax = (Wire.read()<<8)|Wire.read();
    int16_t ay = (Wire.read()<<8)|Wire.read();
    int16_t az = (Wire.read()<<8)|Wire.read();
    Wire.read(); Wire.read();
    int16_t gx = (Wire.read()<<8)|Wire.read();
    int16_t gy = (Wire.read()<<8)|Wire.read();
    Wire.read(); Wire.read();

    uint32_t now = micros();
    float dt = (now - _mpu_last_us) * 1e-6f;
    _mpu_last_us = now;
    if (dt > 0.5f || dt < 0.0f) return;

    float acc_roll  = atan2f(ay, az) * 57.295779f;
    float acc_pitch = atan2f(-ax, sqrtf(ay*ay + az*az)) * 57.295779f;
    _roll_deg  = ALPHA * (_roll_deg  + (gx/131.0f)*dt) + (1.0f-ALPHA)*acc_roll;
    _pitch_deg = ALPHA * (_pitch_deg + (gy/131.0f)*dt) + (1.0f-ALPHA)*acc_pitch;
}

// ---------------------------------------------------------------------------
// TWAI (CAN)
// ---------------------------------------------------------------------------
static void can_init() {
    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(
        (gpio_num_t)CAN_TX_PIN, (gpio_num_t)CAN_RX_PIN, TWAI_MODE_NORMAL);
    twai_timing_config_t  t = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t  f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    ESP_ERROR_CHECK(twai_driver_install(&g, &t, &f));
    ESP_ERROR_CHECK(twai_start());
    Serial.println("[CAN] TWAI started at 500kbps");
}

static void can_send(uint32_t arb_id, const void *data, uint8_t len) {
    twai_message_t msg = {};
    msg.identifier       = arb_id;
    msg.data_length_code = len;
    msg.extd             = 0;
    if (len && data) memcpy(msg.data, data, len);
    twai_transmit(&msg, pdMS_TO_TICKS(5));
}

// ---------------------------------------------------------------------------
// Telemetry
// ---------------------------------------------------------------------------
static void send_heartbeat() {
    can_send(CAN_ID(NODE_TILT, MSG_HEARTBEAT), nullptr, 0);
}

static void send_pos_report() {
    FramePosReport f;
    f.axis       = AXIS_TILT;
    f.pos_cdeg   = motion_get_pos_cdeg();
    f.vel_cdeg_s = motion_get_vel_cdeg_s();
    f.flags      = motion_get_flags();
    can_send(CAN_ID(NODE_TILT, MSG_POS_REPORT), &f, sizeof(f));
}

static void send_imu() {
    if (!_mpu_ok) return;
    _mpu_update();
    FrameImu f;
    f.roll_cdeg  = (int16_t)(_roll_deg  * 100.0f);
    f.pitch_cdeg = (int16_t)(_pitch_deg * 100.0f);
    f.yaw_cdeg   = 0;
    can_send(CAN_ID(NODE_TILT, MSG_SENSOR_IMU), &f, sizeof(f));
}

static void send_fault(uint8_t code) {
    FrameFault f = { NODE_TILT, code };
    can_send(CAN_ID(NODE_TILT, MSG_FAULT), &f, sizeof(f));
}

// ---------------------------------------------------------------------------
// CAN RX
// ---------------------------------------------------------------------------
static uint32_t _last_pi_heartbeat_ms = 0;

static void handle_can_rx() {
    twai_message_t msg;
    while (twai_receive(&msg, 0) == ESP_OK) {
        if (CAN_ID_NODE(msg.identifier) != NODE_PI) continue;
        uint8_t type = CAN_ID_MSG_TYPE(msg.identifier);
        switch (type) {
            case MSG_HEARTBEAT:
                _last_pi_heartbeat_ms = millis();
                motion_clear_can_fault();
                break;
            case MSG_ESTOP:
                motion_estop();
                break;
            case MSG_STOP: {
                FrameStop *f = (FrameStop*)msg.data;
                if (f->axis == AXIS_TILT || f->axis == AXIS_ALL) motion_stop();
                break;
            }
            case MSG_VEL_CMD: {
                FrameVelCmd *f = (FrameVelCmd*)msg.data;
                if (f->axis == AXIS_TILT) motion_set_velocity(f->vel_cdeg_s);
                break;
            }
            case MSG_POS_CMD: {
                FramePosCmd *f = (FramePosCmd*)msg.data;
                if (f->axis == AXIS_TILT) motion_set_position(f->pos_cdeg);
                break;
            }
            case MSG_HOME_CMD: {
                FrameHomeCmd *f = (FrameHomeCmd*)msg.data;
                if (f->axis == AXIS_TILT || f->axis == AXIS_ALL) motion_home();
                break;
            }
            case MSG_SETTINGS: {
                FrameSettings *f = (FrameSettings*)msg.data;
                motion_set_settings(f->max_speed_cdeg_s, f->accel_cdeg_s2);
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Setup / loop
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    Serial.println("[tilt] booting...");

    can_init();
    motion_init();

    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, I2C_FREQ);
    _mpu_write(MPU_PWR_MGMT_1, 0x00);
    delay(100);
    Wire.beginTransmission(MPU_ADDR);
    _mpu_ok = (Wire.endTransmission() == 0);
    if (_mpu_ok) {
        _mpu_last_us = micros();
        Serial.println("[sensors] MPU-6050 OK");
    } else {
        Serial.println("[sensors] MPU-6050 NOT FOUND");
    }

    _last_pi_heartbeat_ms = millis();
    Serial.println("[tilt] ready");
}

void loop() {
    uint32_t now = millis();

    handle_can_rx();

    uint32_t hb_age = millis() - _last_pi_heartbeat_ms;
    if (hb_age > CAN_TIMEOUT_MS) {
        motion_estop();
        send_fault(FAULT_CAN_TIMEOUT);
        _last_pi_heartbeat_ms = millis();
    }

    motion_tick();

    static uint32_t t_pos=0, t_imu=0, t_hb=0;
    if (now - t_pos >= INTERVAL_POS_REPORT) { t_pos=now; send_pos_report(); }
    if (now - t_imu >= INTERVAL_IMU)        { t_imu=now; send_imu(); }
    if (now - t_hb  >= INTERVAL_HEARTBEAT)  { t_hb=now;  send_heartbeat(); }
}
