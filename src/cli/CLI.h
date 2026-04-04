#pragma once

// =============================================================================
// CLI — USB serial command interface
//
// Normal commands are line-based (send over any serial terminal at 115200).
// Jog mode gives real-time WASD keyboard control of both axes.
//
// Commands
// ────────
//   help  [command]              Show help
//   version                      Firmware version
//   status                       Position / motion state
//
//   jog   [speed_deg_s]          Enter WASD keyboard jog mode
//   vel   <pan|tilt|all> <°/s>   Set continuous velocity (remote/tracking use)
//   move  <pan|tilt> <°> [--relative]
//   stop  [pan|tilt|all]
//   estop                        Hard stop + latch
//   enable  [pan|tilt|all]
//   disable [pan|tilt|all]
//
//   set   speed  <°/s>
//   set   accel  <°/s²>
//   set   fine   <0–1>           Fine-speed scale used in jog mode
//   set   limits <pan|tilt> <min> <max>
//   set   limits on|off
//   get   position|speed|accel|limits
//
//   save                         Persist settings to flash
//   reset                        Restore factory defaults (in RAM only)
//   reboot
// =============================================================================

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "motion/MotionController.h"
#include "sensors/SensorManager.h"
#include "config.h"
#include "version.h"

class CLI {
public:
    explicit CLI(MotionController& motion);

    bool begin(uint32_t baud = CLI_BAUD_RATE);

    void setSensorManager(SensorManager* s) { _sensors = s; }

    static void cliTask(void* param);

    // Thread-safe output — callable from other tasks.
    void print(const char* msg);
    void printf(const char* fmt, ...);

private:
    MotionController& _motion;
    SensorManager*    _sensors = nullptr;
    char     _buf[CLI_MAX_LINE];
    int      _len = 0;
    char     _piBuf[CLI_MAX_LINE];  // receive buffer for Pi UART (Serial2)
    int      _piLen = 0;
    SemaphoreHandle_t _txMutex = nullptr;

    void readSerial();    // USB — with echo and prompt
    void readPiSerial();  // Pi UART — no echo, no prompt
    void dispatch(char* line);
    void printPrompt();
    void printToPi(const char* msg);  // Serial2-only — for query() responses

    void cmdHelp    (int argc, char** argv);
    void cmdVersion (int argc, char** argv);
    void cmdStatus  (int argc, char** argv);
    void cmdJog     (int argc, char** argv);
    void cmdVel     (int argc, char** argv);
    void cmdMove    (int argc, char** argv);
    void cmdStop    (int argc, char** argv);
    void cmdEstop   (int argc, char** argv);
    void cmdEnable  (int argc, char** argv);
    void cmdDisable (int argc, char** argv);
    void cmdSet     (int argc, char** argv);
    void cmdGet     (int argc, char** argv);
    void cmdPing    (int argc, char** argv);
    void cmdSave    (int argc, char** argv);
    void cmdReset   (int argc, char** argv);
    void cmdReboot  (int argc, char** argv);
    void cmdSensor  (int argc, char** argv);

    bool parseAxis (const char* s, AxisId& out) const;
    bool parseFloat(const char* s, float&  out) const;
};
