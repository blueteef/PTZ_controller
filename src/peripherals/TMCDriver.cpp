// =============================================================================
// TMCDriver.cpp
// =============================================================================

#include "TMCDriver.h"
#include <TMCStepper.h>
#include "config.h"

// Serial1 remapped to TMC_UART_RX_PIN / TMC_UART_TX_PIN (half-duplex, shared bus).
static HardwareSerial _tmcSerial(1);

// One TMC2209Stepper object per axis — each addressed via MS1/MS2 hardware pins.
static TMC2209Stepper _pan (&_tmcSerial, TMC_R_SENSE, TMC_PAN_ADDR);
static TMC2209Stepper _tilt(&_tmcSerial, TMC_R_SENSE, TMC_TILT_ADDR);

// -----------------------------------------------------------------------------
// Internal helper
// -----------------------------------------------------------------------------

static bool _configAxis(TMC2209Stepper& drv, uint16_t mA, const char* name) {
    drv.begin();

    uint8_t result = drv.test_connection();
    if (result != 0) {
        Serial.printf("[TMC] %s driver not responding (code %d) — check wiring/address\r\n",
                      name, result);
        return false;
    }

    drv.rms_current(mA);        // set coil current
    drv.microsteps(16);         // match hardware STEP-pin resolution
    drv.intpol(true);           // 256x interpolation — smoother motion, free
    drv.en_spreadCycle(false);  // StealthChop: silent, ideal for slow gimbal moves
    drv.pwm_autoscale(true);    // auto-tune StealthChop PWM
    drv.pwm_autograd(true);     // auto-gradient for better low-speed performance
    drv.toff(3);                // enable driver output (toff=0 disables)

    Serial.printf("[TMC] %s OK — %u mA RMS, StealthChop, 256x interp\r\n", name, mA);
    return true;
}

// -----------------------------------------------------------------------------
// Public
// -----------------------------------------------------------------------------

bool TMCDriver::begin() {
    // 115200 is the TMC2209 default UART baud.
    _tmcSerial.begin(115200, SERIAL_8N1, TMC_UART_RX_PIN, TMC_UART_TX_PIN);
    delay(50);  // let drivers finish power-on reset before first UART access

    bool ok = true;
    ok &= _configAxis(_pan,  TMC_PAN_RMS_MA,  "Pan");
    ok &= _configAxis(_tilt, TMC_TILT_RMS_MA, "Tilt");
    return ok;
}

void TMCDriver::setPanCurrent(uint16_t mA_rms) {
    _pan.rms_current(mA_rms);
}

void TMCDriver::setTiltCurrent(uint16_t mA_rms) {
    _tilt.rms_current(mA_rms);
}
