#pragma once

// =============================================================================
// CLI — GNU-style serial command interface
//
// Commands are entered over USB Serial at CLI_BAUD_RATE (115200).
// Syntax follows GNU conventions:
//   • Subcommand style:  <verb> [object] [args]
//   • Long-form flags:   --relative,  --axis pan
//   • 'help'             lists all commands
//   • 'help <command>'   shows detailed usage for that command
//   • 'version'          shows firmware version
// =============================================================================

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "motion/MotionController.h"
#include "config.h"
#include "version.h"

class CLI {
public:
    explicit CLI(MotionController& motion);

    // Call from setup().  Optionally pass a custom baud rate.
    bool begin(uint32_t baud = CLI_BAUD_RATE);

    // FreeRTOS task entry point.
    static void cliTask(void* param);

    // Thread-safe printf — can be called from other tasks.
    void print(const char* msg);
    void printf(const char* fmt, ...);

private:
    MotionController& _motion;
    char     _buf[CLI_MAX_LINE];
    int      _len = 0;
    SemaphoreHandle_t _txMutex = nullptr;

    void readSerial();
    void dispatch(char* line);
    void printPrompt();

    // ── Command handlers ────────────────────────────────────────────────────
    void cmdHelp    (int argc, char** argv);
    void cmdVersion (int argc, char** argv);
    void cmdStatus  (int argc, char** argv);
    void cmdHome    (int argc, char** argv);
    void cmdMove    (int argc, char** argv);
    void cmdStop    (int argc, char** argv);
    void cmdEstop   (int argc, char** argv);
    void cmdEnable  (int argc, char** argv);
    void cmdDisable (int argc, char** argv);
    void cmdSet     (int argc, char** argv);
    void cmdGet     (int argc, char** argv);
    void cmdPing    (int argc, char** argv);
    void cmdCal     (int argc, char** argv);
    void cmdSave    (int argc, char** argv);
    void cmdReset   (int argc, char** argv);
    void cmdReboot  (int argc, char** argv);
    void cmdVel     (int argc, char** argv);
    void cmdDiag    (int argc, char** argv);

    // ── Helpers ─────────────────────────────────────────────────────────────
    bool parseAxis(const char* s, AxisId& out) const;
    bool parseFloat(const char* s, float& out) const;
};
