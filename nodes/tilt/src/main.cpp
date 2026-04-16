#include <Arduino.h>
#include "can_ids.h"
#include "can_frames.h"
#include "config.h"

// Tilt node — ESP32
// Responsibilities:
//   - Up to 4 TMC2209 lens steppers (zoom, focus, iris, spare)
//   - CAN bus (TWAI) node: NODE_TILT
//   - Local UART bridge to Raspberry Pi
//
// Pi handles: camera, thermal, vision pipeline, high-level commands
// This ESP32 is the real-time / CAN interface for the tilt board

void setup() {
    // TODO: init TWAI
    // TODO: init TMC2209 drivers
    // TODO: init Pi UART bridge
}

void loop() {
    // TODO: process incoming CAN frames (lens commands from pi via bridge)
    // TODO: forward sensor/position data to Pi
    // TODO: lens stepper motion tick
}
