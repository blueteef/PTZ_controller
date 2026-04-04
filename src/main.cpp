// =============================================================================
// main.cpp — PTZ Controller entry point
//
// Boot sequence
// ─────────────
//   1. USB Serial initialised
//   2. Status LED initialised
//   3. TMC2209 drivers configured over UART (current, StealthChop, interpolation)
//   4. MotionController initialised (FastAccelStepper)
//   5. CLI initialised
//   6. FreeRTOS tasks created
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
#include "peripherals/TMCDriver.h"
#include "sensors/SensorManager.h"

static MotionController gMotion;
static StatusLED        gLED;
static CLI*             gCLI = nullptr;
static SensorManager    gSensors;

void setup() {
    Serial.begin(CLI_BAUD_RATE);
    uint32_t t = millis();
    while (!Serial && (millis() - t) < 2000) {}

    Serial.printf("\r\n%s v%s  booting...\r\n",
                  PTZ_FIRMWARE_NAME, PTZ_VERSION_STRING);

    gLED.begin();
    gLED.setPattern(LEDPattern::FAST_BLINK);

    // TMC2209 must be configured before MotionController enables the drivers.
    if (!TMCDriver::begin()) {
        Serial.println("[WARN] One or more TMC2209 drivers did not respond — check wiring");
        // Non-fatal: steppers may still move if STEP/DIR/EN are correct,
        // just without UART-configured current/mode settings.
    }

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
    gCLI->setSensorManager(&gSensors);
    Serial.println("[BOOT] CLI OK");

    gSensors.begin();
    Serial.println("[BOOT] SensorManager OK");

    xTaskCreatePinnedToCore(
        MotionController::motionTask, "motion",
        TASK_MOTION_STACK, &gMotion,
        TASK_MOTION_PRIORITY, nullptr, 1);

    xTaskCreatePinnedToCore(
        CLI::cliTask, "cli",
        TASK_CLI_STACK, gCLI,
        TASK_CLI_PRIORITY, nullptr, 1);

    xTaskCreatePinnedToCore(
        SensorManager::sensorTask, "sensors",
        TASK_SENSOR_STACK, &gSensors,
        TASK_SENSOR_PRIORITY, nullptr, 0);  // core 0 — away from motion/CLI on core 1

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
