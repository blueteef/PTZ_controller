// =============================================================================
// StatusLED.cpp
// =============================================================================

#include "StatusLED.h"

StatusLED::StatusLED(uint8_t pin) : _pin(pin) {}

void StatusLED::begin() {
    pinMode(_pin, OUTPUT);
    setPin(false);
}

void StatusLED::setPattern(LEDPattern p) {
    if (p == _pattern) return;
    _pattern  = p;
    _phase    = 0;
    _lastMs   = millis();
    // Apply initial state immediately.
    setPin(p == LEDPattern::SOLID);
}

void StatusLED::setPin(bool on) {
    _pinState = on;
    digitalWrite(_pin, on ? HIGH : LOW);
}

void StatusLED::update() {
    uint32_t now     = millis();
    uint32_t elapsed = now - _lastMs;

    switch (_pattern) {
        case LEDPattern::OFF:
            setPin(false);
            break;

        case LEDPattern::SOLID:
            setPin(true);
            break;

        case LEDPattern::SLOW_BLINK:
            // 500 ms on, 500 ms off  (1 Hz)
            if (elapsed >= 500) {
                setPin(!_pinState);
                _lastMs = now;
            }
            break;

        case LEDPattern::FAST_BLINK:
            // 62 ms on, 62 ms off  (~8 Hz)
            if (elapsed >= 62) {
                setPin(!_pinState);
                _lastMs = now;
            }
            break;

        case LEDPattern::DOUBLE_BLINK: {
            // Sequence (phase 0-4, then 1 s gap):
            //   0: ON  60 ms
            //   1: OFF 100 ms
            //   2: ON  60 ms
            //   3: OFF 100 ms
            //   4: gap 700 ms  (total cycle ≈ 1 s)
            static const uint16_t durations[] = {60, 100, 60, 100, 700};
            static const bool     states[]    = {true, false, true, false, false};
            constexpr uint8_t phases = 5;

            if (elapsed >= durations[_phase]) {
                _phase  = (_phase + 1) % phases;
                _lastMs = now;
                setPin(states[_phase]);
            }
            break;
        }
    }
}
