// =============================================================================
// SensorManager.cpp
// =============================================================================

#include "SensorManager.h"
#include <Wire.h>
#include <SoftwareSerial.h>
#include <INA226_WE.h>
#include <Adafruit_BMP280.h>
#include <TinyGPSPlus.h>
#include <MPU6050.h>
#include <math.h>
#include "config.h"

// I2C0 — stationary side
static INA226_WE       _ina(INA226_I2C_ADDR);
static Adafruit_BMP280 _bmp;

// I2C1 — moving side (through slip ring)
static MPU6050         _mpu(MPU6050_I2C_ADDR, &Wire1);

// GPS on SoftwareSerial — HardwareSerial 1 reserved for TMC2209 UART
static TinyGPSPlus     _gps;
static SoftwareSerial  _gpsSerial;

// -----------------------------------------------------------------------------
// Public
// -----------------------------------------------------------------------------

bool SensorManager::begin() {
    // I2C bus 0 — shared by INA226 and BMP280
    Wire.begin(I2C0_SDA_PIN, I2C0_SCL_PIN);

    // INA226
    _inaOk = _ina.init();
    if (_inaOk) {
        _ina.setResistorRange(INA226_SHUNT_OHM, INA226_MAX_A);
        Serial.printf("[SENS] INA226 OK — shunt %.3f Ω, max %.1f A\r\n",
                      INA226_SHUNT_OHM, INA226_MAX_A);
    } else {
        Serial.println("[SENS] INA226 not found (check I2C wiring/address)");
    }
    _data.inaOk = _inaOk;

    // BMP280
    _bmpOk = _bmp.begin(BMP280_I2C_ADDR);
    if (_bmpOk) {
        // Normal mode: 2x temp / 16x pressure oversampling, 16x IIR filter, 500 ms standby
        _bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                         Adafruit_BMP280::SAMPLING_X2,
                         Adafruit_BMP280::SAMPLING_X16,
                         Adafruit_BMP280::FILTER_X16,
                         Adafruit_BMP280::STANDBY_MS_500);
        Serial.printf("[SENS] BMP280 OK (0x%02X)\r\n", BMP280_I2C_ADDR);
    } else {
        Serial.println("[SENS] BMP280 not found (check SDO pin, try addr 0x77)");
    }
    _data.bmpOk = _bmpOk;

    // GPS on SoftwareSerial — HardwareSerial 1 (Serial1) is reserved for TMC2209 UART
    _gpsSerial.begin(GPS_BAUD_RATE, SWSERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    Serial.printf("[SENS] GPS SoftwareSerial started — GPIO%d RX, GPIO%d TX, %d baud\r\n",
                  GPS_RX_PIN, GPS_TX_PIN, GPS_BAUD_RATE);

    // I2C bus 1 — moving side (MPU-6050 IMU + QMC5883L compass through slip ring)
    Wire1.begin(I2C1_SDA_PIN, I2C1_SCL_PIN);

    // MPU-6050
    _mpu.initialize();
    _imuOk = _mpu.testConnection();
    if (_imuOk) {
        _mpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_2);  // ±2g — 16384 LSB/g
        Serial.printf("[SENS] MPU-6050 OK (0x%02X)\r\n", MPU6050_I2C_ADDR);
    } else {
        Serial.println("[SENS] MPU-6050 not found (check I2C1 wiring)");
    }
    _data.imuOk = _imuOk;

    // QMC5883L — direct Wire1 register access (no library; fixed I2C address 0x0D)
    Wire1.beginTransmission(QMC5883L_I2C_ADDR);
    Wire1.write(0x0B);  // SET/RESET period register — must be 0x01 per datasheet
    Wire1.write(0x01);
    _magOk = (Wire1.endTransmission() == 0);
    if (_magOk) {
        Wire1.beginTransmission(QMC5883L_I2C_ADDR);
        Wire1.write(0x09);  // Control Register 1
        Wire1.write(0x1D);  // Continuous mode | 200 Hz | 8G range | 512 OSR
        Wire1.endTransmission();
        Serial.printf("[SENS] QMC5883L OK (0x%02X)\r\n", QMC5883L_I2C_ADDR);
    } else {
        Serial.println("[SENS] QMC5883L not found (check I2C1 wiring)");
    }
    _data.magOk = _magOk;

    return true;
}

// -----------------------------------------------------------------------------
// Private — called from sensorTask
// -----------------------------------------------------------------------------

void SensorManager::_readINA() {
    if (!_inaOk) return;
    _ina.readAndClearFlags();   // required before reading — clears conversion-ready flag
    _data.busVoltageV = _ina.getBusVoltage_V();
    _data.currentMA   = _ina.getCurrent_mA();
    _data.powerMW     = _ina.getBusPower() * 1000.0f;  // getBusPower() returns W → mW
}

void SensorManager::_readBMP() {
    if (!_bmpOk) return;
    _data.tempC    = _bmp.readTemperature();
    _data.pressHPa = _bmp.readPressure() / 100.0f;
    _data.altM     = _bmp.readAltitude(1013.25f);  // standard sea-level reference
}

void SensorManager::_feedGPS() {
    while (_gpsSerial.available()) {
        _gps.encode(_gpsSerial.read());
    }
    _data.gpsFix  = _gps.location.isValid();
    _data.gpsSats = _gps.satellites.isValid() ? (uint8_t)_gps.satellites.value() : 0;
    if (_gps.location.isValid()) {
        _data.gpsLat = _gps.location.lat();
        _data.gpsLon = _gps.location.lng();
    }
    _data.gpsHdgDeg   = _gps.course.isValid() ? (float)_gps.course.deg()  : 0.0f;
    _data.gpsSpdKnots = _gps.speed.isValid()  ? (float)_gps.speed.knots() : 0.0f;
}

void SensorManager::_readIMU() {
    if (!_imuOk) return;

    uint32_t now = millis();
    _imuDtS = (_lastImuMs == 0) ? 0.0f : (now - _lastImuMs) / 1000.0f;
    _lastImuMs = now;

    int16_t ax, ay, az, gx, gy, gz;
    _mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

    // ±2g range → 16384 LSB/g
    float axg = ax / 16384.0f;
    float ayg = ay / 16384.0f;
    float azg = az / 16384.0f;

    // ±250°/s range → 131 LSB/°/s
    float gxDps = gx / 131.0f;
    float gyDps = gy / 131.0f;
    _gzDps      = gz / 131.0f;   // shared with _readMag for yaw complementary filter

    float accelRoll  = atan2f(ayg, azg)                        * 180.0f / (float)M_PI;
    float accelPitch = atan2f(-axg, sqrtf(ayg*ayg + azg*azg)) * 180.0f / (float)M_PI;

    if (_imuDtS > 0.0f && _imuDtS < 0.5f) {
        // Complementary filter: trust gyro for fast motion, anchor to accel long-term
        _data.rollDeg  = IMU_COMP_ALPHA * (_data.rollDeg  + gxDps * _imuDtS)
                       + (1.0f - IMU_COMP_ALPHA) * accelRoll;
        _data.pitchDeg = IMU_COMP_ALPHA * (_data.pitchDeg + gyDps * _imuDtS)
                       + (1.0f - IMU_COMP_ALPHA) * accelPitch;
    } else {
        // First call or stale reading — seed directly from accelerometer
        _data.rollDeg  = accelRoll;
        _data.pitchDeg = accelPitch;
    }
}

void SensorManager::_readMag() {
    if (!_magOk) return;
    Wire1.beginTransmission(QMC5883L_I2C_ADDR);
    Wire1.write(0x00);  // data registers start at 0x00
    if (Wire1.endTransmission(false) != 0) return;
    Wire1.requestFrom((uint8_t)QMC5883L_I2C_ADDR, (uint8_t)6);
    if (Wire1.available() < 6) return;
    int16_t mx = (int16_t)(Wire1.read() | (Wire1.read() << 8));
    int16_t my = (int16_t)(Wire1.read() | (Wire1.read() << 8));
    int16_t mz = (int16_t)(Wire1.read() | (Wire1.read() << 8));

    // Tilt-compensate using IMU roll/pitch for accurate heading when camera is angled
    float rollR  = _data.rollDeg  * (float)M_PI / 180.0f;
    float pitchR = _data.pitchDeg * (float)M_PI / 180.0f;
    float mxc =  mx * cosf(pitchR) + mz * sinf(pitchR);
    float myc =  mx * sinf(rollR) * sinf(pitchR)
               + my * cosf(rollR)
               - mz * sinf(rollR) * cosf(pitchR);
    float hdg = atan2f(-myc, mxc) * 180.0f / (float)M_PI;
    if (hdg < 0.0f) hdg += 360.0f;

    if (_imuDtS > 0.0f && _imuDtS < 0.5f) {
        // Complementary filter for yaw: gyro integrates fast transients,
        // compass corrects long-term drift.  Handle 0/360 wrap by blending
        // on the shortest angular path.
        float gyroYaw = _data.magHdgDeg + _gzDps * _imuDtS;
        if (gyroYaw <   0.0f) gyroYaw += 360.0f;
        if (gyroYaw >= 360.0f) gyroYaw -= 360.0f;

        float diff = hdg - gyroYaw;
        if (diff >  180.0f) diff -= 360.0f;
        if (diff < -180.0f) diff += 360.0f;

        hdg = gyroYaw + (1.0f - IMU_COMP_ALPHA) * diff;
        if (hdg <   0.0f) hdg += 360.0f;
        if (hdg >= 360.0f) hdg -= 360.0f;
    }

    _data.magHdgDeg = hdg;
}

void SensorManager::_pushSlow() {
    Serial2.printf("$PWR ok=%d,vin=%.3f,curr=%.1f,pwr=%.1f\r\n",
                   _data.inaOk ? 1 : 0,
                   _data.busVoltageV, _data.currentMA, _data.powerMW);
    if (_data.bmpOk) {
        Serial2.printf("$ENV temp=%.2f,press=%.2f,alt=%.1f\r\n",
                       _data.tempC, _data.pressHPa, _data.altM);
    }
    Serial2.printf("$GPS lat=%.6f,lon=%.6f,fix=%d,sats=%d,hdg=%.1f,spd=%.2f\r\n",
                   _data.gpsLat, _data.gpsLon,
                   _data.gpsFix ? 1 : 0,
                   _data.gpsSats,
                   _data.gpsHdgDeg,
                   _data.gpsSpdKnots);
}

void SensorManager::_pushFast() {
    Serial2.printf("$IMU ok=%d,roll=%.2f,pitch=%.2f\r\n",
                   _data.imuOk ? 1 : 0,
                   _data.rollDeg, _data.pitchDeg);
    Serial2.printf("$MAG ok=%d,hdg=%.1f\r\n",
                   _data.magOk ? 1 : 0,
                   _data.magHdgDeg);
}

// -----------------------------------------------------------------------------
// FreeRTOS task
// -----------------------------------------------------------------------------

void SensorManager::sensorTask(void* param) {
    SensorManager* sm = static_cast<SensorManager*>(param);

    for (;;) {
        uint32_t now = millis();

        // IMU + compass: read and push at 20 Hz
        if (now - sm->_lastImuPushMs >= SENSOR_IMU_PUSH_MS) {
            sm->_lastImuPushMs = now;
            sm->_readIMU();
            sm->_readMag();
            sm->_pushFast();
        }

        // GPS feed: run every task tick for fast fix acquisition
        sm->_feedGPS();

        // Slow sensors: INA226 / BMP280 / GPS telemetry at 1 Hz
        if (now - sm->_lastSlowMs >= SENSOR_PUSH_MS) {
            sm->_lastSlowMs = now;
            sm->_readINA();
            sm->_readBMP();
            sm->_pushSlow();
        }

        vTaskDelay(pdMS_TO_TICKS(10));  // 100 Hz task rate, handles 20 Hz IMU timing
    }
}
