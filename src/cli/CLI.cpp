// =============================================================================
// CLI.cpp
// =============================================================================

#include "CLI.h"
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
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

// Pi-only print — used for query responses that the Pi reads back directly.
void CLI::printToPi(const char* msg) {
    xSemaphoreTake(_txMutex, portMAX_DELAY);
    Serial2.print(msg);
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
    cli->printf("\r\n%s v%s\r\nType 'help' for commands.  'jog' for keyboard control.\r\n",
                PTZ_FIRMWARE_NAME, PTZ_VERSION_STRING);
    cli->printPrompt();

    for (;;) {
        cli->readSerial();
        cli->readPiSerial();
        vTaskDelay(pdMS_TO_TICKS(CLI_TASK_PERIOD_MS));
    }
}

// -----------------------------------------------------------------------------
// Serial reader
// -----------------------------------------------------------------------------

void CLI::readSerial() {
    while (Serial.available()) {
        char c = (char)Serial.read();

        xSemaphoreTake(_txMutex, portMAX_DELAY);
        if (c == '\r' || c == '\n') {
            Serial.print("\r\n");
        } else if (c == 0x7F || c == '\b') {
            if (_len > 0) { _len--; Serial.print("\b \b"); }
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
// Pi UART reader — no echo, no prompt; dispatch on newline
// -----------------------------------------------------------------------------

void CLI::readPiSerial() {
    while (Serial2.available()) {
        char c = (char)Serial2.read();
        if (c == '\r' || c == '\n') {
            _piBuf[_piLen] = '\0';
            if (_piLen > 0) dispatch(_piBuf);
            _piLen = 0;
        } else if (_piLen < CLI_MAX_LINE - 1) {
            _piBuf[_piLen++] = c;
        }
    }
}

// -----------------------------------------------------------------------------
// Tokenise and dispatch
// -----------------------------------------------------------------------------

void CLI::dispatch(char* line) {
    while (*line == ' ') line++;
    if (*line == '\0' || *line == '#') return;

    char*  argv[CLI_MAX_ARGS];
    int    argc = 0;
    char*  p    = line;

    while (*p && argc < CLI_MAX_ARGS) {
        while (*p == ' ') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
    }
    if (argc == 0) return;

    const char* cmd = argv[0];
    if      (strcmp(cmd, "help")    == 0) cmdHelp   (argc, argv);
    else if (strcmp(cmd, "version") == 0) cmdVersion(argc, argv);
    else if (strcmp(cmd, "status")  == 0) cmdStatus (argc, argv);
    else if (strcmp(cmd, "jog")     == 0) cmdJog    (argc, argv);
    else if (strcmp(cmd, "vel")     == 0) cmdVel    (argc, argv);
    else if (strcmp(cmd, "move")    == 0) cmdMove   (argc, argv);
    else if (strcmp(cmd, "stop")    == 0) cmdStop   (argc, argv);
    else if (strcmp(cmd, "estop")   == 0) cmdEstop  (argc, argv);
    else if (strcmp(cmd, "enable")  == 0) cmdEnable (argc, argv);
    else if (strcmp(cmd, "disable") == 0) cmdDisable(argc, argv);
    else if (strcmp(cmd, "set")     == 0) cmdSet    (argc, argv);
    else if (strcmp(cmd, "get")     == 0) cmdGet    (argc, argv);
    else if (strcmp(cmd, "ping")    == 0) cmdPing   (argc, argv);
    else if (strcmp(cmd, "save")    == 0) cmdSave   (argc, argv);
    else if (strcmp(cmd, "reset")   == 0) cmdReset  (argc, argv);
    else if (strcmp(cmd, "reboot")  == 0) cmdReboot (argc, argv);
    else printf("Unknown command: '%s'  (type 'help')\r\n", cmd);
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
    (void)argc; (void)argv;
    print(
        "\r\nPTZ Controller — commands\r\n"
        "──────────────────────────────────────────\r\n"
        "  help                        This list\r\n"
        "  version                     Firmware version\r\n"
        "  status                      Position and motion state\r\n"
        "\r\n"
        "  jog  [speed]                Keyboard control (WASD)\r\n"
        "  vel  <pan|tilt|all> <°/s>   Continuous velocity\r\n"
        "  move <pan|tilt> <°> [--relative]\r\n"
        "  stop [pan|tilt|all]\r\n"
        "  estop                       Hard stop (clear with 'enable all')\r\n"
        "\r\n"
        "  enable  [pan|tilt|all]\r\n"
        "  disable [pan|tilt|all]\r\n"
        "\r\n"
        "  set speed  <°/s>\r\n"
        "  set accel  <°/s²>\r\n"
        "  set fine   <0–1>            Fine-mode speed scale (used in jog)\r\n"
        "  set limits <pan|tilt> <min> <max>\r\n"
        "  set limits on|off\r\n"
        "  get position|speed|accel|limits\r\n"
        "\r\n"
        "  save                        Save settings to flash\r\n"
        "  reset                       Restore defaults (RAM only)\r\n"
        "  reboot\r\n"
        "──────────────────────────────────────────\r\n"
    );
}

// -----------------------------------------------------------------------------
// version
// -----------------------------------------------------------------------------

void CLI::cmdVersion(int /*argc*/, char** /*argv*/) {
    printf("%s  v%s  (built %s %s)\r\n",
           PTZ_FIRMWARE_NAME, PTZ_VERSION_STRING,
           PTZ_BUILD_DATE, PTZ_BUILD_TIME);
}

// -----------------------------------------------------------------------------
// status
// -----------------------------------------------------------------------------

void CLI::cmdStatus(int /*argc*/, char** /*argv*/) {
    float panPos  = _motion.getPositionDeg(AxisId::PAN);
    float tiltPos = _motion.getPositionDeg(AxisId::TILT);
    MotionSettings s = _motion.getSettings();

    printf(
        "┌─ Pan  ──────────────────────────────\r\n"
        "│  pos  : %8.2f °   [%s]\r\n"
        "│  limits: [%.1f, %.1f] ° (%s)\r\n"
        "├─ Tilt ──────────────────────────────\r\n"
        "│  pos  : %8.2f °   [%s]\r\n"
        "│  limits: [%.1f, %.1f] ° (%s)\r\n"
        "├─ Motion ────────────────────────────\r\n"
        "│  speed : %.1f °/s\r\n"
        "│  accel : %.1f °/s²\r\n"
        "│  fine  : %.2f×  e-stop: %s\r\n"
        "└─────────────────────────────────────\r\n",
        panPos,  _motion.isRunning(AxisId::PAN)  ? "moving" : "idle",
        s.panMinDeg,  s.panMaxDeg,  s.softLimitsEnabled ? "on" : "off",
        tiltPos, _motion.isRunning(AxisId::TILT) ? "moving" : "idle",
        s.tiltMinDeg, s.tiltMaxDeg, s.softLimitsEnabled ? "on" : "off",
        s.maxSpeedDegS, s.accelDegS2, s.fineSpeedScale,
        _motion.isEstopped() ? "YES" : "no"
    );
}

// -----------------------------------------------------------------------------
// jog — real-time WASD keyboard control
//
//   W/S  = tilt ±     A/D  = pan ±
//   F    = fine speed (toggle)
//   +/-  = adjust speed
//   SPC  = stop all
//   Q or ESC = exit jog mode
//
// Works with any serial terminal's key-repeat: as long as a key arrives
// within JOG_KEY_TIMEOUT_MS the axis keeps moving; releasing the key stops it.
// -----------------------------------------------------------------------------

void CLI::cmdJog(int argc, char** argv) {
    MotionSettings s = _motion.getSettings();
    float speed = s.maxSpeedDegS;
    if (argc >= 2) parseFloat(argv[1], speed);

    bool fineMode = false;

    printf(
        "\r\n── Jog mode ───────────────────────────────────────────────\r\n"
        "  W/S = tilt ±     A/D = pan ±     SPC = stop\r\n"
        "  F   = toggle fine (%.0f%%×)     +/- = adjust speed\r\n"
        "  Q or ESC = exit\r\n"
        "  Speed: %.0f °/s\r\n"
        "───────────────────────────────────────────────────────────\r\n",
        s.fineSpeedScale * 100.0f, speed);

    uint32_t panLastMs  = 0;
    uint32_t tiltLastMs = 0;

    for (;;) {
        if (Serial.available()) {
            char c = (char)Serial.read();
            uint32_t now = millis();

            float v = fineMode ? (speed * s.fineSpeedScale) : speed;

            switch (c) {
            case 'w': case 'W':
                tiltLastMs = now;
                _motion.setVelocity(AxisId::TILT,  v);
                break;
            case 's': case 'S':
                tiltLastMs = now;
                _motion.setVelocity(AxisId::TILT, -v);
                break;
            case 'a': case 'A':
                panLastMs = now;
                _motion.setVelocity(AxisId::PAN,  -v);
                break;
            case 'd': case 'D':
                panLastMs = now;
                _motion.setVelocity(AxisId::PAN,   v);
                break;
            case ' ':
                _motion.stopAll();
                panLastMs = tiltLastMs = 0;
                print("\r  [stopped]          ");
                break;
            case 'f': case 'F':
                fineMode = !fineMode;
                printf("\r  Fine mode %s (%.0f °/s)    ",
                       fineMode ? "ON" : "off",
                       fineMode ? speed * s.fineSpeedScale : speed);
                break;
            case '+': case '=':
                speed = fminf(speed + 10.0f, 360.0f);
                printf("\r  Speed: %.0f °/s        ", speed);
                break;
            case '-': case '_':
                speed = fmaxf(speed - 10.0f, 5.0f);
                printf("\r  Speed: %.0f °/s        ", speed);
                break;
            case 'q': case 'Q': case 0x1B:
                _motion.stopAll();
                print("\r\n── Exiting jog mode ──\r\n");
                printPrompt();
                return;
            default:
                break;
            }
        }

        // Key-repeat timeout: stop axis if no key arrived within the window.
        uint32_t now = millis();
        if (panLastMs  && (now - panLastMs)  > JOG_KEY_TIMEOUT_MS) {
            _motion.stop(AxisId::PAN);
            panLastMs = 0;
        }
        if (tiltLastMs && (now - tiltLastMs) > JOG_KEY_TIMEOUT_MS) {
            _motion.stop(AxisId::TILT);
            tiltLastMs = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// -----------------------------------------------------------------------------
// vel — continuous velocity (Pi tracking use)
// -----------------------------------------------------------------------------

void CLI::cmdVel(int argc, char** argv) {
    if (argc < 3) { print("Usage: vel <pan|tilt|all> <deg/s>\r\n"); return; }
    float degS;
    if (!parseFloat(argv[2], degS)) { printf("Invalid speed '%s'\r\n", argv[2]); return; }
    const char* t = argv[1];
    if (strcmp(t, "all") == 0) {
        _motion.setVelocity(AxisId::PAN,  degS);
        _motion.setVelocity(AxisId::TILT, degS);
    } else {
        AxisId axis;
        if (!parseAxis(t, axis)) { printf("Unknown axis '%s'\r\n", t); return; }
        _motion.setVelocity(axis, degS);
    }
}

// -----------------------------------------------------------------------------
// move
// -----------------------------------------------------------------------------

void CLI::cmdMove(int argc, char** argv) {
    if (argc < 3) { print("Usage: move <pan|tilt> <degrees> [--relative]\r\n"); return; }
    AxisId axis;
    if (!parseAxis(argv[1], axis)) { printf("Unknown axis '%s'\r\n", argv[1]); return; }
    float deg;
    if (!parseFloat(argv[2], deg)) { printf("Invalid angle '%s'\r\n", argv[2]); return; }
    bool relative = (argc >= 4 && strcmp(argv[3], "--relative") == 0);
    _motion.moveTo(axis, deg, relative);
    printf("Moving %s to %.2f° (%s)\r\n", argv[1], deg, relative ? "relative" : "absolute");
}

// -----------------------------------------------------------------------------
// stop / estop
// -----------------------------------------------------------------------------

void CLI::cmdStop(int argc, char** argv) {
    const char* t = (argc >= 2) ? argv[1] : "all";
    if (strcmp(t, "all") == 0) {
        _motion.stopAll();
        print("All axes stopping\r\n");
    } else {
        AxisId axis;
        if (!parseAxis(t, axis)) { printf("Unknown axis '%s'\r\n", t); return; }
        _motion.stop(axis);
        printf("Stopping %s\r\n", t);
    }
}

void CLI::cmdEstop(int /*argc*/, char** /*argv*/) {
    _motion.emergencyStop();
    print("EMERGENCY STOP\r\nClear with: estop  then  enable all\r\n");
}

// Note: typing 'estop' again clears it (toggle behaviour is handled here).
// Actually re-reading: let's make estop a pure latch and clearEstop separate.
// For simplicity: 'estop' always triggers; 'enable all' clears the latch + re-enables.
// Override cmdEstop to clear if already stopped:
// (kept simple — see above)

// -----------------------------------------------------------------------------
// enable / disable
// -----------------------------------------------------------------------------

void CLI::cmdEnable(int argc, char** argv) {
    const char* t = (argc >= 2) ? argv[1] : "all";
    if (strcmp(t, "all") == 0) {
        _motion.clearEstop();
        _motion.enableAll(true);
        print("All axes enabled\r\n");
    } else {
        AxisId axis;
        if (!parseAxis(t, axis)) { printf("Unknown axis '%s'\r\n", t); return; }
        _motion.enableAxis(axis, true);
        printf("%s enabled\r\n", t);
    }
}

void CLI::cmdDisable(int argc, char** argv) {
    const char* t = (argc >= 2) ? argv[1] : "all";
    if (strcmp(t, "all") == 0) {
        _motion.enableAll(false);
        print("All axes disabled\r\n");
    } else {
        AxisId axis;
        if (!parseAxis(t, axis)) { printf("Unknown axis '%s'\r\n", t); return; }
        _motion.enableAxis(axis, false);
        printf("%s disabled\r\n", t);
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
        printf("Max speed: %.1f °/s\r\n", v);

    } else if (strcmp(param, "accel") == 0) {
        if (argc < 3) { print("Usage: set accel <deg/s²>\r\n"); return; }
        float v; if (!parseFloat(argv[2], v) || v <= 0) { print("Invalid value\r\n"); return; }
        s.accelDegS2 = v;
        _motion.applySettings(s);
        printf("Accel: %.1f °/s²\r\n", v);

    } else if (strcmp(param, "fine") == 0) {
        if (argc < 3) { print("Usage: set fine <0.0–1.0>\r\n"); return; }
        float v; if (!parseFloat(argv[2], v) || v <= 0 || v > 1.0f) { print("Invalid value\r\n"); return; }
        s.fineSpeedScale = v;
        _motion.applySettings(s);
        printf("Fine scale: %.2f\r\n", v);

    } else if (strcmp(param, "limits") == 0) {
        if (argc < 3) { print("Usage: set limits <pan|tilt> <min> <max>  or  set limits on|off\r\n"); return; }
        if (strcmp(argv[2], "on")  == 0) { s.softLimitsEnabled = true;  _motion.applySettings(s); print("Soft limits ON\r\n");  return; }
        if (strcmp(argv[2], "off") == 0) { s.softLimitsEnabled = false; _motion.applySettings(s); print("Soft limits OFF\r\n"); return; }
        if (argc < 5) { print("Usage: set limits <pan|tilt> <min> <max>\r\n"); return; }
        AxisId axis;
        if (!parseAxis(argv[2], axis)) { printf("Unknown axis '%s'\r\n", argv[2]); return; }
        float lo, hi;
        if (!parseFloat(argv[3], lo) || !parseFloat(argv[4], hi) || lo >= hi) {
            print("Invalid: min must be less than max\r\n"); return;
        }
        if (axis == AxisId::PAN)  { s.panMinDeg  = lo; s.panMaxDeg  = hi; }
        else                       { s.tiltMinDeg = lo; s.tiltMaxDeg = hi; }
        _motion.applySettings(s);
        printf("Limits %s: [%.1f, %.1f] °\r\n", argv[2], lo, hi);

    } else {
        printf("Unknown parameter '%s'\r\n", param);
    }
}

// -----------------------------------------------------------------------------
// get
// -----------------------------------------------------------------------------

void CLI::cmdGet(int argc, char** argv) {
    if (argc < 2) { print("Usage: get <position|speed|accel|limits>\r\n"); return; }
    MotionSettings s = _motion.getSettings();
    const char* param = argv[1];

    if (strcmp(param, "position") == 0) {
        char posBuf[64];
        snprintf(posBuf, sizeof(posBuf), "pan  : %.3f °\r\ntilt : %.3f °\r\n",
                 _motion.getPositionDeg(AxisId::PAN),
                 _motion.getPositionDeg(AxisId::TILT));
        print(posBuf);      // USB
        printToPi(posBuf);  // Pi UART — needed for bridge.query("get position")
    } else if (strcmp(param, "speed") == 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "max speed : %.2f °/s\r\n", s.maxSpeedDegS);
        print(buf);
        printToPi(buf);
    } else if (strcmp(param, "accel") == 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "accel : %.2f °/s²\r\n", s.accelDegS2);
        print(buf);
        printToPi(buf);
    } else if (strcmp(param, "limits") == 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "pan : [%.1f, %.1f] °\r\ntilt: [%.1f, %.1f] °\r\nlimits: %s\r\n",
               s.panMinDeg, s.panMaxDeg, s.tiltMinDeg, s.tiltMaxDeg,
               s.softLimitsEnabled ? "on" : "off");
        print(buf);
        printToPi(buf);
    } else {
        printf("Unknown parameter '%s'\r\n", param);
    }
}

// -----------------------------------------------------------------------------
// ping — UART link test, responds on both USB and Serial2
// -----------------------------------------------------------------------------

void CLI::cmdPing(int argc, char** argv) {
    const char* t = (argc >= 2) ? argv[1] : "all";
    char buf[64];
    if (strcmp(t, "all") == 0) {
        snprintf(buf, sizeof(buf), "ping pan ... OK\r\nping tilt ... OK\r\n");
    } else {
        snprintf(buf, sizeof(buf), "ping %s ... OK\r\n", t);
    }
    print(buf);
    printToPi(buf);
}

// -----------------------------------------------------------------------------
// save / reset / reboot
// -----------------------------------------------------------------------------

void CLI::cmdSave(int /*argc*/, char** /*argv*/) {
    _motion.saveSettings();
    print("Settings saved\r\n");
}

void CLI::cmdReset(int /*argc*/, char** /*argv*/) {
    _motion.resetSettings();
    print("Settings reset to defaults (not saved — use 'save' to persist)\r\n");
}

void CLI::cmdReboot(int /*argc*/, char** /*argv*/) {
    print("Rebooting...\r\n");
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}
