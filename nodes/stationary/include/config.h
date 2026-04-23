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
// Pan motor — BTS7960 H-bridge (brushed DC gearmotor)
// Drivetrain: 56:1 internal gearbox × 100t/16t external = 350:1 total
// Encoder on OUTPUT shaft — position is direct, no gear ratio needed in math.
// R_IS / L_IS (current sense) not connected.
// ---------------------------------------------------------------------------
#define MOTOR_RPWM_PIN  27      // forward PWM
#define MOTOR_LPWM_PIN  26      // reverse PWM
#define MOTOR_REN_PIN   14      // right bridge enable (active high)
#define MOTOR_LEN_PIN   17      // left  bridge enable (active high)

#define MOTOR_PWM_FREQ  10000   // Hz — above audible range
#define MOTOR_PWM_BITS  8       // 0–255 resolution

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
// Main power relay
// High-active: HIGH = relay energised (power ON), LOW = relay off
// ---------------------------------------------------------------------------
#define RELAY_PIN       25

// ---------------------------------------------------------------------------
// Sensor I2C addresses (informational)
// ---------------------------------------------------------------------------
// HMC5883L compass : 0x1E
// MPU-6050 IMU     : 0x68  — VEHICLE/PLATFORM stabilization
//                            Reports vehicle attitude to CAN as MSG_SENSOR_IMU.
//                            Used to correct for platform motion (vehicle driving, vibration).
//                            A second MPU-6050 lives on the Pi I2C bus at the tilt end-effector
//                            for fine camera stabilization — these are two separate sensors.
// BMP280 baro      : 0x76
// INA226 power     : 0x40
