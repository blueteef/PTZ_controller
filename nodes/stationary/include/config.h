#pragma once

// =============================================================================
// Stationary Node — Hardware Configuration
// =============================================================================

#define NODE_ID         NODE_STATIONARY

// CAN (TWAI)
#define CAN_TX_PIN      21  // TBD
#define CAN_RX_PIN      22  // TBD

// Stepper — TMC2209
// Pin assignments TBD pending schematic revision
#define STEP_PIN        -1
#define DIR_PIN         -1
#define EN_PIN          -1

#define TMC_UART_TX     -1
#define TMC_UART_RX     -1
#define TMC_UART_BAUD   115200

// Sensors TBD
