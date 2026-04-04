#pragma once

// =============================================================================
// SensorManager — Stationary and moving-side sensor suite.
//
// Stationary (I2C0, GPIO22/23):
//   INA226  — 12 V bus voltage, current, power (0x40, R100 shunt)
//   BMP280  — temperature, pressure, altitude   (0x76, SDO=GND)
//   GPS     — lat/lon/heading/speed/fix/sats    (SoftwareSerial GPIO16/13)
//
// Moving / gimbal head (I2C1, GPIO18/5, through slip ring):
//   MPU-6050 — roll, pitch from accelerometer   (0x68, AD0=GND)
//   QMC5883L — tilt-compensated compass heading (0x0D)
//
// Push protocol (Serial2 → Pi, 1 Hz):
//   $PWR ok=1,vin=12.45,curr=823.0,pwr=10234.0
//   $ENV temp=23.4,press=1013.2,alt=45.3
//   $GPS lat=47.123456,lon=-122.567890,fix=1,sats=8,hdg=180.5,spd=0.00
//   $IMU ok=1,roll=1.2,pitch=-0.5
//   $MAG ok=1,hdg=180.5
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

    // MPU-6050 IMU (moving side)
    float rollDeg  = 0.0f;
    float pitchDeg = 0.0f;
    bool  imuOk    = false;

    // QMC5883L compass (moving side, tilt-compensated)
    float magHdgDeg = 0.0f;
    bool  magOk     = false;
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
    void _readIMU();
    void _readMag();
    void _pushSlow();   // INA / BMP / GPS  — 1 Hz
    void _pushFast();   // IMU / MAG        — 20 Hz

    SensorData _data;
    bool     _inaOk         = false;
    bool     _bmpOk         = false;
    bool     _imuOk         = false;
    bool     _magOk         = false;
    uint32_t _lastSlowMs    = 0;   // INA / BMP / GPS push timer (1 Hz)
    uint32_t _lastImuPushMs = 0;   // IMU / MAG push timer (20 Hz)
};
