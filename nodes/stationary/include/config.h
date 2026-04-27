#pragma once

// =============================================================================
// Stationary Node — Hardware Configuration (ESP32 DevKit 30-pin)
//
// Peripheral list:
//   BTS7960 H-bridge — brushed DC pan motor
//   MT6816 SPI encoder — on pinion axle (25t), 4:1 gear ratio to pan shaft
//   Hall effect sensor — single magnet on pan shaft, revolution counter
//   I2C bus — QMC5883L compass, MPU-6050 IMU, BMP280 baro, INA226 power
//   GPS — UART RX/TX (ATGM336H, 115200 baud, 10Hz)
//   CAN — TWAI TX/RX (SN65HVD230 transceiver)
// =============================================================================

#define NODE_ID         NODE_STATIONARY

// ---------------------------------------------------------------------------
// CAN (TWAI) — SN65HVD230 transceiver
// NOTE: GPIO5 is a strapping pin — do not use for TWAI
// ---------------------------------------------------------------------------
#define CAN_TX_PIN      25
#define CAN_RX_PIN      32

// ---------------------------------------------------------------------------
// Pan motor — BTS7960 H-bridge (brushed DC gearmotor)
// Drivetrain: internal gearbox × 100t/25t external pinion = 4:1 at pan shaft
// R_IS / L_IS (current sense) not connected.
// ---------------------------------------------------------------------------
#define MOTOR_RPWM_PIN  27      // forward PWM  (LEDC channel 0)
#define MOTOR_LPWM_PIN  26      // reverse PWM  (LEDC channel 1)
#define MOTOR_REN_PIN   14      // right bridge enable (active high)
#define MOTOR_LEN_PIN   17      // left  bridge enable (active high)

#define MOTOR_PWM_FREQ  10000   // Hz — above audible range
#define MOTOR_PWM_BITS  8       // 0–255 resolution

// ---------------------------------------------------------------------------
// MT6816 SPI encoder — read-only (no MOSI needed)
// Mounted on pinion axle; reads absolute angle of pinion (0–16383 per rev).
// Pan shaft angle = pinion_angle / ENCODER_GEAR_RATIO
// Hardware SPI (VSPI)
// ---------------------------------------------------------------------------
#define ENC_MOSI_PIN    23
#define ENC_MISO_PIN    19
#define ENC_SCK_PIN     18
#define ENC_CS_PIN      15

// Pinion:pan gear ratio — pinion turns this many times per pan revolution
#define ENCODER_GEAR_RATIO  4.0f    // 100t ring / 25t pinion

// ---------------------------------------------------------------------------
// Hall effect sensor — revolution counter
// Single magnet on rotating pan shaft collar, single sensor on stationary base.
// Open-collector type (e.g. A3144): requires 10kΩ pullup to 3.3V.
// Push-pull type (e.g. SS49E): connect directly, no pullup needed.
// Direction is inferred from motor command, not hall sequence.
// ---------------------------------------------------------------------------
#define HALL_PIN        33      // interrupt-capable GPIO, use INPUT_PULLUP

// ---------------------------------------------------------------------------
// I2C bus — shared by compass, IMU, baro, power monitor
// ---------------------------------------------------------------------------
#define I2C_SDA_PIN     21
#define I2C_SCL_PIN     22
#define I2C_FREQ        400000  // 400 kHz fast mode

// ---------------------------------------------------------------------------
// GPS (SERIAL1)
// ---------------------------------------------------------------------------
#define GPS_RX_PIN      13  // ESP32 RX ← GPS TX
#define GPS_TX_PIN      12  // ESP32 TX → GPS RX
#define GPS_BAUD        115200

// ---------------------------------------------------------------------------
// Sensor I2C addresses (informational)
// ---------------------------------------------------------------------------
// QMC5883L compass : 0x0D  (clone chip — NOT HMC5883L at 0x1E)
// MPU-6050 IMU     : 0x68  — vehicle/platform IMU, reports over CAN
// BMP280 baro      : 0x76
// INA226 power     : 0x40
