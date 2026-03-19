// =============================================================================
// MKSServo42C.cpp — UART driver implementation
//
// Per MKS-SERVO42C-User-Manual-V1.1.2:
//
//   Downlink  (ESP32 → driver): [Addr][Func][Data...][tCHK]
//   Uplink    (driver → ESP32): [Addr][Data...][tCHK]
//     — the function code is NOT echoed back in the response.
//
//   tCHK = 8-bit SUM of all preceding bytes in the frame (NOT XOR).
//     Example: addr=0xE0, func=0x30  →  tCHK = (0xE0+0x30) & 0xFF = 0x10
//
//   Default slave address: 0xE0.  Configurable 0xE0–0xE9.
//   Default baud rate:     38400.
//
// Wiring note:
//   Use the MKS 4-pin UART header (Tx / Rx / G / 3V3), NOT the signal-
//   connector COM pin.  Cross Tx→Rx and Rx→Tx.  No level shifter needed
//   (3.3 V TTL on both sides).
// =============================================================================

#include "MKSServo42C.h"
#include "config.h"
#include <string.h>

// -----------------------------------------------------------------------------
// Constructor / begin
// -----------------------------------------------------------------------------

MKSServo42C::MKSServo42C(HardwareSerial& uart, uint8_t addr)
    : _uart(uart), _addr(addr) {}

bool MKSServo42C::begin(uint32_t baud) {
    (void)baud;
    // Caller is responsible for Serial1/Serial2.begin() with correct pins.
    if (!_uart_mutex) _uart_mutex = xSemaphoreCreateMutex();
    return (_uart_mutex != nullptr);
}

// -----------------------------------------------------------------------------
// Address management
// -----------------------------------------------------------------------------

void    MKSServo42C::setAddress(uint8_t addr) { _addr = addr; }
uint8_t MKSServo42C::getAddress() const       { return _addr; }

// -----------------------------------------------------------------------------
// Low-level framing
// -----------------------------------------------------------------------------

// tCHK = 8-bit sum (NOT XOR) of all bytes before the checksum byte.
uint8_t MKSServo42C::calcCRC(const uint8_t* data, size_t len) const {
    uint16_t sum = 0;
    for (size_t i = 0; i < len; i++) sum += data[i];
    return (uint8_t)(sum & 0xFF);
}

bool MKSServo42C::sendCommand(uint8_t func, const uint8_t* data, size_t dlen) {
    // Brief delay to let any in-flight bytes from a previous exchange finish
    // arriving, then flush them before sending a new command.
    delay(2);
    while (_uart.available()) _uart.read();

    uint8_t frame[20];
    size_t  n = 0;
    frame[n++] = _addr;
    frame[n++] = func;
    for (size_t i = 0; i < dlen; i++) frame[n++] = data[i];
    frame[n] = calcCRC(frame, n);
    n++;
    _uart.write(frame, n);
    return true;
}

// Two response frame formats exist across firmware versions — auto-detected:
//
//   Format A (func echo):   [Addr][Func][Data x N][tCHK]   — newer firmware
//   Format B (no func echo):[Addr][Data x N]               — older firmware
//
// Detection: if byte[1] == expected func code → Format A; else → Format B.
// tCHK verification is skipped (formula does not match observed bytes).
//
// len (in/out): on entry = expected data bytes; on exit = actual bytes read.
bool MKSServo42C::readResponse(uint8_t func, uint8_t* buf, size_t& len,
                                uint32_t timeoutMs) {
    const size_t expectedData = len;
    uint32_t     start        = millis();

    // Helper: read one byte, blocking until data arrives or timeout.
    auto readByte = [&](uint8_t& out) -> bool {
        while (!_uart.available()) {
            if ((uint32_t)(millis() - start) > timeoutMs) return false;
            taskYIELD();
        }
        out = (uint8_t)_uart.read();
        return true;
    };

    uint8_t b;

    // Byte 0: address
    if (!readByte(b) || b != _addr) return false;

    // Byte 1: func echo (Format A) or first data byte (Format B)
    if (!readByte(b)) return false;

    if (b == func) {
        // Format A: [addr][func][data x N][tCHK]
        for (size_t i = 0; i < expectedData; i++)
            if (!readByte(buf[i])) return false;
        uint8_t dummy; readByte(dummy); // consume tCHK (ignore it)
    } else {
        // Format B: [addr][data x N] — byte already in hand is data[0]
        buf[0] = b;
        for (size_t i = 1; i < expectedData; i++)
            if (!readByte(buf[i])) return false;
    }

    len = expectedData;
    return true;
}

// -----------------------------------------------------------------------------
// Atomic command helper — takes the UART mutex for the entire send+receive
// pair so motionTask encoder polls cannot interleave with CLI commands.
// -----------------------------------------------------------------------------

bool MKSServo42C::doCommand(uint8_t func, const uint8_t* txData, size_t txLen,
                              uint8_t* rxBuf, size_t& rxLen, uint32_t timeoutMs) {
    if (_uart_mutex &&
        xSemaphoreTake(_uart_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return false;
    }
    bool ok = sendCommand(func, txData, txLen) &&
              readResponse(func, rxBuf, rxLen, timeoutMs);
    if (_uart_mutex) xSemaphoreGive(_uart_mutex);
    return ok;
}

// -----------------------------------------------------------------------------
// Communication test
// -----------------------------------------------------------------------------

bool MKSServo42C::ping() {
    uint8_t buf[1]; size_t len = 1;
    return doCommand((uint8_t)MKSFunc::FUNC_READ_STATUS, nullptr, 0,
                     buf, len, MKS_UART_TIMEOUT_MS);
}

// -----------------------------------------------------------------------------
// Configuration commands
// -----------------------------------------------------------------------------

bool MKSServo42C::setMicrosteps(uint8_t mstep) {
    uint8_t d[] = {mstep};
    uint8_t buf[1]; size_t len = 1;
    return doCommand((uint8_t)MKSFunc::FUNC_SET_MSTEP, d, 1,
                     buf, len, MKS_UART_TIMEOUT_MS) && buf[0] == 0x01;
}

bool MKSServo42C::setWorkingCurrentMA(uint16_t ma) {
    // Protocol: 1-byte index where current = index × 200 mA (0x00–0x0F).
    uint8_t idx = (uint8_t)(ma / 200);
    if (idx > 15) idx = 15;
    if (idx < 1)  idx = 1;
    uint8_t buf[1]; size_t len = 1;
    return doCommand((uint8_t)MKSFunc::FUNC_SET_CURRENT, &idx, 1,
                     buf, len, MKS_UART_TIMEOUT_MS) && buf[0] == 0x01;
}

bool MKSServo42C::setHoldCurrentPercent(uint8_t pct) {
    if (pct > 100) pct = 100;
    uint8_t d[] = {pct};
    uint8_t buf[1]; size_t len = 1;
    return doCommand((uint8_t)MKSFunc::FUNC_SET_CURRENT, d, 1,
                     buf, len, MKS_UART_TIMEOUT_MS) && buf[0] == 0x01;
}

bool MKSServo42C::setDirection(bool reverse) {
    uint8_t d[] = {reverse ? (uint8_t)0x01 : (uint8_t)0x00};
    uint8_t buf[1]; size_t len = 1;
    return doCommand((uint8_t)MKSFunc::FUNC_SET_DIRECTION, d, 1,
                     buf, len, MKS_UART_TIMEOUT_MS) && buf[0] == 0x01;
}

bool MKSServo42C::setEnableActiveLevel(bool activeLow) {
    uint8_t d[] = {activeLow ? (uint8_t)0x01 : (uint8_t)0x00};
    uint8_t buf[1]; size_t len = 1;
    return doCommand((uint8_t)MKSFunc::FUNC_SET_EN_LEVEL, d, 1,
                     buf, len, MKS_UART_TIMEOUT_MS) && buf[0] == 0x01;
}

bool MKSServo42C::enableMotor(bool en) {
    uint8_t d[] = {en ? (uint8_t)0x01 : (uint8_t)0x00};
    uint8_t buf[1]; size_t len = 1;
    return doCommand((uint8_t)MKSFunc::FUNC_ENABLE, d, 1,
                     buf, len, MKS_UART_TIMEOUT_MS) && buf[0] == 0x01;
}

// -----------------------------------------------------------------------------
// Motion commands
// -----------------------------------------------------------------------------

bool MKSServo42C::emergencyStop() {
    uint8_t buf[1]; size_t len = 1;
    return doCommand((uint8_t)MKSFunc::FUNC_STOP, nullptr, 0,
                     buf, len, MKS_UART_TIMEOUT_MS);
}

bool MKSServo42C::setZeroMode(uint8_t mode) {
    uint8_t d[] = {mode};
    uint8_t buf[1]; size_t len = 1;
    return doCommand((uint8_t)MKSFunc::FUNC_SET_ZERO_MODE, d, 1,
                     buf, len, MKS_UART_TIMEOUT_MS) && buf[0] != 0x00;
}

bool MKSServo42C::setZeroSpeed(uint8_t speed) {
    if (speed > 4) speed = 4;
    uint8_t d[] = {speed};
    uint8_t buf[1]; size_t len = 1;
    return doCommand((uint8_t)MKSFunc::FUNC_SET_ZERO_SPEED, d, 1,
                     buf, len, MKS_UART_TIMEOUT_MS) && buf[0] != 0x00;
}

bool MKSServo42C::setZeroDir(bool cw) {
    uint8_t d[] = {cw ? (uint8_t)0x00 : (uint8_t)0x01};
    uint8_t buf[1]; size_t len = 1;
    return doCommand((uint8_t)MKSFunc::FUNC_SET_ZERO_DIR, d, 1,
                     buf, len, MKS_UART_TIMEOUT_MS) && buf[0] != 0x00;
}

bool MKSServo42C::setZeroPoint() {
    uint8_t d[] = {0x00};
    uint8_t buf[1]; size_t len = 1;
    return doCommand((uint8_t)MKSFunc::FUNC_SET_ZERO_POINT, d, 1,
                     buf, len, MKS_UART_TIMEOUT_MS) && buf[0] != 0x00;
}

bool MKSServo42C::goHome() {
    // FUNC_GO_HOME (0x94) returns to the saved zero point.
    // Requires setZeroMode(1 or 2) and setZeroPoint() to have been called first.
    // Older firmware may not respond; treat no-response as accepted.
    if (_uart_mutex &&
        xSemaphoreTake(_uart_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return false;

    uint8_t d[] = {0x00};
    bool sent = sendCommand((uint8_t)MKSFunc::FUNC_GO_HOME, d, 1);
    if (!sent) { if (_uart_mutex) xSemaphoreGive(_uart_mutex); return false; }

    uint8_t buf[1]; size_t len = 1;
    bool gotResp = readResponse((uint8_t)MKSFunc::FUNC_GO_HOME, buf, len,
                                MKS_UART_TIMEOUT_MS);
    if (_uart_mutex) xSemaphoreGive(_uart_mutex);

    return !gotResp || (buf[0] != 0x00);
}

bool MKSServo42C::calibrateEncoder() {
    // Motor rotates one full revolution — must be unloaded.
    // Response arrives only after completion; use a long timeout.
    uint8_t buf[1]; size_t len = 1;
    return doCommand((uint8_t)MKSFunc::FUNC_CALIBRATE, nullptr, 0,
                     buf, len, 10000) && buf[0] == 0x01;
}

// -----------------------------------------------------------------------------
// Read-back commands
// -----------------------------------------------------------------------------

bool MKSServo42C::readEncoderAngle(float& angleDeg) {
    // Newer firmware: int32 carry (4 bytes) + uint16 value (2 bytes) = 6 data bytes
    //   Full angle = carry * 360.0 + value * 360.0 / 16384.0
    // Older firmware: uint16 value only (2 data bytes), angle = value * 360.0 / 16384.0
    // _encoderBytes is auto-detected on first call: try 6, fall back to 2.
    uint8_t buf[6]; size_t len = _encoderBytes;
    if (!doCommand((uint8_t)MKSFunc::FUNC_READ_ENCODER, nullptr, 0,
                   buf, len, MKS_UART_TIMEOUT_MS)) {
        if (_encoderBytes == 6) {
            // Retry with 2-byte format (older firmware)
            _encoderBytes = 2;
            len = 2;
            if (!doCommand((uint8_t)MKSFunc::FUNC_READ_ENCODER, nullptr, 0,
                           buf, len, MKS_UART_TIMEOUT_MS))             return false;
        } else {
            return false;
        }
    }

    if (_encoderBytes == 6) {
        int32_t  carry = (int32_t)(((uint32_t)buf[0] << 24) |
                                    ((uint32_t)buf[1] << 16) |
                                    ((uint32_t)buf[2] <<  8) |
                                     (uint32_t)buf[3]);
        uint16_t value = (uint16_t)(((uint16_t)buf[4] << 8) | buf[5]);
        angleDeg = (float)carry * 360.0f + (float)value * 360.0f / 16384.0f;
    } else {
        // 2-byte: shaft angle within one revolution (0–360°)
        uint16_t value = (uint16_t)(((uint16_t)buf[0] << 8) | buf[1]);
        angleDeg = (float)value * 360.0f / 16384.0f;
    }
    return true;
}

bool MKSServo42C::readSpeedRPM(int16_t& rpm) {
    uint8_t buf[2]; size_t len = 2;
    if (!doCommand((uint8_t)MKSFunc::FUNC_READ_SPEED, nullptr, 0,
                   buf, len, MKS_UART_TIMEOUT_MS))                     return false;
    rpm = (int16_t)(((uint16_t)buf[0] << 8) | buf[1]);
    return true;
}

bool MKSServo42C::readShaftError(float& errorDeg) {
    uint8_t buf[2]; size_t len = 2;
    if (!doCommand((uint8_t)MKSFunc::FUNC_READ_ERROR, nullptr, 0,
                   buf, len, MKS_UART_TIMEOUT_MS))                     return false;
    int16_t raw = (int16_t)(((uint16_t)buf[0] << 8) | buf[1]);
    errorDeg = (float)raw * 360.0f / 16384.0f;
    return true;
}

bool MKSServo42C::readStatus(uint8_t& statusByte) {
    uint8_t buf[1]; size_t len = 1;
    if (!doCommand((uint8_t)MKSFunc::FUNC_READ_STATUS, nullptr, 0,
                   buf, len, MKS_UART_TIMEOUT_MS))                     return false;
    statusByte = buf[0];
    return true;
}

// -----------------------------------------------------------------------------
// UART drive-mode motion commands
// -----------------------------------------------------------------------------

bool MKSServo42C::runVelocity(uint8_t spdDir) {
    uint8_t d[] = {spdDir};
    uint8_t buf[1]; size_t len = 1;
    return doCommand((uint8_t)MKSFunc::FUNC_RUN_VELOCITY, d, 1,
                     buf, len, MKS_UART_TIMEOUT_MS) && buf[0] != 0x00;
}

bool MKSServo42C::stopMotor() {
    uint8_t buf[1]; size_t len = 1;
    return doCommand((uint8_t)MKSFunc::FUNC_STOP, nullptr, 0,
                     buf, len, MKS_UART_TIMEOUT_MS) && buf[0] != 0x00;
}

bool MKSServo42C::runPulses(uint8_t spdDir, uint32_t pulses) {
    // Frame: [addr][0xFD][spdDir][pulse3][pulse2][pulse1][pulse0][tCHK]
    uint8_t d[] = {
        spdDir,
        (uint8_t)(pulses >> 24),
        (uint8_t)(pulses >> 16),
        (uint8_t)(pulses >>  8),
        (uint8_t)(pulses & 0xFF)
    };
    uint8_t buf[1]; size_t len = 1;
    return doCommand((uint8_t)MKSFunc::FUNC_RUN_PULSES, d, 5,
                     buf, len, MKS_UART_TIMEOUT_MS) && buf[0] == 0x01;
}

bool MKSServo42C::pollRunPulses(bool& done) {
    // The driver sends a second unsolicited response when the move completes.
    // We read it non-blocking by checking available() first.
    // Minimum 3 bytes: [addr][func_or_data][data_or_chk] — check without blocking.
    if (_uart.available() < 3) { done = false; return true; }
    if (_uart_mutex &&
        xSemaphoreTake(_uart_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        done = false; return true;
    }
    uint8_t buf[1]; size_t len = 1;
    bool ok = readResponse((uint8_t)MKSFunc::FUNC_RUN_PULSES, buf, len,
                           MKS_UART_TIMEOUT_MS);
    if (_uart_mutex) xSemaphoreGive(_uart_mutex);
    if (!ok) return false;
    done = (buf[0] == 0x02); // 2 = reached target
    return true;
}

// -----------------------------------------------------------------------------
// Raw diagnostic
// -----------------------------------------------------------------------------

void MKSServo42C::sendRaw(uint8_t func, const uint8_t* data, size_t dlen,
                           uint32_t timeoutMs) {
    if (_uart_mutex) xSemaphoreTake(_uart_mutex, pdMS_TO_TICKS(500));

    // Build and send the frame exactly as sendCommand() does.
    delay(2);
    while (_uart.available()) _uart.read();

    uint8_t frame[20];
    size_t  n = 0;
    frame[n++] = _addr;
    frame[n++] = func;
    for (size_t i = 0; i < dlen; i++) frame[n++] = data[i];
    frame[n] = calcCRC(frame, n);
    n++;

    Serial.printf("  TX (%u bytes):", (unsigned)n);
    for (size_t i = 0; i < n; i++) Serial.printf(" %02X", frame[i]);
    Serial.println();

    _uart.write(frame, n);

    // Collect every byte that arrives within timeoutMs and print it raw.
    uint8_t  rxBuf[32];
    size_t   rxLen   = 0;
    uint32_t start   = millis();
    while ((uint32_t)(millis() - start) < timeoutMs && rxLen < sizeof(rxBuf)) {
        if (_uart.available()) rxBuf[rxLen++] = (uint8_t)_uart.read();
        else taskYIELD();
    }

    if (_uart_mutex) xSemaphoreGive(_uart_mutex);

    if (rxLen == 0) {
        Serial.println("  RX: (nothing received — check wiring and baud rate)");
        return;
    }

    Serial.printf("  RX (%u bytes):", (unsigned)rxLen);
    for (size_t i = 0; i < rxLen; i++) Serial.printf(" %02X", rxBuf[i]);
    Serial.println();

    // Decode what we can.
    Serial.printf("  RX[0] addr expected 0x%02X, got 0x%02X  %s\n",
                  _addr, rxBuf[0], rxBuf[0] == _addr ? "OK" : "MISMATCH");
    if (rxLen >= 2)
        Serial.printf("  RX[1] = 0x%02X  (func echo or first data byte)\n", rxBuf[1]);

    // Recalculate checksum both ways so the user can see which matches.
    if (rxLen >= 2) {
        uint8_t sumChk = 0, xorChk = 0;
        for (size_t i = 0; i < rxLen - 1; i++) { sumChk += rxBuf[i]; xorChk ^= rxBuf[i]; }
        Serial.printf("  expected tCHK (SUM) = 0x%02X, (XOR) = 0x%02X, got 0x%02X\n",
                      sumChk, xorChk, rxBuf[rxLen - 1]);
    }
}

bool MKSServo42C::saveParams() {
    // 0xFF 0xC8 = save current F6 velocity as startup speed.
    // Config settings (0x82–0x93) are auto-saved when accepted; no explicit save needed.
    uint8_t d[] = {0xC8};
    uint8_t buf[1]; size_t len = 1;
    return doCommand((uint8_t)MKSFunc::FUNC_SAVE_PARAMS, d, 1,
                     buf, len, MKS_UART_TIMEOUT_MS) && buf[0] == 0x01;
}
