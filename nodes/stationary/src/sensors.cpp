#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BMP280.h>
#include <INA226_WE.h>
#include "config.h"
#include "sensors.h"

// ---------------------------------------------------------------------------
// MPU-6050 — raw I2C, no library dependency
// ---------------------------------------------------------------------------
#define MPU_ADDR        0x68
#define MPU_PWR_MGMT_1  0x6B
#define MPU_ACCEL_XOUT  0x3B
#define MPU_GYRO_XOUT   0x43

static bool _mpu_ok = false;

static void _mpu_write(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

static bool _mpu_read_raw(int16_t &ax, int16_t &ay, int16_t &az,
                           int16_t &gx, int16_t &gy, int16_t &gz) {
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(MPU_ACCEL_XOUT);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)14) != 14) return false;

    ax = (Wire.read() << 8) | Wire.read();
    ay = (Wire.read() << 8) | Wire.read();
    az = (Wire.read() << 8) | Wire.read();
    Wire.read(); Wire.read();   // skip temp
    gx = (Wire.read() << 8) | Wire.read();
    gy = (Wire.read() << 8) | Wire.read();
    gz = (Wire.read() << 8) | Wire.read();
    return true;
}

// Simple complementary filter state
static float _roll_deg  = 0.0f;
static float _pitch_deg = 0.0f;
static uint32_t _mpu_last_us = 0;
static constexpr float ALPHA = 0.96f;   // gyro weight

static void _mpu_update() {
    int16_t ax, ay, az, gx, gy, gz;
    if (!_mpu_read_raw(ax, ay, az, gx, gy, gz)) return;

    uint32_t now = micros();
    float dt = (now - _mpu_last_us) * 1e-6f;
    _mpu_last_us = now;
    if (dt > 0.5f || dt < 0.0f) return;   // skip stale first tick

    // Accel angles
    float acc_roll  = atan2f(ay, az) * 57.295779f;
    float acc_pitch = atan2f(-ax, sqrtf(ay*ay + az*az)) * 57.295779f;

    // Gyro rates (250 dps range → 131 LSB/dps)
    float gr_x = gx / 131.0f;
    float gr_y = gy / 131.0f;

    _roll_deg  = ALPHA * (_roll_deg  + gr_x * dt) + (1.0f - ALPHA) * acc_roll;
    _pitch_deg = ALPHA * (_pitch_deg + gr_y * dt) + (1.0f - ALPHA) * acc_pitch;
}

// ---------------------------------------------------------------------------
// HMC5883L — raw I2C
// ---------------------------------------------------------------------------
#define MAG_ADDR        0x1E
#define MAG_CONFIG_A    0x00
#define MAG_CONFIG_B    0x01
#define MAG_MODE        0x02
#define MAG_DATA_XH     0x03

static bool _mag_ok = false;

static bool _mag_init() {
    Wire.beginTransmission(MAG_ADDR);
    Wire.write(MAG_CONFIG_A); Wire.write(0x70);  // 8 samples, 15Hz
    Wire.write(0x20);                             // config B: ±1.3G
    Wire.write(0x00);                             // continuous mode
    return Wire.endTransmission() == 0;
}

static bool _mag_read(float &hdg_deg) {
    Wire.beginTransmission(MAG_ADDR);
    Wire.write(MAG_DATA_XH);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)MAG_ADDR, (uint8_t)6) != 6) return false;

    int16_t x = (Wire.read() << 8) | Wire.read();
    int16_t z = (Wire.read() << 8) | Wire.read();
    int16_t y = (Wire.read() << 8) | Wire.read();

    hdg_deg = atan2f((float)y, (float)x) * 57.295779f;
    if (hdg_deg < 0) hdg_deg += 360.0f;
    return true;
}

// ---------------------------------------------------------------------------
// BMP280
// ---------------------------------------------------------------------------
static Adafruit_BMP280 _bmp;
static bool _bmp_ok = false;

// ---------------------------------------------------------------------------
// INA226
// ---------------------------------------------------------------------------
static INA226_WE _ina(0x40);
static bool _ina_ok = false;

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------

void sensors_init() {
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, I2C_FREQ);

    // MPU-6050
    _mpu_write(MPU_PWR_MGMT_1, 0x00);   // wake up
    delay(100);
    Wire.beginTransmission(MPU_ADDR);
    _mpu_ok = (Wire.endTransmission() == 0);
    if (_mpu_ok) {
        _mpu_last_us = micros();
        Serial.println("[sensors] MPU-6050 OK");
    } else {
        Serial.println("[sensors] MPU-6050 NOT FOUND");
    }

    // HMC5883L
    _mag_ok = _mag_init();
    Serial.printf("[sensors] HMC5883L %s\n", _mag_ok ? "OK" : "NOT FOUND");

    // BMP280
    _bmp_ok = _bmp.begin(0x76);
    if (_bmp_ok) {
        _bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                         Adafruit_BMP280::SAMPLING_X2,
                         Adafruit_BMP280::SAMPLING_X16,
                         Adafruit_BMP280::FILTER_X4,
                         Adafruit_BMP280::STANDBY_MS_500);
        Serial.println("[sensors] BMP280 OK");
    } else {
        Serial.println("[sensors] BMP280 NOT FOUND");
    }

    // INA226
    _ina_ok = _ina.init();
    if (_ina_ok) {
        _ina.setResistorRange(0.1f, 1.3f);   // 0.1Ω shunt, 1.3A max
        Serial.println("[sensors] INA226 OK");
    } else {
        Serial.println("[sensors] INA226 NOT FOUND");
    }
}

bool sensors_get_imu(int16_t &roll_cdeg, int16_t &pitch_cdeg, int16_t &yaw_cdeg) {
    if (!_mpu_ok) return false;
    _mpu_update();
    roll_cdeg  = (int16_t)(_roll_deg  * 100.0f);
    pitch_cdeg = (int16_t)(_pitch_deg * 100.0f);
    yaw_cdeg   = 0;   // gyro-only yaw drifts — use compass for heading
    return true;
}

bool sensors_get_mag(int16_t &hdg_cdeg) {
    if (!_mag_ok) return false;
    float hdg;
    if (!_mag_read(hdg)) return false;
    hdg_cdeg = (int16_t)(hdg * 100.0f);
    return true;
}

bool sensors_get_env(int16_t &temp_cdeg, uint16_t &press_hPa) {
    if (!_bmp_ok) return false;
    temp_cdeg  = (int16_t)(_bmp.readTemperature() * 100.0f);
    press_hPa  = (uint16_t)(_bmp.readPressure() / 100.0f);
    return true;
}

bool sensors_get_power(uint16_t &voltage_mv, int16_t &current_ma) {
    if (!_ina_ok) return false;
    voltage_mv = (uint16_t)(_ina.getBusVoltage_V() * 1000.0f);
    current_ma = (int16_t)(_ina.getCurrent_mA());
    return true;
}
