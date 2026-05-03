#pragma once

// =============================================================================
// Tilt Node — Hardware Configuration (ESP32 DevKit 30-pin)
// Mirrors stationary node pinout — same motor/encoder/hall/I2C pins.
// No GPS. Only MPU-6050 on I2C.
// =============================================================================

#define NODE_ID         NODE_TILT

// ---------------------------------------------------------------------------
// CAN (TWAI) — SN65HVD230 transceiver
// NOTE: GPIO5 is a strapping pin — do not use for TWAI
// ---------------------------------------------------------------------------
#define CAN_TX_PIN      25
#define CAN_RX_PIN      32

// ---------------------------------------------------------------------------
// Tilt motor — BTS7960 H-bridge (brushed DC gearmotor)
// R_IS / L_IS not connected.
// ---------------------------------------------------------------------------
#define MOTOR_RPWM_PIN  27
#define MOTOR_LPWM_PIN  26
#define MOTOR_REN_PIN   14
#define MOTOR_LEN_PIN   17

#define MOTOR_PWM_FREQ  20000
#define MOTOR_PWM_BITS  8

// ---------------------------------------------------------------------------
// MT6816 SPI encoder — bit-bang, read-only
// ---------------------------------------------------------------------------
#define ENC_MOSI_PIN    23
#define ENC_MISO_PIN    19
#define ENC_SCK_PIN     18
#define ENC_CS_PIN      15

// Gear ratio: encoder pinion turns per tilt shaft revolution
// Update once mechanical is finalized
#define ENCODER_GEAR_RATIO  4.0f

// ---------------------------------------------------------------------------
// Hall effect sensor — revolution counter / homing
// ---------------------------------------------------------------------------
#define HALL_PIN        33

// ---------------------------------------------------------------------------
// Homing
// ---------------------------------------------------------------------------
#define HOME_DUTY           150
#define HOME_DIRECTION      1
#define HOME_TIMEOUT_MS     20000

// ---------------------------------------------------------------------------
// I2C — MPU-6050 only
// ---------------------------------------------------------------------------
#define I2C_SDA_PIN     21
#define I2C_SCL_PIN     22
#define I2C_FREQ        400000

// MPU-6050 @ 0x68
