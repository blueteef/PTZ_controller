#pragma once

// =============================================================================
// Tilt Node — Hardware Configuration
// =============================================================================

#define NODE_ID         NODE_TILT

// CAN (TWAI)
#define CAN_TX_PIN      21  // TBD
#define CAN_RX_PIN      22  // TBD

// Pi UART bridge
#define PI_UART_TX      17  // TBD
#define PI_UART_RX      16  // TBD
#define PI_UART_BAUD    921600

// Lens steppers — TMC2209 (up to 4)
// Pin assignments TBD pending schematic revision
#define ZOOM_STEP_PIN   -1
#define ZOOM_DIR_PIN    -1
#define ZOOM_EN_PIN     -1

#define FOCUS_STEP_PIN  -1
#define FOCUS_DIR_PIN   -1
#define FOCUS_EN_PIN    -1

#define IRIS_STEP_PIN   -1
#define IRIS_DIR_PIN    -1
#define IRIS_EN_PIN     -1

// TMC2209 UART (shared bus)
#define TMC_UART_TX     -1  // TBD
#define TMC_UART_RX     -1  // TBD
#define TMC_UART_BAUD   115200
