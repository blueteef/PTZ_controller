// =============================================================================
// SensorManager.cpp
// =============================================================================

#include "SensorManager.h"
#include <Wire.h>
#include <INA226_WE.h>
#include <Adafruit_BMP280.h>
#include <TinyGPSPlus.h>
#include "config.h"

static INA226_WE       _ina(INA226_I2C_ADDR);
static Adafruit_BMP280 _bmp;   // uses default Wire; begin() specifies addr
static TinyGPSPlus     _gps;

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

    // GPS on Serial1 (HardwareSerial 1 — freed because TMCDriver now uses SoftwareSerial)
    Serial1.begin(GPS_BAUD_RATE, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    Serial.printf("[SENS] GPS UART started — GPIO%d RX, GPIO%d TX, %d baud\r\n",
                  GPS_RX_PIN, GPS_TX_PIN, GPS_BAUD_RATE);

    return true;  // always true — sensor failures are non-fatal and logged above
}

// -----------------------------------------------------------------------------
// Private — called from sensorTask
// -----------------------------------------------------------------------------

void SensorManager::_readINA() {
    if (!_inaOk) return;
    _data.busVoltageV = _ina.getBusVoltage_V();
    _data.currentMA   = _ina.getCurrent_mA();
    _data.powerMW     = _data.busVoltageV * _data.currentMA;  // V * mA = mW
}

void SensorManager::_readBMP() {
    if (!_bmpOk) return;
    _data.tempC    = _bmp.readTemperature();
    _data.pressHPa = _bmp.readPressure() / 100.0f;
    _data.altM     = _bmp.readAltitude(1013.25f);  // standard sea-level reference
}

void SensorManager::_feedGPS() {
    while (Serial1.available()) {
        _gps.encode(Serial1.read());
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

void SensorManager::_pushTelemetry() {
    if (_data.inaOk) {
        Serial2.printf("$PWR vin=%.3f,curr=%.1f,pwr=%.1f\r\n",
                       _data.busVoltageV, _data.currentMA, _data.powerMW);
    }
    if (_data.bmpOk) {
        Serial2.printf("$ENV temp=%.2f,press=%.2f,alt=%.1f\r\n",
                       _data.tempC, _data.pressHPa, _data.altM);
    }
    // GPS pushed unconditionally — Pi uses fix=0 to ignore position fields
    Serial2.printf("$GPS lat=%.6f,lon=%.6f,fix=%d,sats=%d,hdg=%.1f,spd=%.2f\r\n",
                   _data.gpsLat, _data.gpsLon,
                   _data.gpsFix ? 1 : 0,
                   _data.gpsSats,
                   _data.gpsHdgDeg,
                   _data.gpsSpdKnots);
}

// -----------------------------------------------------------------------------
// FreeRTOS task
// -----------------------------------------------------------------------------

void SensorManager::sensorTask(void* param) {
    SensorManager* sm = static_cast<SensorManager*>(param);

    for (;;) {
        // Feed GPS at task rate (~20 Hz) for responsive fix acquisition
        sm->_feedGPS();

        // Read slow sensors and push telemetry at 1 Hz
        uint32_t now = millis();
        if (now - sm->_lastReadMs >= SENSOR_PUSH_MS) {
            sm->_lastReadMs = now;
            sm->_readINA();
            sm->_readBMP();
            sm->_pushTelemetry();
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
