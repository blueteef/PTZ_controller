#pragma once

// =============================================================================
// SensorManager — Stationary-side sensor suite.
//
// Sensors:
//   INA226  — 12 V bus voltage, current, power (I2C0 0x40, shunt R100 = 0.1Ω)
//   BMP280  — temperature, pressure, altitude   (I2C0 0x76, SDO=GND)
//   GPS     — lat/lon/heading/speed/fix/sats    (Serial1, 9600 baud)
//
// Operation:
//   sensorTask() runs at ~20 Hz, feeds GPS bytes continuously, reads INA/BMP
//   at ~1 Hz, and pushes $-prefixed telemetry lines on Serial2 every
//   SENSOR_PUSH_MS milliseconds.
//
// Pi parses the pushed lines:
//   $PWR vin=12.45,curr=823.0,pwr=10234.0
//   $ENV temp=23.4,press=1013.2,alt=45.3
//   $GPS lat=47.123456,lon=-122.567890,fix=1,sats=8,hdg=180.5,spd=0.00
// =============================================================================

#include <Arduino.h>

struct SensorData {
    // INA226
    float busVoltageV = 0.0f;
    float currentMA   = 0.0f;
    float powerMW     = 0.0f;
    bool  inaOk       = false;

    // BMP280
    float tempC    = 0.0f;
    float pressHPa = 0.0f;
    float altM     = 0.0f;
    bool  bmpOk    = false;

    // GPS
    bool    gpsFix      = false;
    uint8_t gpsSats     = 0;
    double  gpsLat      = 0.0;
    double  gpsLon      = 0.0;
    float   gpsHdgDeg   = 0.0f;
    float   gpsSpdKnots = 0.0f;
};

class SensorManager {
public:
    bool begin();

    SensorData getData() const { return _data; }

    static void sensorTask(void* param);

private:
    void _readINA();
    void _readBMP();
    void _feedGPS();
    void _pushTelemetry();

    SensorData _data;
    bool     _inaOk      = false;
    bool     _bmpOk      = false;
    uint32_t _lastPushMs = 0;
    uint32_t _lastReadMs = 0;
};
