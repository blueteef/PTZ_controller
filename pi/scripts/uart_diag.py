#!/usr/bin/env python3
"""
uart_diag.py — Pi UART diagnostic script.

Sends test commands and displays raw responses with hex dump.
Use with an FTDI monitor on the other end to verify TX/RX.

Usage:
  python3 scripts/uart_diag.py
  python3 scripts/uart_diag.py --port /dev/serial0 --baud 115200
"""

import argparse
import sys
import time

try:
    import serial
except ImportError:
    print("ERROR: pyserial not installed. Run: pip install pyserial")
    sys.exit(1)


def hexdump(data: bytes) -> str:
    if not data:
        return "(empty)"
    hex_part = " ".join(f"{b:02x}" for b in data)
    asc_part = "".join(chr(b) if 32 <= b < 127 else "." for b in data)
    return f"{hex_part}  |{asc_part}|"


def send_and_read(ser, cmd: str, wait: float = 1.0) -> bytes:
    ser.reset_input_buffer()
    raw = cmd.encode()
    print(f"\n>> TX: {repr(cmd)}")
    print(f"   hex: {hexdump(raw)}")
    ser.write(raw)
    ser.flush()
    time.sleep(wait)
    response = ser.read(ser.in_waiting or 256)
    print(f"<< RX: {repr(response)}")
    print(f"   hex: {hexdump(response)}")
    return response


def main():
    parser = argparse.ArgumentParser(description="Pi UART diagnostic")
    parser.add_argument("--port", default="/dev/serial0")
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()

    print(f"Opening {args.port} at {args.baud} baud...")
    try:
        ser = serial.Serial(args.port, args.baud, timeout=1.0)
    except serial.SerialException as e:
        print(f"FAIL: {e}")
        sys.exit(1)

    print(f"Port open. RTS={ser.rts} CTS={ser.cts} DSR={ser.dsr}")
    time.sleep(0.5)

    print("\n--- Draining any startup output ---")
    time.sleep(1.0)
    startup = ser.read(ser.in_waiting or 256)
    if startup:
        print(f"Startup data: {repr(startup)}")
        print(f"hex: {hexdump(startup)}")
    else:
        print("(nothing in buffer)")

    commands = [
        ("ping all\n",     1.0),
        ("get position\n", 1.0),
        ("get speed\n",    1.0),
    ]

    for cmd, wait in commands:
        send_and_read(ser, cmd, wait)

    print("\n--- Loopback check (expect RX=TX if loopback jumper fitted) ---")
    test_bytes = b"\xAA\x55\xAA\x55"
    ser.reset_input_buffer()
    ser.write(test_bytes)
    ser.flush()
    time.sleep(0.2)
    echo = ser.read(ser.in_waiting or 16)
    print(f"Sent:     {hexdump(test_bytes)}")
    print(f"Received: {hexdump(echo)}")
    if echo == test_bytes:
        print("LOOPBACK OK — TX is wired to RX")
    elif not echo:
        print("No echo — TX is not looped back (expected if connected to ESP32)")
    else:
        print("Partial/garbled echo — check wiring")

    ser.close()
    print("\nDone.")


if __name__ == "__main__":
    main()
