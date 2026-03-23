#!/usr/bin/env python3
"""
test_serial.py — Smoke test for the ESP32 serial bridge.

Run this on the Pi (with ESP32 connected) BEFORE starting the main server
to verify that the serial connection and protocol are working.

Usage:
  python3 scripts/test_serial.py
  python3 scripts/test_serial.py --port /dev/ttyUSB1
"""

import argparse
import sys
import time

try:
    import serial
except ImportError:
    print("ERROR: pyserial not installed.  Run: pip install pyserial")
    sys.exit(1)

def main():
    parser = argparse.ArgumentParser(description="ESP32 serial bridge smoke test")
    parser.add_argument("--port", default="/dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()

    print(f"Opening {args.port} at {args.baud} baud...")
    try:
        ser = serial.Serial(args.port, args.baud, timeout=1.0)
    except serial.SerialException as e:
        print(f"FAIL: {e}")
        sys.exit(1)

    ser.reset_input_buffer()
    time.sleep(0.2)  # let ESP32 settle

    tests = [
        ("ping all",     "OK"),
        ("get position", "pan"),
        ("get speed",    "speed"),
    ]

    passed = 0
    for cmd, expect in tests:
        ser.write(f"{cmd}\n".encode())
        time.sleep(0.3)
        raw = ser.read(ser.in_waiting or 256).decode(errors="replace")
        ok = expect.lower() in raw.lower()
        status = "PASS" if ok else "FAIL"
        print(f"  [{status}]  {cmd!r:20s}  →  {repr(raw.strip())[:60]}")
        if ok:
            passed += 1

    ser.close()
    print(f"\n{passed}/{len(tests)} tests passed")
    sys.exit(0 if passed == len(tests) else 1)

if __name__ == "__main__":
    main()
