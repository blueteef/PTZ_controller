// =============================================================================
// main.cpp — PTZ Controller entry point
//
// Boot sequence
// ─────────────
//   1. USB Serial (CLI) + MKS UART ports initialised
//   2. Status LED initialised (fast blink while booting)
//   3. MotionController initialised (FastAccelStepper + MKS drivers)
//   4. GamepadInput initialised (Bluepad32 Bluetooth stack)
//   5. CLI initialised
//   6. FreeRTOS tasks created and scheduler takes over
//
// Pin assignments and all tuneable constants live in include/config.h.
// =============================================================================

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "config.h"
#include "version.h"
#include "motion/MotionController.h"
#include "input/GamepadInput.h"
#include "cli/CLI.h"
#include "peripherals/StatusLED.h"

// -----------------------------------------------------------------------------
// Global objects  (constructed before setup(); kept in .bss / .data)
// -----------------------------------------------------------------------------
static MotionController gMotion;
static StatusLED        gLED;

// Heap-allocated after Serial is ready so constructor output is visible.
static GamepadInput* gInput = nullptr;
static CLI*          gCLI   = nullptr;

// -----------------------------------------------------------------------------
// setup
// -----------------------------------------------------------------------------

void setup() {
    // ── USB Serial (CLI) ────────────────────────────────────────────────────
    Serial.begin(CLI_BAUD_RATE);
    // Give the host a moment to open the port before printing.
    uint32_t t = millis();
    while (!Serial && (millis() - t) < 2000) {}

    Serial.printf("\r\n%s v%s  booting...\r\n",
                  PTZ_FIRMWARE_NAME, PTZ_VERSION_STRING);

    // ── MKS UART ports ──────────────────────────────────────────────────────
    // Full-duplex TTL UART — connect the MKS 4-pin UART header (Tx/Rx/G/3V3),
    // NOT the signal-connector COM pin.  Cross Tx→Rx and Rx→Tx.
    // Serial2 → Pan  driver  (TX=GPIO17 → MKS Rx,  RX=GPIO16 ← MKS Tx)
    // Serial1 → Tilt driver  (TX=GPIO4  → MKS Rx,  RX=GPIO5  ← MKS Tx)
    Serial2.begin(MKS_BAUD_RATE, SERIAL_8N1, PAN_UART_RX_PIN,  PAN_UART_TX_PIN);
    Serial1.begin(MKS_BAUD_RATE, SERIAL_8N1, TILT_UART_RX_PIN, TILT_UART_TX_PIN);

    // ── Status LED ──────────────────────────────────────────────────────────
    gLED.begin();
    gLED.setPattern(LEDPattern::FAST_BLINK); // fast blink = booting

    // ── Motion controller ───────────────────────────────────────────────────
    if (!gMotion.begin()) {
        Serial.println("[FATAL] MotionController init failed — halting");
        gLED.setPattern(LEDPattern::FAST_BLINK);
        while (true) vTaskDelay(portMAX_DELAY);
    }
    Serial.println("[BOOT] MotionController OK");

    // ── Gamepad (Bluepad32) ─────────────────────────────────────────────────
    gInput = new GamepadInput(gMotion);
    if (!gInput->begin()) {
        Serial.println("[WARN] GamepadInput init failed — continuing without gamepad");
    } else {
        Serial.println("[BOOT] Bluepad32 OK — waiting for controller...");
    }

    // ── CLI ─────────────────────────────────────────────────────────────────
    gCLI = new CLI(gMotion);
    gCLI->begin(CLI_BAUD_RATE);
    Serial.println("[BOOT] CLI OK");

    // ── FreeRTOS tasks ───────────────────────────────────────────────────────
    // Motion task  — core 1 (same as Arduino loop, avoids contention with BT)
    xTaskCreatePinnedToCore(
        MotionController::motionTask, "motion",
        TASK_MOTION_STACK, &gMotion,
        TASK_MOTION_PRIORITY, nullptr, 1);

    // Input task   — core 0 (Bluetooth stack lives here)
    xTaskCreatePinnedToCore(
        GamepadInput::inputTask, "input",
        TASK_INPUT_STACK, gInput,
        TASK_INPUT_PRIORITY, nullptr, 0);

    // CLI task     — core 1
    xTaskCreatePinnedToCore(
        CLI::cliTask, "cli",
        TASK_CLI_STACK, gCLI,
        TASK_CLI_PRIORITY, nullptr, 1);

    gLED.setPattern(LEDPattern::SLOW_BLINK); // slow blink = idle / no gamepad
    Serial.printf("[BOOT] %s v%s ready\r\n",
                  PTZ_FIRMWARE_NAME, PTZ_VERSION_STRING);
}

// -----------------------------------------------------------------------------
// loop  — updates the status LED and yields; real work is in RTOS tasks
// -----------------------------------------------------------------------------

void loop() {
    // Choose LED pattern based on system state.
    if (gMotion.isHoming(AxisId::PAN) || gMotion.isHoming(AxisId::TILT)) {
        gLED.setPattern(LEDPattern::DOUBLE_BLINK);
    } else if (gMotion.isRunning(AxisId::PAN) || gMotion.isRunning(AxisId::TILT)) {
        gLED.setPattern(LEDPattern::SOLID);
    } else {
        gLED.setPattern(LEDPattern::SLOW_BLINK);
    }

    gLED.update();
    vTaskDelay(pdMS_TO_TICKS(20));
}
