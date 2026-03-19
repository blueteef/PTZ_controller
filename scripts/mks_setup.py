#!/usr/bin/env python3
"""
MKS Servo42C  —  FTDI Setup & Diagnostic Tool
==============================================
Single source of truth for all driver settings.
Edit the TARGET CONFIG section below, then run option 9 to apply.

Connect FTDI to the driver's 4-pin UART header (Tx / Rx / G / 3V3).
Wire: FTDI-TX → driver-Rx,  FTDI-RX → driver-Tx.
Disconnect the ESP32 first — only one UART master at a time.

Requirements:  pip install pyserial
Usage:         python mks_setup.py
"""

import serial
import serial.tools.list_ports
import time
import sys

# =============================================================================
# TARGET CONFIG  —  edit this section, not the code below
# =============================================================================

BAUD_RATE = 38400
ADDR_PAN  = 0xE0
ADDR_TILT = 0xE1

# Settings applied identically to both axes unless overridden below.
TARGET = {
    "work_mode"    : 0x02,   # CR_UART (0=CR_OPEN 1=CR_vFOC 2=CR_UART) — manual uses 0x02
    "microsteps"   : 16,     # 1/2/4/8/16/32/64/128/256
    "current_ma"   : 1000,   # running current in mA (index × 200 mA, 200–3000)
    "direction"    : 0,      # 0 = CW,  1 = CCW
    "en_active_low": True,   # True = EN pin is active-LOW (standard MKS default)
    # Zero-mode (homing) — must be non-zero for go-home (0x94) to work
    "zero_mode"    : 1,      # 0=Disable, 1=DirMode, 2=NearMode
    "zero_speed"   : 2,      # 0=fastest … 4=slowest
    "zero_dir"     : 0,      # 0=CW, 1=CCW
}

# Per-axis overrides — only entries listed here differ from TARGET.
# Example: if tilt needs reversed direction, add "direction": 1 here.
PAN_OVERRIDES  = {}
TILT_OVERRIDES = {}

# =============================================================================
# Protocol helpers
# =============================================================================

def crc(data: bytes) -> int:
    return sum(data) & 0xFF

def build(addr: int, func: int, data: bytes = b"") -> bytes:
    frame = bytes([addr, func]) + data
    return frame + bytes([crc(frame)])

def xact(ser: serial.Serial, frame: bytes, wait: float = 0.15) -> bytes:
    ser.reset_input_buffer()
    time.sleep(0.005)
    ser.write(frame)
    time.sleep(wait)
    return ser.read(64)

def parse(raw: bytes, addr: int, func: int):
    """Return (data_bytes, format_label) or (None, reason)."""
    if not raw:
        return None, "NO_RESPONSE"
    if raw[0] != addr:
        return None, f"ADDR_MISMATCH(got 0x{raw[0]:02X})"
    if len(raw) < 2:
        return None, "TOO_SHORT"
    if raw[1] == func:
        return raw[2:-1], "A"   # Format A: addr + func echo + data + tCHK
    return raw[1:], "B"         # Format B: addr + data  (no func echo)

def hex_str(b: bytes) -> str:
    return " ".join(f"{x:02X}" for x in b)

def status_flags(byte: int) -> str:
    parts = []
    if byte & 0x01: parts.append("ENABLED")
    if byte & 0x02: parts.append("IN_POS")
    if byte & 0x04: parts.append("STALL")
    if byte & 0x08: parts.append("HOMING")
    for bit in range(4, 8):
        if byte & (1 << bit): parts.append(f"bit{bit}")
    return " | ".join(parts) if parts else "(no flags)"

# =============================================================================
# Command wrappers
# =============================================================================

def cmd_read_status(ser, addr):
    raw = xact(ser, build(addr, 0x3A))
    d, f = parse(raw, addr, 0x3A)
    return d, f, raw

def cmd_read_encoder(ser, addr):
    raw = xact(ser, build(addr, 0x30))
    d, f = parse(raw, addr, 0x30)
    if d and len(d) >= 6:
        carry = int.from_bytes(d[0:4], "big", signed=True)
        value = int.from_bytes(d[4:6], "big")
        return carry * 360.0 + value * 360.0 / 16384.0, f + "(6-byte)"
    if d and len(d) >= 2:
        value = int.from_bytes(d[0:2], "big")
        return value * 360.0 / 16384.0, f + "(2-byte)"
    return None, f

def cmd_read_speed(ser, addr):
    raw = xact(ser, build(addr, 0x32))
    d, f = parse(raw, addr, 0x32)
    if d and len(d) >= 2:
        return int.from_bytes(d[0:2], "big", signed=True), f
    return None, f

def cmd_set_workmode(ser, addr, mode: int):
    # Manual: mode 0=CR_OPEN, 1=CR_vFOC, 2=CR_UART
    raw = xact(ser, build(addr, 0x82, bytes([mode])))
    return parse(raw, addr, 0x82)

def cmd_set_zero_mode(ser, addr, mode: int):
    raw = xact(ser, build(addr, 0x90, bytes([mode])))
    return parse(raw, addr, 0x90)

def cmd_set_zero_point(ser, addr):
    """Save current position as home/zero (0x91 0x00)."""
    raw = xact(ser, build(addr, 0x91, bytes([0x00])))
    return parse(raw, addr, 0x91)

def cmd_set_zero_speed(ser, addr, speed: int):
    raw = xact(ser, build(addr, 0x92, bytes([speed])))
    return parse(raw, addr, 0x92)

def cmd_set_zero_dir(ser, addr, cw: bool):
    raw = xact(ser, build(addr, 0x93, bytes([0x00 if cw else 0x01])))
    return parse(raw, addr, 0x93)

def cmd_set_microsteps(ser, addr, mstep: int):
    raw = xact(ser, build(addr, 0x84, bytes([mstep])))
    return parse(raw, addr, 0x84)

def cmd_set_current(ser, addr, ma: int):
    """
    Set running current via 0x83.
    Protocol: 1-byte index where current = index × 200 mA (0x01–0x0F).
    Returns (data, fmt, label).
    """
    idx = max(1, min(15, round(ma / 200)))
    payload = bytes([idx])
    raw = xact(ser, build(addr, 0x83, payload))
    d, f = parse(raw, addr, 0x83)
    label = f"idx={idx} ({idx*200} mA)"
    return d, f, label

def cmd_set_direction(ser, addr, reverse: bool):
    raw = xact(ser, build(addr, 0x86, bytes([0x01 if reverse else 0x00])))
    return parse(raw, addr, 0x86)

def cmd_set_en_level(ser, addr, active_low: bool):
    raw = xact(ser, build(addr, 0x85, bytes([0x01 if active_low else 0x00])))
    return parse(raw, addr, 0x85)

def cmd_enable(ser, addr, en: bool):
    raw = xact(ser, build(addr, 0xF3, bytes([0x01 if en else 0x00])))
    return parse(raw, addr, 0xF3)

def cmd_run_velocity(ser, addr, spd_dir: int):
    raw = xact(ser, build(addr, 0xF6, bytes([spd_dir])))
    return parse(raw, addr, 0xF6)

def cmd_stop(ser, addr):
    raw = xact(ser, build(addr, 0xF7))
    return parse(raw, addr, 0xF7)

def cmd_set_zero_and_go(ser, addr):
    """Save current position as zero (0x91 00), then immediately go home (0x94 00)."""
    d, f = cmd_set_zero_point(ser, addr)
    return d, f

def cmd_go_home(ser, addr):
    """Return to saved zero point (0x94 00). Zero mode must be non-Disable."""
    raw = xact(ser, build(addr, 0x94, bytes([0x00])), wait=0.3)
    d, f = parse(raw, addr, 0x94)
    return d, f, raw

def cmd_save_velocity(ser, addr):
    """Save current F6 velocity as startup speed (0xFF 0xC8).
    NOTE: config settings (microsteps, current, direction, zero mode) are
    auto-saved to flash immediately when set — no explicit save needed."""
    raw = xact(ser, build(addr, 0xFF, bytes([0xC8])))
    return parse(raw, addr, 0xFF)

def cmd_clear_velocity(ser, addr):
    """Clear saved startup velocity (0xFF 0xCA)."""
    raw = xact(ser, build(addr, 0xFF, bytes([0xCA])))
    return parse(raw, addr, 0xFF)

def cmd_raw(ser, addr, func: int):
    frame = build(addr, func)
    print(f"  TX: {hex_str(frame)}")
    raw = xact(ser, frame, wait=0.3)
    if not raw:
        print("  RX: (nothing)")
        return
    print(f"  RX ({len(raw)} bytes): {hex_str(raw)}")
    d, f = parse(raw, addr, func)
    if d is not None:
        print(f"  Format {f},  data: {hex_str(d)}")
    else:
        print(f"  Parse: {f}")
    if len(raw) >= 2:
        s = sum(raw[:-1]) & 0xFF
        x = 0
        for b in raw[:-1]: x ^= b
        print(f"  tCHK: got 0x{raw[-1]:02X}  SUM=0x{s:02X}  XOR=0x{x:02X}")

# =============================================================================
# Core: apply all settings to one axis
# =============================================================================

def apply_all(ser, addr: int, name: str, settings: dict) -> list:
    """
    Apply every setting in `settings` to the driver at `addr`.
    Returns a list of (parameter, result, detail) rows for display.
    """
    rows   = []
    manual = []   # items that need physical-display config

    def row(param, status, detail=""):
        rows.append((param, status, detail))

    # ── Work mode ────────────────────────────────────────────────────────────
    # Detect if already in CR_UART by probing 0xF6 (send speed=0)
    d, _ = cmd_run_velocity(ser, addr, 0x00)
    if d is not None:
        cmd_stop(ser, addr)
        row("work_mode", "SKIP", "already CR_UART — 0xF6 responded")
    else:
        d, _ = cmd_set_workmode(ser, addr, settings["work_mode"])
        if d is not None and d[0] != 0x00:
            row("work_mode", "OK",
                f"0x{settings['work_mode']:02X} CR_UART  ← REBOOT DRIVER NOW")
        else:
            row("work_mode", "MANUAL",
                f"Set via display: CR_UART  (byte=0x{settings['work_mode']:02X})")
            manual.append(("Work Mode", "CR_UART"))

    # ── Microsteps ───────────────────────────────────────────────────────────
    d, _ = cmd_set_microsteps(ser, addr, settings["microsteps"])
    if d is not None and d[0] != 0x00:
        row("microsteps", "OK", str(settings["microsteps"]))
    else:
        row("microsteps", "MANUAL", str(settings["microsteps"]))
        manual.append(("Microsteps", str(settings["microsteps"])))

    # ── Current ───────────────────────────────────────────────────────────────
    d, _, enc = cmd_set_current(ser, addr, settings["current_ma"])
    if d is not None:
        row("current", "OK", f"{settings['current_ma']} mA  [{enc}]")
    else:
        row("current", "MANUAL", f"{settings['current_ma']} mA")
        manual.append(("Running Current", f"{settings['current_ma']} mA"))

    # ── Direction ────────────────────────────────────────────────────────────
    reverse = bool(settings["direction"])
    d, _ = cmd_set_direction(ser, addr, reverse)
    if d is not None and d[0] != 0x00:
        row("direction", "OK", "CCW" if reverse else "CW")
    else:
        row("direction", "MANUAL", "CCW" if reverse else "CW")
        manual.append(("Direction", "CCW" if reverse else "CW"))

    # ── EN pin active level ───────────────────────────────────────────────────
    active_low = settings["en_active_low"]
    d, _ = cmd_set_en_level(ser, addr, active_low)
    if d is not None and d[0] != 0x00:
        row("en_level", "OK", "active-LOW" if active_low else "active-HIGH")
    else:
        row("en_level", "MANUAL", "active-LOW" if active_low else "active-HIGH")
        manual.append(("EN Level", "active-LOW" if active_low else "active-HIGH"))

    # ── Zero mode (homing) ────────────────────────────────────────────────────
    d, _ = cmd_set_zero_mode(ser, addr, settings["zero_mode"])
    if d is not None and d[0] != 0x00:
        modes = {0: "Disable", 1: "DirMode", 2: "NearMode"}
        row("zero_mode", "OK", modes.get(settings["zero_mode"], str(settings["zero_mode"])))
    else:
        row("zero_mode", "MANUAL", f"0_Mode = {settings['zero_mode']}")
        manual.append(("0_Mode", "DirMode" if settings["zero_mode"] == 1 else str(settings["zero_mode"])))

    d, _ = cmd_set_zero_speed(ser, addr, settings["zero_speed"])
    if d is not None and d[0] != 0x00:
        row("zero_speed", "OK", str(settings["zero_speed"]))
    else:
        row("zero_speed", "MANUAL", f"0_Speed = {settings['zero_speed']}")
        manual.append(("0_Speed", str(settings["zero_speed"])))

    d, _ = cmd_set_zero_dir(ser, addr, settings["zero_dir"] == 0)
    if d is not None and d[0] != 0x00:
        row("zero_dir", "OK", "CW" if settings["zero_dir"] == 0 else "CCW")
    else:
        row("zero_dir", "MANUAL", f"0_Dir = {'CW' if settings['zero_dir'] == 0 else 'CCW'}")
        manual.append(("0_Dir", "CW" if settings["zero_dir"] == 0 else "CCW"))

    # ── Print results ─────────────────────────────────────────────────────────
    col_w = 14
    print(f"\n  ┌─ {name}  (0x{addr:02X}) {'─' * 36}")
    for param, status, detail in rows:
        icon = "✓" if status == "OK" else ("→" if status == "SKIP" else "!")
        print(f"  │  {icon} {param:<{col_w}} [{status:<6}]  {detail}")
    print(f"  └{'─' * 50}")

    if manual:
        print(f"\n  !! {name}: {len(manual)} setting(s) must be configured via the")
        print(f"     driver's OLED display / buttons:")
        for param, value in manual:
            print(f"       • {param:<20} →  {value}")

    return rows


# =============================================================================
# Menu helpers
# =============================================================================

def pick_port() -> str:
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        print("No serial ports found.")
        sys.exit(1)
    if len(ports) == 1:
        print(f"Auto-selected: {ports[0].device}  ({ports[0].description})")
        return ports[0].device
    print("\nAvailable ports:")
    for i, p in enumerate(ports):
        print(f"  {i+1}. {p.device}  —  {p.description}")
    while True:
        try:
            n = int(input("Select port number: "))
            return ports[n - 1].device
        except (ValueError, IndexError):
            print("Invalid selection.")

def pick_axes(prompt="Axis [pan/tilt/both]: "):
    c = input(f"  {prompt}").strip().lower()
    result = []
    if c in ("pan",  "both", "all"): result.append((ADDR_PAN,  "PAN"))
    if c in ("tilt", "both", "all"): result.append((ADDR_TILT, "TILT"))
    if not result:
        print("  Unknown — using pan")
        result = [(ADDR_PAN, "PAN")]
    return result

def merged(overrides: dict) -> dict:
    s = dict(TARGET)
    s.update(overrides)
    return s

def ok_str(data, fmt) -> str:
    if data is None:
        return "NO RESPONSE"
    v = data[0] if data else 0
    return f"0x{v:02X}  {'OK' if v != 0 else 'FAIL (returned 0x00)'}"


# =============================================================================
# Main
# =============================================================================

def main():
    print("=" * 58)
    print("  MKS Servo42C  —  Setup & Diagnostic Tool")
    print("=" * 58)
    print(f"  Target: {TARGET['microsteps']}µstep  "
          f"{TARGET['current_ma']}mA  "
          f"{'CW' if not TARGET['direction'] else 'CCW'}  "
          f"zero_mode={'DirMode' if TARGET['zero_mode']==1 else TARGET['zero_mode']}\n")

    port = pick_port()
    try:
        ser = serial.Serial(port, BAUD_RATE, timeout=0.2)
    except Exception as e:
        print(f"Cannot open {port}: {e}")
        sys.exit(1)
    print(f"Opened {port} @ {BAUD_RATE} baud\n")

    MENU = """
  ── Diagnostics ────────────────────────────────────
  1   Scan both axes  (status + encoder + format)
  2   Read status
  3   Read encoder angle
  4   Read shaft speed (RPM)
  5   Go home  0x94  (motor returns to saved zero)
  5z  Set zero point  0x91  (save current pos as home)
  6   Run velocity  0xF6  !! MOTOR WILL SPIN !!
  7   Stop motor  0xF7
  8   Enable / disable motor output

  ── Configuration ──────────────────────────────────
  9   Apply ALL settings  (from TARGET CONFIG above)
  10  Save startup velocity to flash  0xFF C8  (config settings auto-save)
  11  Show target config

  ── Advanced ───────────────────────────────────────
  12  Send raw command (any hex func code)
  13  Probe current encoding
  0   Exit
"""

    while True:
        print(MENU)
        choice = input("  Choice: ").strip()

        # ── 1 Scan ──────────────────────────────────────────────────────────
        if choice == "1":
            for addr, name in [(ADDR_PAN, "PAN"), (ADDR_TILT, "TILT")]:
                d, fmt, raw = cmd_read_status(ser, addr)
                print(f"\n  {name}  addr=0x{addr:02X}")
                if d is None:
                    print(f"    Status  : NO RESPONSE")
                    continue
                sb = d[0] if d else 0
                print(f"    Status  : 0x{sb:02X}  [{status_flags(sb)}]")
                print(f"    Format  : {'A — newer fw' if fmt == 'A' else 'B — older fw'}")
                print(f"    Raw RX  : {hex_str(raw)}")
                angle, afmt = cmd_read_encoder(ser, addr)
                if angle is not None:
                    print(f"    Encoder : {angle:.2f}°  ({afmt})")
                else:
                    print(f"    Encoder : read failed")

        # ── 2 Status ────────────────────────────────────────────────────────
        elif choice == "2":
            for addr, name in pick_axes():
                d, fmt, raw = cmd_read_status(ser, addr)
                if d is None:
                    print(f"  {name}: NO RESPONSE")
                else:
                    sb = d[0] if d else 0
                    print(f"  {name}: 0x{sb:02X}  [{status_flags(sb)}]  fmt={fmt}")
                    print(f"    Raw: {hex_str(raw)}")

        # ── 3 Encoder ───────────────────────────────────────────────────────
        elif choice == "3":
            for addr, name in pick_axes():
                angle, fmt = cmd_read_encoder(ser, addr)
                if angle is None:
                    print(f"  {name}: encoder read failed")
                else:
                    print(f"  {name}: {angle:.3f}°  ({fmt})")

        # ── 4 Speed ─────────────────────────────────────────────────────────
        elif choice == "4":
            for addr, name in pick_axes():
                rpm, fmt = cmd_read_speed(ser, addr)
                if rpm is None:
                    print(f"  {name}: speed read failed")
                else:
                    print(f"  {name}: {rpm} RPM  ({fmt})")

        # ── 5 Go home ───────────────────────────────────────────────────────
        elif choice == "5":
            print("  Sending 0x94 00 — motor returns to saved zero point.")
            print("  (Requires zero_mode != Disable and zero point previously set via 5z)")
            for addr, name in pick_axes("Axis [pan/tilt/both]: "):
                d, fmt, raw = cmd_go_home(ser, addr)
                print(f"\n  {name}:")
                print(f"    Raw RX : {hex_str(raw) if raw else '(nothing)'}")
                if d is None:
                    print(f"    Result : NO RESPONSE — check that zero_mode is not Disable (run option 9 first)")
                else:
                    sb = d[0] if d else 0
                    print(f"    Result : 0x{sb:02X}  {'accepted — watch motor' if sb != 0 else 'FAILED (0x00)'}")

        # ── 5z Set zero point ────────────────────────────────────────────────
        elif choice == "5z":
            print("  Saving current position as zero/home point (0x91 00).")
            print("  Position the motor at the desired home BEFORE running this.")
            for addr, name in pick_axes("Axis [pan/tilt/both]: "):
                d, fmt = cmd_set_zero_point(ser, addr)
                print(f"  {name}: {ok_str(d, fmt)}")

        # ── 6 Run velocity ──────────────────────────────────────────────────
        elif choice == "6":
            print("  !! Motor will spin !!")
            axes = pick_axes()
            try:
                spd = max(1, min(127, int(input("  Speed 1–127 (10 = slow): "))))
                d   = input("  Direction [cw/ccw]: ").strip().lower()
            except ValueError:
                print("  Invalid input")
                continue
            spd_dir = spd | (0x80 if d == "ccw" else 0x00)
            for addr, name in axes:
                data, fmt = cmd_run_velocity(ser, addr, spd_dir)
                print(f"  {name}: {ok_str(data, fmt)}  (0x{spd_dir:02X})")

        # ── 7 Stop ──────────────────────────────────────────────────────────
        elif choice == "7":
            for addr, name in pick_axes():
                data, fmt = cmd_stop(ser, addr)
                print(f"  {name}: {ok_str(data, fmt)}")

        # ── 8 Enable / disable ──────────────────────────────────────────────
        elif choice == "8":
            en = input("  [enable/disable]: ").strip().lower() == "enable"
            for addr, name in pick_axes():
                data, fmt = cmd_enable(ser, addr, en)
                print(f"  {name} {'enable' if en else 'disable'}: {ok_str(data, fmt)}")

        # ── 9 Apply ALL settings ─────────────────────────────────────────────
        elif choice == "9":
            print("\n  Current TARGET CONFIG:")
            for k, v in TARGET.items():
                print(f"    {k:<16} = {v}")
            if PAN_OVERRIDES:
                print(f"  PAN overrides  : {PAN_OVERRIDES}")
            if TILT_OVERRIDES:
                print(f"  TILT overrides : {TILT_OVERRIDES}")

            axes = pick_axes("\n  Apply to [pan/tilt/both]: ")
            c = input("  Confirm — proceed? [y/N]: ").strip().lower()
            if c != "y":
                continue

            for addr, name in axes:
                overrides = PAN_OVERRIDES if addr == ADDR_PAN else TILT_OVERRIDES
                apply_all(ser, addr, name, merged(overrides))

            print("\n  Use option 10 to save to flash.")

        # ── 10 Save startup velocity ─────────────────────────────────────────
        elif choice == "10":
            print("  Saving current F6 velocity as startup speed (0xFF C8).")
            print("  Note: config settings (microsteps, current, etc.) are already")
            print("  auto-saved to flash when set — this is only for startup velocity.")
            for addr, name in pick_axes():
                data, fmt = cmd_save_velocity(ser, addr)
                print(f"  {name}: {ok_str(data, fmt)}")
                if data is not None and data[0] != 0x00:
                    print(f"    !! Driver will now be DISABLED — re-enable with option 8.")

        # ── 11 Show config ───────────────────────────────────────────────────
        elif choice == "11":
            print("\n  TARGET CONFIG  (single source of truth)\n")
            print(f"  {'Parameter':<18} {'PAN':>10} {'TILT':>10}")
            print(f"  {'-'*40}")
            pan_s  = merged(PAN_OVERRIDES)
            tilt_s = merged(TILT_OVERRIDES)
            for k in TARGET:
                pv = pan_s[k]
                tv = tilt_s[k]
                diff = "  ← differs" if pv != tv else ""
                print(f"  {k:<18} {str(pv):>10} {str(tv):>10}{diff}")

        # ── 12 Raw ──────────────────────────────────────────────────────────
        elif choice == "12":
            axes = pick_axes()
            raw_func = input("  Hex function code (e.g. 91): ").strip()
            try:
                func = int(raw_func, 16)
            except ValueError:
                print("  Invalid hex value")
                continue
            for addr, name in axes:
                print(f"\n  {name}  (0x{addr:02X})  func=0x{func:02X}:")
                cmd_raw(ser, addr, func)

        # ── 13 Test current command ──────────────────────────────────────────
        elif choice == "13":
            axes = pick_axes()
            ma = TARGET["current_ma"]
            idx = max(1, min(15, round(ma / 200)))
            print(f"\n  Sending 0x83 idx={idx} ({idx*200} mA) per manual spec.")
            for addr, name in axes:
                frame = build(addr, 0x83, bytes([idx]))
                raw = xact(ser, frame)
                rx  = hex_str(raw) if raw else "(nothing)"
                ok  = "✓" if raw and raw[0] == addr else "✗"
                print(f"  {ok} {name} TX: {hex_str(frame)}  RX: {rx}")

        elif choice == "0":
            break
        else:
            print("  Unknown option")

    ser.close()
    print("\nPort closed.")


if __name__ == "__main__":
    main()
