#pragma once

// =============================================================================
// Stationary Node — Hardware Configuration (ESP32 DevKit 30-pin)
//
// Peripheral list:
//   TMC2209 stepper — STEP/DIR/EN + UART
//   SPI magnetic encoder (e.g. AS5047P) — MOSI/MISO/SCK/CS
//   I2C bus — HMC5883L compass, MPU-6050 IMU, BMP280 baro, INA226 power
//   GPS — UART RX (TX optional)
//   CAN — TWAI TX/RX (SN65HVD230 transceiver)
// =============================================================================

#define NODE_ID         NODE_STATIONARY

// ---------------------------------------------------------------------------
// CAN (TWAI) — SN65HVD230 transceiver
// ---------------------------------------------------------------------------
#define CAN_TX_PIN      5
#define CAN_RX_PIN      4

// ---------------------------------------------------------------------------
// Stepper — TMC2209 (single axis)
// ---------------------------------------------------------------------------
#define STEP_PIN        27
#define DIR_PIN         26
#define EN_PIN          14

// TMC2209 single-wire UART (SERIAL2 on ESP32)
#define TMC_UART_TX     17
#define TMC_UART_RX     16
#define TMC_UART_BAUD   115200
#define TMC_ADDR        0   // MS1=GND → address 0

// ---------------------------------------------------------------------------
// SPI magnetic encoder (AS5047P or compatible)
// Hardware SPI bus (VSPI)
// ---------------------------------------------------------------------------
#define ENC_MOSI_PIN    23
#define ENC_MISO_PIN    19
#define ENC_SCK_PIN     18
#define ENC_CS_PIN      15

// ---------------------------------------------------------------------------
// I2C bus — shared by compass, IMU, baro, power monitor
// (Wire, hardware I2C0)
// ---------------------------------------------------------------------------
#define I2C_SDA_PIN     21
#define I2C_SCL_PIN     22
#define I2C_FREQ        400000  // 400 kHz fast mode

// ---------------------------------------------------------------------------
// GPS (SERIAL1)
// ---------------------------------------------------------------------------
#define GPS_RX_PIN      13  // ESP32 RX ← GPS TX
#define GPS_TX_PIN      12  // ESP32 TX → GPS RX (optional)
#define GPS_BAUD        9600

// ---------------------------------------------------------------------------
// Sensor I2C addresses (informational)
// ---------------------------------------------------------------------------
// HMC5883L compass : 0x1E
// MPU-6050 IMU     : 0x68
// BMP280 baro      : 0x76
// INA226 power     : 0x40
