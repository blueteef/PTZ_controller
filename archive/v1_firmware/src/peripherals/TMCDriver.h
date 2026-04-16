#pragma once

// =============================================================================
// TMCDriver — BigTreeTech TMC2209 V1.3 initialisation.
//
// Configures both stepper drivers over UART at boot.  STEP/DIR/EN control is
// still handled by FastAccelStepper — TMCDriver only sets up the driver IC
// registers (current, mode, interpolation).
//
// Wiring
// ──────
//   GPIO4  (TX) ──[1kΩ]──┬── Pan  PDN_UART
//   GPIO13 (RX) ──────────┘── Pan  PDN_UART
//                         └── Tilt PDN_UART  (same wire, diff address)
//
//   MS1/MS2 on Pan  board: LOW / LOW  → UART address 0
//   MS1/MS2 on Tilt board: HIGH/ LOW  → UART address 1
// =============================================================================

#include <Arduino.h>

class TMCDriver {
public:
    // Configure both TMC2209 drivers.  Call before MotionController::begin().
    // Returns true if both drivers respond; false if either is missing/wired wrong.
    static bool begin();

    // Runtime current adjustment — called if Pi pushes updated current via CLI.
    static void setPanCurrent(uint16_t mA_rms);
    static void setTiltCurrent(uint16_t mA_rms);
};
