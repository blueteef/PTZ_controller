#pragma once

// =============================================================================
// MKS Servo42C Closed-Loop Driver — UART Interface
//
// NOTE: Verify all function codes against your specific MKS Servo42C firmware
//       version before use. Protocol details may differ between firmware
//       releases. Reference: MKS Servo42C UART Communication Protocol v1.x
//
// Wiring: use the MKS 4-pin UART header (Tx / Rx / G / 3V3), NOT the COM pin
//   on the signal connector.  The COM pin is either floating (standard variant)
//   or an optocoupler supply voltage reference (OC variant) — it is not a data
//   line.  Cross Tx→Rx and Rx→Tx.  3.3 V TTL; no level shifter required.
//
// Frame format (downlink):  [Addr][Func][Data...][tCHK]
// Frame format (uplink):    [Addr][Func][Data...][tCHK]  ← func IS echoed
//   tCHK (downlink) = 8-bit SUM of all preceding bytes (per manual example)
//   tCHK (uplink)   = formula unclear; response checksum is not verified —
//                     addr + func echo validation used instead.
//   Slave addresses: 0xE0–0xE9 (default 0xE0)
//   Default baud: 38400
// =============================================================================

#include <Arduino.h>
#include <HardwareSerial.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// -----------------------------------------------------------------------------
// MKS Servo42C UART Function Codes
// NOTE: Verify all function codes against your specific MKS Servo42C firmware
//       version before use.
// -----------------------------------------------------------------------------
enum class MKSFunc : uint8_t {
    FUNC_READ_ENCODER    = 0x30,
    FUNC_READ_SPEED      = 0x32,
    FUNC_READ_PULSES     = 0x33,
    FUNC_READ_IO         = 0x34,
    FUNC_READ_ERROR      = 0x39,
    FUNC_READ_STATUS     = 0x3A,
    FUNC_CALIBRATE       = 0x80,
    FUNC_SET_WORKMODE    = 0x82,
    FUNC_SET_CURRENT     = 0x83,   // data: 1-byte index, current = index × 200 mA (0x00–0x0F)
    FUNC_SET_MSTEP       = 0x84,
    FUNC_SET_EN_LEVEL    = 0x85,
    FUNC_SET_DIRECTION   = 0x86,
    // Zero-mode (homing) commands — 0x90–0x94
    FUNC_SET_ZERO_MODE   = 0x90,   // 0=Disable, 1=DirMode, 2=NearMode
    FUNC_SET_ZERO_POINT  = 0x91,   // Save current position as home (data byte 0x00)
    FUNC_SET_ZERO_SPEED  = 0x92,   // 0=fastest … 4=slowest
    FUNC_SET_ZERO_DIR    = 0x93,   // 0=CW, 1=CCW
    FUNC_GO_HOME         = 0x94,   // Return to saved zero point (data byte 0x00)
    FUNC_ENABLE          = 0xF3,
    FUNC_RUN_VELOCITY    = 0xF6,   // CR_UART mode: constant speed
    FUNC_STOP            = 0xF7,   // CR_UART mode: decelerate to stop
    FUNC_RUN_PULSES      = 0xFD,   // CR_UART mode: run N microsteps
    FUNC_SAVE_PARAMS     = 0xFF,
};

// Status byte bit flags returned by FUNC_READ_STATUS
static const uint8_t MKS_STATUS_ENABLED  = 0x01;
static const uint8_t MKS_STATUS_IN_POS   = 0x02;
static const uint8_t MKS_STATUS_STALL    = 0x04;
static const uint8_t MKS_STATUS_HOMING   = 0x08;

class MKSServo42C {
public:
    // -------------------------------------------------------------------------
    // Constructor
    //   uart — HardwareSerial port connected to the driver's COM port
    //   addr — driver address (set via DIP switches, default 0x01)
    // -------------------------------------------------------------------------
    MKSServo42C(HardwareSerial& uart, uint8_t addr);

    // Initialise UART. Call after Serial1/Serial2 .begin() with correct pins.
    bool begin(uint32_t baud = 38400);

    // -------------------------------------------------------------------------
    // Communication test
    // -------------------------------------------------------------------------
    // Sends a read-status frame and returns true if valid reply received.
    bool ping();

    // -------------------------------------------------------------------------
    // Configuration commands (write to driver RAM; use save() to persist)
    // -------------------------------------------------------------------------

    // Set microstep resolution: valid values 1,2,4,8,16,32,64,128,256
    bool setMicrosteps(uint8_t mstep);

    // Set working (run) current in milliamps (e.g. 800 = 800 mA)
    bool setWorkingCurrentMA(uint16_t ma);

    // Set hold current as percentage of working current (0–100)
    bool setHoldCurrentPercent(uint8_t pct);

    // Reverse motor direction
    bool setDirection(bool reverse);

    // Set whether EN pin is active-low (true) or active-high (false)
    bool setEnableActiveLevel(bool activeLow);

    // Enable or disable driver output stage
    bool enableMotor(bool en);

    // -------------------------------------------------------------------------
    // Motion commands
    // -------------------------------------------------------------------------

    // Immediately halt motor (coasts or brakes depending on firmware setting)
    bool emergencyStop();

    // Zero-mode (homing) setup — must be called once before goHome() works.
    //   mode: 0=Disable, 1=DirMode (fixed direction), 2=NearMode (shortest path)
    bool setZeroMode(uint8_t mode);
    //   speed: 0=fastest … 4=slowest
    bool setZeroSpeed(uint8_t speed);
    //   cw: true=CW, false=CCW
    bool setZeroDir(bool cw);
    // Save current encoder position as the zero/home point.
    bool setZeroPoint();
    // Command driver to return to the saved zero point (0x94).
    // Requires setZeroMode(non-zero) and setZeroPoint() to have been called first.
    bool goHome();

    // Run encoder calibration routine (motor will rotate one full revolution)
    bool calibrateEncoder();

    // -------------------------------------------------------------------------
    // Read-back commands
    // -------------------------------------------------------------------------

    // Read encoder absolute angle; maps 0x0000–0x3FFF (14-bit) to 0–360°
    bool readEncoderAngle(float& angleDeg);

    // Read driver-measured shaft speed in RPM (signed)
    bool readSpeedRPM(int16_t& rpm);

    // Read difference between commanded and actual shaft angle (degrees)
    bool readShaftError(float& errorDeg);

    // Read raw status byte (see MKS_STATUS_* bit flags above)
    bool readStatus(uint8_t& statusByte);

    // Save current configuration parameters to driver flash
    bool saveParams();

    // -------------------------------------------------------------------------
    // UART drive-mode motion commands  (driver must be in CR_UART work mode)
    // -------------------------------------------------------------------------

    // Run motor at constant speed.
    //   spdDir: bit7 = direction (0=CW, 1=CCW), bits6-0 = speed (0–127)
    //   RPM = speed_bits × 30000 / (Mstep × 200)
    //   Response status: 1 = running, 2 = speed changed, 0 = error
    bool runVelocity(uint8_t spdDir);

    // Stop motor (decelerate to halt).
    //   Response status: 1 = stopping, 2 = already stopped, 0 = error
    bool stopMotor();

    // Run a fixed number of microsteps then stop.
    //   spdDir: same encoding as runVelocity
    //   pulses: microstep count (32-bit)
    //   Returns true when the driver acknowledges the command (status=1).
    //   Call pollRunPulses() to detect completion (status=2).
    bool runPulses(uint8_t spdDir, uint32_t pulses);

    // Non-blocking poll for runPulses completion.
    //   done: set true when driver reports status=2 (reached target)
    //   Returns false on UART timeout.
    bool pollRunPulses(bool& done);

    // -------------------------------------------------------------------------
    // Address management
    // -------------------------------------------------------------------------
    void    setAddress(uint8_t addr);
    uint8_t getAddress() const;

    // -------------------------------------------------------------------------
    // Raw diagnostic — sends frame and dumps every received byte to Serial.
    // Use 'diag' CLI command to call this.
    // -------------------------------------------------------------------------
    void sendRaw(uint8_t func, const uint8_t* data, size_t dlen,
                 uint32_t timeoutMs = 200);

private:
    HardwareSerial&   _uart;
    uint8_t           _addr;
    SemaphoreHandle_t _uart_mutex    = nullptr;
    uint8_t           _encoderBytes  = 6;   // 6 = newer fw (carry+value); 2 = older fw (value only)

    // tCHK = 8-bit SUM of all preceding bytes
    uint8_t calcCRC(const uint8_t* data, size_t len) const;

    // Build and transmit a UART command frame to the driver
    bool sendCommand(uint8_t func, const uint8_t* data, size_t dlen);

    // Read and validate a response frame; populates buf/len with data bytes
    // (excludes addr, func, and CRC bytes)
    bool readResponse(uint8_t func, uint8_t* buf, size_t& len, uint32_t timeoutMs);

    // Mutex-protected sendCommand + readResponse in one atomic call.
    // All public command methods use this to prevent races with the motionTask.
    bool doCommand(uint8_t func, const uint8_t* txData, size_t txLen,
                   uint8_t* rxBuf, size_t& rxLen, uint32_t timeoutMs);
};
