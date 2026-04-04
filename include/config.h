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
// TMC2209 UART (Serial1, half-duplex)
//   Wiring: GPIO4  (TX) → PDN (TX header pin, has onboard 1kΩ) on both drivers
//           GPIO17 (RX) → PDN (RX header pin, direct) on both drivers
//   Address: Pan  driver MS1=LOW  MS2=LOW  → addr 0
//            Tilt driver MS1=HIGH MS2=LOW  → addr 1
// -----------------------------------------------------------------------------
#define TMC_UART_RX_PIN   17
#define TMC_UART_TX_PIN   4
#define TMC_R_SENSE       0.11f   // BTT TMC2209 V1.3 sense resistor (Ω)
#define TMC_PAN_ADDR      0       // set by MS1=LOW  MS2=LOW
#define TMC_TILT_ADDR     1       // set by MS1=HIGH MS2=LOW
#define TMC_PAN_RMS_MA    700     // RMS current for pan  axis (mA) — tune to taste
#define TMC_TILT_RMS_MA   700     // RMS current for tilt axis (mA)

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
#define DEFAULT_MICROSTEPS    16        // STEP-pin resolution; TMC2209 interpolates to 256x internally

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

// -----------------------------------------------------------------------------
// Sensors — stationary side
// -----------------------------------------------------------------------------

// I2C bus 0 (INA226 + BMP280 on stationary chassis)
#define I2C0_SDA_PIN         22
#define I2C0_SCL_PIN         23

// INA226 power monitor (R100 shunt = 0.1 Ω, addr set by A0/A1 = GND/GND)
#define INA226_I2C_ADDR      0x40
#define INA226_SHUNT_OHM     0.1f
#define INA226_MAX_A         8.0f

// BMP280 barometric / temperature (SDO=GND → I2C addr 0x76)
#define BMP280_I2C_ADDR      0x76

// GPS UART on Serial1 (HardwareSerial 1)
//   Pi GPIO16 (RX) ← ESP32 GPIO13 (TX)  [GPS TX → ESP32]
//   Pi GPIO13 (TX) → ESP32 GPIO16 (RX)  [GPS RX ← ESP32]
// Note: TX pin here just reserves the GPIO; GPS modules rarely need commands.
#define GPS_RX_PIN           16
#define GPS_TX_PIN           13
#define GPS_BAUD_RATE        9600

// Sensor push intervals
#define SENSOR_PUSH_MS       1000   // INA226 / BMP280 / GPS  (slow sensors, 1 Hz)
#define SENSOR_IMU_PUSH_MS   50     // IMU / compass           (20 Hz)

// I2C bus 1 (moving side — MPU-6050 + QMC5883L through slip ring)
#define I2C1_SDA_PIN         18
#define I2C1_SCL_PIN         5

// MPU-6050 IMU (AD0=GND → 0x68)
#define MPU6050_I2C_ADDR     0x68

// QMC5883L compass (fixed address 0x0D)
#define QMC5883L_I2C_ADDR    0x0D

// -----------------------------------------------------------------------------
// FreeRTOS sensor task
// -----------------------------------------------------------------------------
#define TASK_SENSOR_PRIORITY   1
#define TASK_SENSOR_STACK      4096
