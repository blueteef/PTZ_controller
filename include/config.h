#pragma once

// =============================================================================
// PTZ Controller — Hardware pin assignments and fixed constants only.
//
// All tunable motion settings (speed, accel, limits, invert, fine scale) are
// owned by the Pi and pushed to the ESP32 via the CLI protocol on every
// connect. The ESP32 has no persistent storage for motion settings.
// =============================================================================

// -----------------------------------------------------------------------------
// Pins
// -----------------------------------------------------------------------------
#define STATUS_LED_PIN   2

// Tilt axis
#define TILT_DIR_PIN     32
#define TILT_STEP_PIN    33
#define TILT_EN_PIN      25

// Pan axis
#define PAN_DIR_PIN      26
#define PAN_STEP_PIN     27
#define PAN_EN_PIN       14

// -----------------------------------------------------------------------------
// Pi UART (Serial2) — direct GPIO link, no USB cable needed.
//   Pi GPIO14 (TX) → ESP32 GPIO19 (RX)
//   Pi GPIO15 (RX) ← ESP32 GPIO21 (TX)
//   Pi GND         → ESP32 GND  (3.3 V logic on both sides, no shifter)
// -----------------------------------------------------------------------------
#define PI_UART_RX_PIN   19
#define PI_UART_TX_PIN   21
#define PI_BAUD_RATE     57600

// -----------------------------------------------------------------------------
// Motor / gearing — physical hardware, do not change without rewiring.
// -----------------------------------------------------------------------------
#define MOTOR_STEPS_PER_REV   200       // 1.8°/step NEMA17
#define DEFAULT_MICROSTEPS    16        // A4988: MS1/MS2/MS3 all HIGH → 1/16

// Pan  gear ratio  144:17  ≈ 8.47:1  (output : motor)
#define PAN_GEAR_RATIO_NUM    144
#define PAN_GEAR_RATIO_DEN    17

// Tilt gear ratio   64:21  ≈ 3.05:1
#define TILT_GEAR_RATIO_NUM   64
#define TILT_GEAR_RATIO_DEN   21

// Minimum speed below which setVelocity issues a stop instead.
#define MIN_VELOCITY_DEG_S    0.5f

// Velocity watchdog: stop all axes if no vel command arrives while running.
#define VEL_WATCHDOG_MS       110

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

// Jog mode: stop axis if no directional key arrives within this window.
#define JOG_KEY_TIMEOUT_MS    150
