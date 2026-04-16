#pragma once

// =============================================================================
// StatusLED — non-blocking LED pattern driver
//
// Patterns
// ────────
//   OFF          — always off
//   SOLID        — always on
//   SLOW_BLINK   — 1 Hz blink  — idle / gamepad not connected
//   FAST_BLINK   — 8 Hz blink  — error / e-stop active
//   DOUBLE_BLINK — two quick pulses then pause — homing in progress
// =============================================================================

#include <Arduino.h>
#include "config.h"

enum class LEDPattern {
    OFF,
    SOLID,
    SLOW_BLINK,
    FAST_BLINK,
    DOUBLE_BLINK,
};

class StatusLED {
public:
    explicit StatusLED(uint8_t pin = STATUS_LED_PIN);

    void begin();
    void setPattern(LEDPattern p);
    LEDPattern getPattern() const { return _pattern; }

    // Call regularly (e.g. from loop() or a dedicated task at ≥ 20 Hz).
    void update();

private:
    uint8_t    _pin;
    LEDPattern _pattern    = LEDPattern::OFF;
    uint32_t   _lastMs     = 0;
    uint8_t    _phase      = 0;   // sub-state within DOUBLE_BLINK sequence
    bool       _pinState   = false;

    void setPin(bool on);
};
