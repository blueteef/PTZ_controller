#pragma once

// =============================================================================
// PTZ Controller — All tuneable constants and pin assignments
// =============================================================================

// -----------------------------------------------------------------------------
// Pin Definitions
// -----------------------------------------------------------------------------
#define STATUS_LED_PIN   2
#define LASER_PIN        13   // FET driver gate — HIGH = laser on

// Pan axis
#define PAN_STEP_PIN      25
#define PAN_DIR_PIN       26
#define PAN_EN_PIN        27
#define PAN_UART_TX_PIN   17   // Serial2 TX → MKS Rx
#define PAN_UART_RX_PIN   16   // Serial2 RX ← MKS Tx

// Tilt axis
#define TILT_STEP_PIN     32
#define TILT_DIR_PIN      33
#define TILT_EN_PIN       19
#define TILT_UART_TX_PIN   4   // Serial1 TX → MKS Rx
#define TILT_UART_RX_PIN   5   // Serial1 RX ← MKS Tx

// NOTE: The MKS COM pin on the signal connector is NOT the serial data line.
//   Standard variant: COM is floating (leave unconnected).
//   OC variant:       COM is the optocoupler supply — connect to ESP32 3.3V.
// Serial communication uses the separate 4-pin UART header (Tx, Rx, G, 3V3).

// Stepper direction invert — set true if axis moves the wrong way.
// Flips the DIR pin logic in FastAccelStepper without rewiring.
#define PAN_DIR_INVERT    false
#define TILT_DIR_INVERT   false

// UART0 (Serial) = USB CLI (default Arduino Serial)

// -----------------------------------------------------------------------------
// MKS Servo42C UART Settings
// -----------------------------------------------------------------------------
#define MKS_BAUD_RATE         38400
#define MKS_UART_TIMEOUT_MS   50
// Slave addresses: 0xE0–0xE9 (set via onboard menu or UART command 0x8B).
// Default from factory is 0xE0.  If both drivers share a bus, assign unique addresses.
#define MKS_PAN_ADDR          0xE0
#define MKS_TILT_ADDR         0xE1

// -----------------------------------------------------------------------------
// Motion Defaults
// -----------------------------------------------------------------------------
#define MOTOR_STEPS_PER_REV   200       // 1.8°/step NEMA17
#define DEFAULT_MICROSTEPS    16

// Pan axis gear ratio: 144:17 (output:motor) — approx 8.47:1
#define PAN_GEAR_RATIO_NUM    144
#define PAN_GEAR_RATIO_DEN    17

// Tilt axis gear ratio: 64:21 (output:motor) — approx 3.048:1
#define TILT_GEAR_RATIO_NUM   64
#define TILT_GEAR_RATIO_DEN   21

#define DEFAULT_MAX_SPEED_DEG_S   180.0f  // degrees per second at output shaft
#define DEFAULT_ACCEL_DEG_S2      30.0f   // degrees per second² at output shaft
#define DEFAULT_FINE_SPEED_SCALE  0.15f   // scale applied when using right stick

// -----------------------------------------------------------------------------
// Soft Travel Limits (degrees)
// -----------------------------------------------------------------------------
#define PAN_SOFT_LIMIT_MIN    -180.0f
#define PAN_SOFT_LIMIT_MAX     180.0f
#define TILT_SOFT_LIMIT_MIN    -45.0f
#define TILT_SOFT_LIMIT_MAX     90.0f
#define SOFT_LIMITS_ENABLED    false  // re-enable after homing is configured

// -----------------------------------------------------------------------------
// Gamepad / Bluepad32
// -----------------------------------------------------------------------------
#define GAMEPAD_STICK_DEADZONE  64    // out of 512 (12.5%)
#define GAMEPAD_STICK_MAX       512
#define GAMEPAD_TRIGGER_MAX     1023
#define GAMEPAD_RECONNECT_MS    5000

// -----------------------------------------------------------------------------
// FreeRTOS Task Priorities and Stack Sizes
// -----------------------------------------------------------------------------
#define TASK_MOTION_PRIORITY   5
#define TASK_INPUT_PRIORITY    4
#define TASK_DRIVER_PRIORITY   3
#define TASK_CLI_PRIORITY      2

#define TASK_MOTION_STACK      4096
#define TASK_INPUT_STACK       4096
#define TASK_DRIVER_STACK      4096
#define TASK_CLI_STACK         8192

// -----------------------------------------------------------------------------
// CLI (Serial Command Interface)
// -----------------------------------------------------------------------------
#define CLI_BAUD_RATE   115200
#define CLI_PROMPT      "ptz> "
#define CLI_MAX_LINE    256
#define CLI_MAX_ARGS    16

// -----------------------------------------------------------------------------
// System / Misc
// -----------------------------------------------------------------------------
#define WATCHDOG_TIMEOUT_MS   5000
#define NVS_NAMESPACE         "ptz_cfg"

// How long to wait for the MKS_STATUS_HOMING bit to appear after sending 0x91.
// If the bit never shows up, the firmware doesn't support driver-based homing;
// software homing (run + encoder-wrap detection) takes over.
#define HOMING_BIT_WAIT_MS    2000

// Hard upper bound on any homing cycle (driver-based or software).
#define HOMING_TIMEOUT_MS     30000

// Software homing: speed at output shaft (deg/s) and encoder wrap-detection
// threshold (degrees).  The motor runs slowly until the encoder crosses 0°.
#define HOMING_SW_SPEED_DEG_S  20.0f
#define HOMING_SW_WRAP_THRESH  30.0f   // jump > this degrees in one poll = wrap
