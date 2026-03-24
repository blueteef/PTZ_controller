#pragma once

// =============================================================================
// PTZ Controller — Pin assignments and tunable constants
// A4988 step/dir/en drivers on both axes.
// =============================================================================

// -----------------------------------------------------------------------------
// Pins
// -----------------------------------------------------------------------------
#define STATUS_LED_PIN   2

// Pan axis
#define PAN_STEP_PIN     32
#define PAN_DIR_PIN      33
#define PAN_EN_PIN       25

// Tilt axis
#define TILT_STEP_PIN    26
#define TILT_DIR_PIN     27
#define TILT_EN_PIN      14

// Stepper direction invert — set true if axis moves the wrong way.
// Flips DIR logic in FastAccelStepper; no rewiring needed.
#define PAN_DIR_INVERT   true
#define TILT_DIR_INVERT  true

// A4988 EN is active-LOW: drive LOW to enable, HIGH to disable.
// EN pins are managed directly with digitalWrite — not via FastAccelStepper.

// MS1/MS2/MS3 on A4988 should be hardwired for 16x microstepping:
//   MS1=HIGH  MS2=HIGH  MS3=HIGH  → connect to 3.3 V on the breakout

// -----------------------------------------------------------------------------
// Pi UART (Serial2) — direct GPIO link, no USB cable needed
//   Pi GPIO14 (TX) → ESP32 GPIO19 (RX)
//   Pi GPIO15 (RX) ← ESP32 GPIO21 (TX)
//   Pi GND         → ESP32 GND        (3.3 V logic on both sides, no shifter)
// On Pi: enable serial in raspi-config → Interface Options → Serial
//   disable login shell on serial, enable serial port hardware → /dev/serial0
// -----------------------------------------------------------------------------
#define PI_UART_RX_PIN   19
#define PI_UART_TX_PIN   21
#define PI_BAUD_RATE     115200

// UART0 (Serial/USB) — kept for development and local debugging

// -----------------------------------------------------------------------------
// Motor / gearing
// -----------------------------------------------------------------------------
#define MOTOR_STEPS_PER_REV   200       // 1.8°/step NEMA17
#define DEFAULT_MICROSTEPS    16

// Pan  gear ratio  144:17  ≈ 8.47:1  (output : motor)
#define PAN_GEAR_RATIO_NUM    144
#define PAN_GEAR_RATIO_DEN    17

// Tilt gear ratio   64:21  ≈ 3.05:1
#define TILT_GEAR_RATIO_NUM   64
#define TILT_GEAR_RATIO_DEN   21

// -----------------------------------------------------------------------------
// Motion defaults  — conservative starting point; tune with 'set speed/accel'
// -----------------------------------------------------------------------------
#define DEFAULT_MAX_SPEED_DEG_S    90.0f   // output-shaft deg/s
#define DEFAULT_ACCEL_DEG_S2      360.0f   // output-shaft deg/s²  (ramp fast, stop crisp)
#define DEFAULT_FINE_SPEED_SCALE    0.2f   // 'jog fine' multiplier

// Minimum speed threshold below which setVelocity issues a stop instead.
#define MIN_VELOCITY_DEG_S          0.5f

// -----------------------------------------------------------------------------
// Soft travel limits (degrees at output shaft)
// -----------------------------------------------------------------------------
#define PAN_SOFT_LIMIT_MIN    -180.0f
#define PAN_SOFT_LIMIT_MAX     180.0f
#define TILT_SOFT_LIMIT_MIN    -45.0f
#define TILT_SOFT_LIMIT_MAX     90.0f
#define SOFT_LIMITS_ENABLED    false   // enable after verifying range of motion

// -----------------------------------------------------------------------------
// FreeRTOS tasks
// -----------------------------------------------------------------------------
#define TASK_MOTION_PRIORITY   5
#define TASK_CLI_PRIORITY      2

#define TASK_MOTION_STACK      4096
#define TASK_CLI_STACK         6144

// -----------------------------------------------------------------------------
// CLI (USB serial)
// -----------------------------------------------------------------------------
#define CLI_BAUD_RATE   115200
#define CLI_PROMPT      "ptz> "
#define CLI_MAX_LINE    256
#define CLI_MAX_ARGS    16

// Jog mode: if no directional key arrives within this window, stop the axis.
#define JOG_KEY_TIMEOUT_MS   150

// -----------------------------------------------------------------------------
// System
// -----------------------------------------------------------------------------
#define NVS_NAMESPACE   "ptz_cfg"
