// =============================================================================
// main.cpp — PTZ Controller entry point
//
// Boot sequence
// ─────────────
//   1. USB Serial initialised
//   2. Status LED initialised
//   3. MotionController initialised (FastAccelStepper, A4988 drivers)
//   4. CLI initialised
//   5. FreeRTOS tasks created
//
// Control
// ───────
//   Type 'jog' in the serial terminal for real-time WASD keyboard control.
//   Type 'help' for all commands.
//   The 'vel' command is the programmatic interface for Pi-based tracking.
// =============================================================================

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "config.h"
#include "version.h"
#include "motion/MotionController.h"
#include "cli/CLI.h"
#include "peripherals/StatusLED.h"

static MotionController gMotion;
static StatusLED        gLED;
static CLI*             gCLI = nullptr;

void setup() {
    Serial.begin(CLI_BAUD_RATE);
    uint32_t t = millis();
    while (!Serial && (millis() - t) < 2000) {}

    Serial.printf("\r\n%s v%s  booting...\r\n",
                  PTZ_FIRMWARE_NAME, PTZ_VERSION_STRING);

    gLED.begin();
    gLED.setPattern(LEDPattern::FAST_BLINK);

    if (!gMotion.begin()) {
        Serial.println("[FATAL] MotionController init failed — halting");
        gLED.setPattern(LEDPattern::FAST_BLINK);
        while (true) vTaskDelay(portMAX_DELAY);
    }
    Serial.println("[BOOT] MotionController OK");

    // Pi hardware UART — same CLI protocol as USB, no cable needed.
    Serial2.begin(PI_BAUD_RATE, SERIAL_8N1, PI_UART_RX_PIN, PI_UART_TX_PIN);
    Serial2.printf("\r\nPTZ UART OK %d baud\r\n", PI_BAUD_RATE);
    Serial.println("[BOOT] Pi UART OK (GPIO19 RX, GPIO21 TX)");

    gCLI = new CLI(gMotion);
    gCLI->begin(CLI_BAUD_RATE);
    Serial.println("[BOOT] CLI OK");

    xTaskCreatePinnedToCore(
        MotionController::motionTask, "motion",
        TASK_MOTION_STACK, &gMotion,
        TASK_MOTION_PRIORITY, nullptr, 1);

    xTaskCreatePinnedToCore(
        CLI::cliTask, "cli",
        TASK_CLI_STACK, gCLI,
        TASK_CLI_PRIORITY, nullptr, 1);

    gLED.setPattern(LEDPattern::SLOW_BLINK);
    Serial.printf("[BOOT] %s v%s ready\r\n",
                  PTZ_FIRMWARE_NAME, PTZ_VERSION_STRING);
}

void loop() {
    if (gMotion.isEstopped()) {
        gLED.setPattern(LEDPattern::FAST_BLINK);
    } else if (gMotion.isRunning(AxisId::PAN) || gMotion.isRunning(AxisId::TILT)) {
        gLED.setPattern(LEDPattern::SOLID);
    } else {
        gLED.setPattern(LEDPattern::SLOW_BLINK);
    }
    gLED.update();
    vTaskDelay(pdMS_TO_TICKS(20));
}
