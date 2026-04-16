#include <Arduino.h>
#include "can_ids.h"
#include "can_frames.h"
#include "config.h"

// Stationary node — ESP32
// Responsibilities:
//   - 1x TMC2209 stepper
//   - Onboard sensors (TBD)
//   - CAN bus (TWAI) node: NODE_STATIONARY

void setup() {
    // TODO: init TWAI
    // TODO: init TMC2209
    // TODO: init sensors
}

void loop() {
    // TODO: process incoming CAN frames
    // TODO: stepper motion tick
    // TODO: broadcast sensor data
}
