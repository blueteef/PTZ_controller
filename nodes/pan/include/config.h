#pragma once

// =============================================================================
// Pan Node — Hardware Configuration
// =============================================================================

#define NODE_ID         NODE_PAN

// CAN (TWAI)
#define CAN_TX_PIN      21
#define CAN_RX_PIN      22

// Pan axis TMC2209
#define PAN_STEP_PIN    27
#define PAN_DIR_PIN     26
#define PAN_EN_PIN      14

// Tilt axis TMC2209
#define TILT_STEP_PIN   33
#define TILT_DIR_PIN    32
#define TILT_EN_PIN     25

// TMC2209 UART (shared bus, MS1 sets address)
#define TMC_UART_TX     17
#define TMC_UART_RX     4
#define TMC_UART_BAUD   115200
#define TMC_ADDR_PAN    0   // MS1=GND
#define TMC_ADDR_TILT   1   // MS1=3V3

// GPS (ATGM336H)
#define GPS_RX_PIN      13  // ESP32 receives GPS TX
#define GPS_TX_PIN      16  // ESP32 transmits to GPS RX
#define GPS_BAUD        9600

// I2C (INA226, QMC5883L, BMP280)
#define I2C_SDA_PIN     22  // TBD — confirm from schematic
#define I2C_SCL_PIN     23  // TBD — confirm from schematic

// Gear ratios
#define PAN_GEAR_RATIO  (144.0f / 17.0f)
#define TILT_GEAR_RATIO (64.0f  / 21.0f)
#define STEPS_PER_REV   3200    // 200 steps * 16 microsteps
