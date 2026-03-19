// =============================================================================
// CLI.cpp — GNU-style serial command interface
// =============================================================================

#include "CLI.h"
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <esp_system.h>

static constexpr uint32_t CLI_TASK_PERIOD_MS = 10;

// -----------------------------------------------------------------------------
// Construction / begin
// -----------------------------------------------------------------------------

CLI::CLI(MotionController& motion) : _motion(motion) {}

bool CLI::begin(uint32_t baud) {
    Serial.begin(baud);
    _txMutex = xSemaphoreCreateMutex();
    return (_txMutex != nullptr);
}

// -----------------------------------------------------------------------------
// Thread-safe output
// -----------------------------------------------------------------------------

void CLI::print(const char* msg) {
    xSemaphoreTake(_txMutex, portMAX_DELAY);
    Serial.print(msg);
    xSemaphoreGive(_txMutex);
}

void CLI::printf(const char* fmt, ...) {
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    print(tmp);
}

void CLI::printPrompt() {
    print("\r\n" CLI_PROMPT);
}

// -----------------------------------------------------------------------------
// FreeRTOS task
// -----------------------------------------------------------------------------

void CLI::cliTask(void* param) {
    CLI* cli = static_cast<CLI*>(param);
    cli->printf("\r\n%s v%s\r\nType 'help' for available commands.\r\n",
                PTZ_FIRMWARE_NAME, PTZ_VERSION_STRING);
    cli->printPrompt();

    for (;;) {
        cli->readSerial();
        vTaskDelay(pdMS_TO_TICKS(CLI_TASK_PERIOD_MS));
    }
}

// -----------------------------------------------------------------------------
// Serial reader — accumulates characters into _buf, dispatches on newline
// -----------------------------------------------------------------------------

void CLI::readSerial() {
    while (Serial.available()) {
        char c = (char)Serial.read();

        // Echo the character back so the terminal shows what is typed.
        xSemaphoreTake(_txMutex, portMAX_DELAY);
        if (c == '\r' || c == '\n') {
            Serial.print("\r\n");
        } else if (c == 0x7F || c == '\b') { // backspace / DEL
            if (_len > 0) {
                _len--;
                Serial.print("\b \b");
            }
            xSemaphoreGive(_txMutex);
            continue;
        } else {
            Serial.print(c);
        }
        xSemaphoreGive(_txMutex);

        if (c == '\r' || c == '\n') {
            _buf[_len] = '\0';
            if (_len > 0) dispatch(_buf);
            _len = 0;
            printPrompt();
        } else if (_len < CLI_MAX_LINE - 1) {
            _buf[_len++] = c;
        }
    }
}

// -----------------------------------------------------------------------------
// Tokenise line and dispatch to a command handler
// -----------------------------------------------------------------------------

void CLI::dispatch(char* line) {
    // Skip leading whitespace.
    while (*line == ' ') line++;
    if (*line == '\0' || *line == '#') return;

    char*  argv[CLI_MAX_ARGS];
    int    argc = 0;
    char*  p    = line;

    while (*p && argc < CLI_MAX_ARGS) {
        // Skip whitespace between tokens.
        while (*p == ' ') p++;
        if (*p == '\0') break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
    }
    if (argc == 0) return;

    // Command dispatch table.
    const char* cmd = argv[0];
    if      (strcmp(cmd, "help")    == 0) cmdHelp   (argc, argv);
    else if (strcmp(cmd, "version") == 0) cmdVersion(argc, argv);
    else if (strcmp(cmd, "status")  == 0) cmdStatus (argc, argv);
    else if (strcmp(cmd, "home")    == 0) cmdHome   (argc, argv);
    else if (strcmp(cmd, "move")    == 0) cmdMove   (argc, argv);
    else if (strcmp(cmd, "stop")    == 0) cmdStop   (argc, argv);
    else if (strcmp(cmd, "estop")   == 0) cmdEstop  (argc, argv);
    else if (strcmp(cmd, "enable")  == 0) cmdEnable (argc, argv);
    else if (strcmp(cmd, "disable") == 0) cmdDisable(argc, argv);
    else if (strcmp(cmd, "set")     == 0) cmdSet    (argc, argv);
    else if (strcmp(cmd, "get")     == 0) cmdGet    (argc, argv);
    else if (strcmp(cmd, "ping")    == 0) cmdPing   (argc, argv);
    else if (strcmp(cmd, "cal")     == 0) cmdCal    (argc, argv);
    else if (strcmp(cmd, "save")    == 0) cmdSave   (argc, argv);
    else if (strcmp(cmd, "reset")   == 0) cmdReset  (argc, argv);
    else if (strcmp(cmd, "reboot")  == 0) cmdReboot (argc, argv);
    else if (strcmp(cmd, "diag")    == 0) cmdDiag   (argc, argv);
    else {
        printf("Unknown command: '%s'  (type 'help' for a list)\r\n", cmd);
    }
}

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

bool CLI::parseAxis(const char* s, AxisId& out) const {
    if (strcasecmp(s, "pan")  == 0) { out = AxisId::PAN;  return true; }
    if (strcasecmp(s, "tilt") == 0) { out = AxisId::TILT; return true; }
    return false;
}

bool CLI::parseFloat(const char* s, float& out) const {
    char* end;
    out = strtof(s, &end);
    return (end != s && *end == '\0');
}

// -----------------------------------------------------------------------------
// help
// -----------------------------------------------------------------------------

void CLI::cmdHelp(int argc, char** argv) {
    if (argc >= 2) {
        // Per-command help.
        const char* cmd = argv[1];
        if (strcmp(cmd, "move") == 0) {
            print(
                "move <pan|tilt> <degrees> [--relative]\r\n"
                "  Move an axis to an absolute position in degrees.\r\n"
                "  --relative  Treat <degrees> as an offset from the current position.\r\n"
                "  Examples:\r\n"
                "    move pan 45.0\r\n"
                "    move tilt -30.0\r\n"
                "    move pan 10.0 --relative\r\n"
            );
        } else if (strcmp(cmd, "set") == 0) {
            print(
                "set <parameter> <value(s)>\r\n"
                "  Parameters:\r\n"
                "    speed  <deg/s>              — max output-shaft speed\r\n"
                "    accel  <deg/s²>             — acceleration ramp rate\r\n"
                "    fine   <scale 0.0–1.0>      — fine-mode speed multiplier\r\n"
                "    limits <pan|tilt> <min> <max> — soft travel limits (degrees)\r\n"
                "    limits off                  — disable soft limits\r\n"
                "    limits on                   — enable soft limits\r\n"
                "  Examples:\r\n"
                "    set speed 45.0\r\n"
                "    set limits pan -90.0 90.0\r\n"
                "    set limits off\r\n"
            );
        } else if (strcmp(cmd, "get") == 0) {
            print(
                "get <parameter>\r\n"
                "  Parameters:\r\n"
                "    position        — current position of both axes (degrees)\r\n"
                "    speed           — current max speed setting\r\n"
                "    accel           — current acceleration setting\r\n"
                "    limits          — current soft limits for both axes\r\n"
                "    encoder <pan|tilt>  — raw encoder angle from MKS driver\r\n"
            );
        } else {
            printf("No detailed help for '%s'\r\n", cmd);
        }
        return;
    }

    print(
        "\r\nPTZ Controller — available commands\r\n"
        "────────────────────────────────────────────────────\r\n"
        "  help    [command]          Show help (or per-command detail)\r\n"
        "  version                   Show firmware version\r\n"
        "  status                    System status snapshot\r\n"
        "\r\n"
        "  home    [pan|tilt|all]    Home axis using hall-effect sensor\r\n"
        "  move    <pan|tilt> <deg> [--relative]\r\n"
        "                            Move axis to position\r\n"
        "  stop    [pan|tilt|all]    Decelerate and stop\r\n"
        "  estop                     Immediate hard stop (clears e-stop flag)\r\n"
        "\r\n"
        "  enable  [pan|tilt|all]    Enable driver output stage\r\n"
        "  disable [pan|tilt|all]    Disable driver output stage\r\n"
        "\r\n"
        "  set     speed  <deg/s>    Set max speed\r\n"
        "  set     accel  <deg/s²>   Set acceleration\r\n"
        "  set     fine   <scale>    Set fine-speed multiplier\r\n"
        "  set     limits ...        Configure soft limits\r\n"
        "  get     position|speed|accel|limits|encoder\r\n"
        "\r\n"
        "  ping    [pan|tilt|all]    Test UART link to driver\r\n"
        "  cal     [pan|tilt|all]    Run encoder calibration\r\n"
        "\r\n"
        "  save                      Persist settings to flash\r\n"
        "  reset                     Restore factory defaults (no save)\r\n"
        "  reboot                    Restart the ESP32\r\n"
        "────────────────────────────────────────────────────\r\n"
        "Type 'help <command>' for detailed usage.\r\n"
    );
}

// -----------------------------------------------------------------------------
// version
// -----------------------------------------------------------------------------

void CLI::cmdVersion(int /*argc*/, char** /*argv*/) {
    printf("%s  version %s  (built %s %s)\r\n",
           PTZ_FIRMWARE_NAME, PTZ_VERSION_STRING,
           PTZ_BUILD_DATE, PTZ_BUILD_TIME);
}

// -----------------------------------------------------------------------------
// status
// -----------------------------------------------------------------------------

void CLI::cmdStatus(int /*argc*/, char** /*argv*/) {
    float panPos  = _motion.getPositionDeg(AxisId::PAN);
    float tiltPos = _motion.getPositionDeg(AxisId::TILT);
    bool  panMov  = _motion.isRunning(AxisId::PAN);
    bool  tiltMov = _motion.isRunning(AxisId::TILT);
    bool  panHom  = _motion.isHoming(AxisId::PAN);
    bool  tiltHom = _motion.isHoming(AxisId::TILT);

    MotionSettings s = _motion.getSettings();

    printf(
        "┌─ Pan  ────────────────────────────────────\r\n"
        "│  position : %8.2f °   %s\r\n"
        "│  limits   : [%.1f, %.1f] °  (%s)\r\n"
        "├─ Tilt ────────────────────────────────────\r\n"
        "│  position : %8.2f °   %s\r\n"
        "│  limits   : [%.1f, %.1f] °  (%s)\r\n"
        "├─ Motion ──────────────────────────────────\r\n"
        "│  max speed : %.1f °/s\r\n"
        "│  accel     : %.1f °/s²\r\n"
        "│  fine scale: %.2f\r\n"
        "└───────────────────────────────────────────\r\n",
        panPos,  panHom  ? "[HOMING]" : (panMov  ? "[moving]" : "[idle]"),
        s.panMinDeg, s.panMaxDeg, s.softLimitsEnabled ? "on" : "off",
        tiltPos, tiltHom ? "[HOMING]" : (tiltMov ? "[moving]" : "[idle]"),
        s.tiltMinDeg, s.tiltMaxDeg, s.softLimitsEnabled ? "on" : "off",
        s.maxSpeedDegS, s.accelDegS2, s.fineSpeedScale
    );
}

// -----------------------------------------------------------------------------
// home
// -----------------------------------------------------------------------------

void CLI::cmdHome(int argc, char** argv) {
    const char* target = (argc >= 2) ? argv[1] : "all";

    auto homeOne = [&](AxisId axis, const char* name) {
        printf("Homing %s... ", name);
        if (_motion.startHoming(axis)) {
            printf("started (poll 'status' to monitor)\r\n");
        } else {
            printf("FAILED — check UART connection\r\n");
        }
    };

    if (strcmp(target, "all") == 0) {
        homeOne(AxisId::PAN,  "pan");
        homeOne(AxisId::TILT, "tilt");
    } else {
        AxisId axis;
        if (!parseAxis(target, axis)) {
            printf("Unknown axis '%s'.  Use: pan, tilt, or all\r\n", target);
            return;
        }
        homeOne(axis, target);
    }
}

// -----------------------------------------------------------------------------
// move
// -----------------------------------------------------------------------------

void CLI::cmdMove(int argc, char** argv) {
    if (argc < 3) {
        print("Usage: move <pan|tilt> <degrees> [--relative]\r\n");
        return;
    }
    AxisId axis;
    if (!parseAxis(argv[1], axis)) {
        printf("Unknown axis '%s'\r\n", argv[1]);
        return;
    }
    float deg;
    if (!parseFloat(argv[2], deg)) {
        printf("Invalid angle '%s'\r\n", argv[2]);
        return;
    }
    bool relative = (argc >= 4 && strcmp(argv[3], "--relative") == 0);
    _motion.moveTo(axis, deg, relative);
    printf("Moving %s to %.2f ° (%s)\r\n",
           argv[1], deg, relative ? "relative" : "absolute");
}

// -----------------------------------------------------------------------------
// stop
// -----------------------------------------------------------------------------

void CLI::cmdStop(int argc, char** argv) {
    const char* target = (argc >= 2) ? argv[1] : "all";
    if (strcmp(target, "all") == 0) {
        _motion.stopAll();
        print("All axes stopping\r\n");
    } else {
        AxisId axis;
        if (!parseAxis(target, axis)) {
            printf("Unknown axis '%s'\r\n", target);
            return;
        }
        _motion.stop(axis);
        printf("Stopping %s\r\n", target);
    }
}

// -----------------------------------------------------------------------------
// estop
// -----------------------------------------------------------------------------

void CLI::cmdEstop(int /*argc*/, char** /*argv*/) {
    _motion.emergencyStop();
    print("EMERGENCY STOP — all axes halted\r\n"
          "Re-enable with: enable all\r\n");
}

// -----------------------------------------------------------------------------
// enable / disable
// -----------------------------------------------------------------------------

void CLI::cmdEnable(int argc, char** argv) {
    const char* target = (argc >= 2) ? argv[1] : "all";
    if (strcmp(target, "all") == 0) {
        _motion.enableAll(true);
        print("All axes enabled\r\n");
    } else {
        AxisId axis;
        if (!parseAxis(target, axis)) { printf("Unknown axis '%s'\r\n", target); return; }
        _motion.enableAxis(axis, true);
        printf("%s enabled\r\n", target);
    }
}

void CLI::cmdDisable(int argc, char** argv) {
    const char* target = (argc >= 2) ? argv[1] : "all";
    if (strcmp(target, "all") == 0) {
        _motion.enableAll(false);
        print("All axes disabled\r\n");
    } else {
        AxisId axis;
        if (!parseAxis(target, axis)) { printf("Unknown axis '%s'\r\n", target); return; }
        _motion.enableAxis(axis, false);
        printf("%s disabled\r\n", target);
    }
}

// -----------------------------------------------------------------------------
// set
// -----------------------------------------------------------------------------

void CLI::cmdSet(int argc, char** argv) {
    if (argc < 2) { print("Usage: set <speed|accel|fine|limits> ...\r\n"); return; }

    MotionSettings s = _motion.getSettings();
    const char* param = argv[1];

    if (strcmp(param, "speed") == 0) {
        if (argc < 3) { print("Usage: set speed <deg/s>\r\n"); return; }
        float v; if (!parseFloat(argv[2], v) || v <= 0) { print("Invalid value\r\n"); return; }
        s.maxSpeedDegS = v;
        _motion.applySettings(s);
        printf("Max speed set to %.2f °/s\r\n", v);

    } else if (strcmp(param, "accel") == 0) {
        if (argc < 3) { print("Usage: set accel <deg/s²>\r\n"); return; }
        float v; if (!parseFloat(argv[2], v) || v <= 0) { print("Invalid value\r\n"); return; }
        s.accelDegS2 = v;
        _motion.applySettings(s);
        printf("Acceleration set to %.2f °/s²\r\n", v);

    } else if (strcmp(param, "fine") == 0) {
        if (argc < 3) { print("Usage: set fine <0.0–1.0>\r\n"); return; }
        float v; if (!parseFloat(argv[2], v) || v <= 0 || v > 1.0f) { print("Invalid value\r\n"); return; }
        s.fineSpeedScale = v;
        _motion.applySettings(s);
        printf("Fine speed scale set to %.3f\r\n", v);

    } else if (strcmp(param, "limits") == 0) {
        if (argc < 3) { print("Usage: set limits <pan|tilt> <min> <max>  OR  set limits on|off\r\n"); return; }

        if (strcmp(argv[2], "on")  == 0) { s.softLimitsEnabled = true;  _motion.applySettings(s); print("Soft limits ON\r\n");  return; }
        if (strcmp(argv[2], "off") == 0) { s.softLimitsEnabled = false; _motion.applySettings(s); print("Soft limits OFF\r\n"); return; }

        if (argc < 5) { print("Usage: set limits <pan|tilt> <min> <max>\r\n"); return; }
        AxisId axis;
        if (!parseAxis(argv[2], axis)) { printf("Unknown axis '%s'\r\n", argv[2]); return; }
        float lo, hi;
        if (!parseFloat(argv[3], lo) || !parseFloat(argv[4], hi) || lo >= hi) {
            print("Invalid limits — min must be less than max\r\n"); return;
        }
        if (axis == AxisId::PAN)  { s.panMinDeg  = lo; s.panMaxDeg  = hi; }
        else                       { s.tiltMinDeg = lo; s.tiltMaxDeg = hi; }
        _motion.applySettings(s);
        printf("Limits for %s set to [%.1f, %.1f] °\r\n", argv[2], lo, hi);

    } else {
        printf("Unknown parameter '%s'\r\n", param);
    }
}

// -----------------------------------------------------------------------------
// get
// -----------------------------------------------------------------------------

void CLI::cmdGet(int argc, char** argv) {
    if (argc < 2) { print("Usage: get <position|speed|accel|limits|encoder>\r\n"); return; }

    const char* param = argv[1];
    MotionSettings s  = _motion.getSettings();

    if (strcmp(param, "position") == 0) {
        printf("pan  : %.3f °\r\ntilt : %.3f °\r\n",
               _motion.getPositionDeg(AxisId::PAN),
               _motion.getPositionDeg(AxisId::TILT));

    } else if (strcmp(param, "speed") == 0) {
        printf("max speed : %.2f °/s\r\n", s.maxSpeedDegS);

    } else if (strcmp(param, "accel") == 0) {
        printf("accel : %.2f °/s²\r\n", s.accelDegS2);

    } else if (strcmp(param, "limits") == 0) {
        printf("pan  : [%.1f, %.1f] °\r\ntilt : [%.1f, %.1f] °\r\nsoft limits: %s\r\n",
               s.panMinDeg, s.panMaxDeg, s.tiltMinDeg, s.tiltMaxDeg,
               s.softLimitsEnabled ? "on" : "off");

    } else if (strcmp(param, "encoder") == 0) {
        const char* axisStr = (argc >= 3) ? argv[2] : nullptr;
        auto readOne = [&](AxisId axis, const char* name) {
            float angle;
            if (_motion.readEncoderAngle(axis, angle)) {
                printf("%s encoder : %.2f °\r\n", name, angle);
            } else {
                printf("%s encoder : UART timeout\r\n", name);
            }
        };
        if (!axisStr || strcmp(axisStr, "pan")  == 0) readOne(AxisId::PAN,  "pan");
        if (!axisStr || strcmp(axisStr, "tilt") == 0) readOne(AxisId::TILT, "tilt");

    } else {
        printf("Unknown parameter '%s'\r\n", param);
    }
}

// -----------------------------------------------------------------------------
// ping
// -----------------------------------------------------------------------------

void CLI::cmdPing(int argc, char** argv) {
    const char* target = (argc >= 2) ? argv[1] : "all";
    auto pingOne = [&](AxisId axis, const char* name) {
        printf("ping %s ... %s\r\n", name,
               _motion.pingDriver(axis) ? "OK" : "TIMEOUT");
    };
    if (strcmp(target, "all") == 0) {
        pingOne(AxisId::PAN,  "pan");
        pingOne(AxisId::TILT, "tilt");
    } else {
        AxisId axis;
        if (!parseAxis(target, axis)) { printf("Unknown axis '%s'\r\n", target); return; }
        pingOne(axis, target);
    }
}

// -----------------------------------------------------------------------------
// cal
// -----------------------------------------------------------------------------

void CLI::cmdCal(int argc, char** argv) {
    const char* target = (argc >= 2) ? argv[1] : "all";
    auto calOne = [&](AxisId axis, const char* name) {
        printf("Calibrating %s encoder (motor will rotate one revolution)...\r\n", name);
        if (_motion.calibrateEncoder(axis)) {
            printf("%s calibration complete\r\n", name);
        } else {
            printf("%s calibration FAILED — check UART connection\r\n", name);
        }
    };
    if (strcmp(target, "all") == 0) {
        calOne(AxisId::PAN,  "pan");
        calOne(AxisId::TILT, "tilt");
    } else {
        AxisId axis;
        if (!parseAxis(target, axis)) { printf("Unknown axis '%s'\r\n", target); return; }
        calOne(axis, target);
    }
}

// -----------------------------------------------------------------------------
// save / reset / reboot
// -----------------------------------------------------------------------------

void CLI::cmdSave(int /*argc*/, char** /*argv*/) {
    _motion.saveSettings();
    print("Settings saved to flash\r\n");
}

void CLI::cmdReset(int /*argc*/, char** /*argv*/) {
    _motion.resetSettings();
    print("Settings restored to factory defaults (not saved — use 'save' to persist)\r\n");
}

void CLI::cmdDiag(int argc, char** argv) {
    // Send a read-status frame (0x3A) to one or both drivers and dump raw bytes.
    // This lets you verify wiring, baud rate, address, and checksum format.
    const char* target = (argc >= 2) ? argv[1] : "all";

    // Optional third argument: hex function code to test (default 0x3A status read)
    uint8_t funcCode = 0x3A;
    if (argc >= 3) {
        char* end;
        funcCode = (uint8_t)strtoul(argv[2], &end, 16);
    }

    auto diagOne = [&](AxisId axis, const char* name) {
        printf("--- diag %s (addr 0x%02X, func 0x%02X) ---\r\n", name,
               axis == AxisId::PAN ? MKS_PAN_ADDR : MKS_TILT_ADDR, funcCode);
        _motion.diagAxis(axis, funcCode, 300);
    };

    if (strcmp(target, "all") == 0 || strcmp(target, "pan")  == 0) diagOne(AxisId::PAN,  "pan");
    if (strcmp(target, "all") == 0 || strcmp(target, "tilt") == 0) diagOne(AxisId::TILT, "tilt");

    print(
        "\r\nInterpretation guide:\r\n"
        "  Nothing received    → wrong baud rate or TX/RX wired backwards\r\n"
        "  Addr mismatch       → driver address not 0xE0/0xE1; check onboard menu\r\n"
        "  tCHK SUM matches    → checksum correct (expected)\r\n"
        "  tCHK XOR matches    → checksum is XOR not SUM; firmware may differ\r\n"
        "  RX[1] = 0x3A        → driver echoes func code (adjust readResponse)\r\n"
        "  RX[1] = 0x01/0x02   → no func echo; response is [addr][data][chk]\r\n"
    );
}

void CLI::cmdReboot(int /*argc*/, char** /*argv*/) {
    print("Rebooting...\r\n");
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}
