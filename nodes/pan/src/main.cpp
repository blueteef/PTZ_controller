#include <Arduino.h>
#include "can_ids.h"
#include "can_frames.h"
#include "config.h"

// Pan node — ESP32
// Responsibilities:
//   - Pan + tilt TMC2209 stepper drivers (STEP/DIR/EN)
//   - TMC2209 UART configuration (StealthChop, current)
//   - INA226 power monitor
//   - ATGM336H GPS
//   - QMC5883L compass
//   - BMP280 baro/temp
//   - CAN bus (TWAI) node: NODE_PAN

void setup() {
    // TODO: init TWAI
    // TODO: init TMC2209 drivers
    // TODO: init sensors
}

void loop() {
    // TODO: process incoming CAN frames
    // TODO: step generation / motion tick
    // TODO: broadcast sensor data + position reports
}
