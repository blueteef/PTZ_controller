#!/usr/bin/env python3
"""
calibrate_imu.py — Interactive IMU / compass orientation calibration.

Reads live $IMU and $MAG lines from the ESP32 over the GPIO UART and walks
you through figuring out the correct sign flips, axis swap, level offsets,
and compass offset for your .env file.  No guessing required.

Usage:
    cd ~/PTZ_controller/pi
    venv/bin/python3 scripts/calibrate_imu.py

Options:
    --port   Serial device  (default: /dev/serial0 or SERIAL_PORT from .env)
    --baud   Baud rate      (default: 115200 or SERIAL_BAUD from .env)
    --write  Write results to pi/.env automatically (asks interactively by default)
"""

import argparse
import math
import os
import sys
import threading
import time
from pathlib import Path

try:
    from dotenv import load_dotenv
    load_dotenv(Path(__file__).parent.parent / ".env", override=True)
except ImportError:
    pass

try:
    import serial
except ImportError:
    print("ERROR: pyserial not installed.")
    sys.exit(1)


# ---------------------------------------------------------------------------
# Shared state updated by the reader thread
# ---------------------------------------------------------------------------

_imu = {"roll": 0.0, "pitch": 0.0, "ok": False}
_mag = {"mx": 0.0, "my": 0.0, "mz": 0.0, "ok": False}
_stop = threading.Event()


def _parse_kv(s: str) -> dict:
    out = {}
    for pair in s.split(","):
        if "=" in pair:
            k, v = pair.split("=", 1)
            out[k.strip()] = v.strip()
    return out


def _mag_hdg() -> float:
    """Compute flat heading from raw mx/my (no tilt comp — pan axis mount)."""
    mx, my = _mag["mx"], _mag["my"]
    return math.degrees(math.atan2(-my, mx)) % 360.0


def _reader(port: "serial.Serial") -> None:
    while not _stop.is_set():
        try:
            raw = port.readline().decode(errors="replace").strip()
            if raw.startswith("$IMU "):
                kv = _parse_kv(raw[5:])
                _imu.update({
                    "roll":  float(kv.get("roll",  0)),
                    "pitch": float(kv.get("pitch", 0)),
                    "ok":    bool(int(kv.get("ok", 0))),
                })
            elif raw.startswith("$MAG "):
                kv = _parse_kv(raw[5:])
                _mag.update({
                    "mx": float(kv.get("mx", 0)),
                    "my": float(kv.get("my", 0)),
                    "mz": float(kv.get("mz", 0)),
                    "ok": bool(int(kv.get("ok", 0))),
                })
        except Exception:
            pass


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _apply(roll_raw: float, pitch_raw: float, res: dict) -> tuple[float, float]:
    """Apply current sign/swap/offset corrections to raw roll/pitch."""
    roll  = roll_raw  * res["IMU_ROLL_SIGN"]
    pitch = pitch_raw * res["IMU_PITCH_SIGN"]
    if res["IMU_SWAP_ROLL_PITCH"]:
        roll, pitch = pitch, roll
    roll  += res["IMU_ROLL_OFFSET_DEG"]
    pitch += res["IMU_PITCH_OFFSET_DEG"]
    return roll, pitch


def _sample(n: int = 15, interval: float = 0.06) -> tuple[float, float, float]:
    """Return averaged (roll_raw, pitch_raw, heading) over n samples."""
    rolls, pitches, hdgs = [], [], []
    for _ in range(n):
        rolls.append(_imu["roll"])
        pitches.append(_imu["pitch"])
        hdgs.append(_mag_hdg())
        time.sleep(interval)
    sin_s = sum(math.sin(math.radians(h)) for h in hdgs)
    cos_s = sum(math.cos(math.radians(h)) for h in hdgs)
    hdg_avg = math.degrees(math.atan2(sin_s, cos_s)) % 360.0
    return sum(rolls) / len(rolls), sum(pitches) / len(pitches), hdg_avg


def _watch_until_enter(res: dict) -> None:
    """Print live corrected roll/pitch and raw heading until ENTER."""
    stop = threading.Event()

    def _display() -> None:
        while not stop.is_set():
            r, p = _apply(_imu["roll"], _imu["pitch"], res)
            h = _mag_hdg()
            print(
                f"\r  roll={r:+7.1f}°  pitch={p:+7.1f}°  heading={h:5.1f}°"
                f"  [press ENTER]  ",
                end="", flush=True,
            )
            time.sleep(0.1)
        print()

    t = threading.Thread(target=_display, daemon=True)
    t.start()
    input()
    stop.set()
    t.join(timeout=0.5)


def _ask(prompt: str, choices: list) -> str:
    choices_lower = [c.lower() for c in choices]
    while True:
        ans = input(prompt).strip().lower()
        if ans in choices_lower:
            return ans
        print(f"  Please answer: {' / '.join(choices)}")


def _section(title: str, step: int, total: int) -> None:
    print()
    print(f"{'─'*62}")
    print(f"  Step {step}/{total}: {title}")
    print(f"{'─'*62}")


# ---------------------------------------------------------------------------
# .env writer
# ---------------------------------------------------------------------------

def _write_env(env_path: Path, results: dict) -> None:
    key_map = {
        "IMU_ROLL_SIGN":        str(results["IMU_ROLL_SIGN"]),
        "IMU_PITCH_SIGN":       str(results["IMU_PITCH_SIGN"]),
        "IMU_SWAP_ROLL_PITCH":  "true" if results["IMU_SWAP_ROLL_PITCH"] else "false",
        "IMU_ROLL_OFFSET_DEG":  f"{results['IMU_ROLL_OFFSET_DEG']:.1f}",
        "IMU_PITCH_OFFSET_DEG": f"{results['IMU_PITCH_OFFSET_DEG']:.1f}",
        "MAG_HDG_OFFSET_DEG":   f"{results['MAG_HDG_OFFSET_DEG']:.1f}",
        "MAG_HDG_INVERT":       "true" if results["MAG_HDG_INVERT"] else "false",
    }

    existing = env_path.read_text().splitlines() if env_path.exists() else []
    written: set = set()
    new_lines = []

    for line in existing:
        key = line.split("=", 1)[0].strip().lstrip("#").strip()
        if key in key_map:
            new_lines.append(f"{key}={key_map[key]}")
            written.add(key)
        else:
            new_lines.append(line)

    unwritten = [k for k in key_map if k not in written]
    if unwritten:
        new_lines.append("")
        new_lines.append("# IMU / Compass orientation (written by calibrate_imu.py)")
        for k in unwritten:
            new_lines.append(f"{k}={key_map[k]}")

    env_path.write_text("\n".join(new_lines) + "\n")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(description="PTZ IMU/Compass calibration helper")
    parser.add_argument("--port",  default=os.getenv("SERIAL_PORT", "/dev/serial0"))
    parser.add_argument("--baud",  type=int, default=int(os.getenv("SERIAL_BAUD", "115200")))
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()

    print()
    print("╔══════════════════════════════════════════════════════════════╗")
    print("║         PTZ — IMU / Compass Calibration Helper              ║")
    print("╚══════════════════════════════════════════════════════════════╝")
    print(f"\n  Port: {args.port}  Baud: {args.baud}")
    print("  Press Ctrl+C at any time to abort.\n")

    try:
        port = serial.Serial(args.port, args.baud, timeout=0.5)
    except serial.SerialException as e:
        print(f"ERROR: cannot open serial port — {e}")
        sys.exit(1)

    reader_t = threading.Thread(target=_reader, args=(port,), daemon=True)
    reader_t.start()

    print("  Waiting for sensor data from ESP32 …", end="", flush=True)
    deadline = time.monotonic() + 6.0
    while time.monotonic() < deadline:
        if _imu["ok"] or _mag["ok"]:
            break
        time.sleep(0.1)
        print(".", end="", flush=True)
    print()

    if not _imu["ok"]:
        print("\n  WARNING: No IMU data ($IMU) — check I2C1 wiring (GPIO18/5) and MPU-6050.")
    if not _mag["ok"]:
        print("\n  WARNING: No compass data ($MAG) — check I2C0 wiring (GPIO22/23) and QMC5883L.")
    if not _imu["ok"] and not _mag["ok"]:
        print("\n  No sensor data — is the ESP32 running?")
        _stop.set(); port.close(); sys.exit(1)

    # Working results — updated incrementally so _watch_until_enter shows
    # corrected values as each step completes.
    results = {
        "IMU_ROLL_SIGN":        1,
        "IMU_PITCH_SIGN":       1,
        "IMU_SWAP_ROLL_PITCH":  False,
        "IMU_ROLL_OFFSET_DEG":  0.0,
        "IMU_PITCH_OFFSET_DEG": 0.0,
        "MAG_HDG_OFFSET_DEG":   0.0,
        "MAG_HDG_INVERT":       False,
    }

    # ════════════════════════════════════════════════════════════════════════
    # Step 1 — Level baseline (raw values — corrections not applied yet)
    # ════════════════════════════════════════════════════════════════════════
    _section("Level baseline", 1, 4)
    print("  Place the camera head FLAT AND LEVEL, aimed roughly forward.")
    print("  Live display shows RAW values at this step.")
    print("  Watch them stabilise, then press ENTER.\n")
    _watch_until_enter(results)   # no corrections yet — shows raw
    roll0, pitch0, hdg0 = _sample(20)
    print(f"\n  Raw baseline →  roll={roll0:+.1f}°  pitch={pitch0:+.1f}°  heading={hdg0:.1f}°")

    # ════════════════════════════════════════════════════════════════════════
    # Step 2 — Tilt motor: detect axis swap + pitch sign
    # ════════════════════════════════════════════════════════════════════════
    _section("Tilt motor test — axis swap + pitch direction", 2, 4)
    print("  Use the TILT motor to tilt the camera as far as it will go in")
    print("  ONE direction, hold it there, and press ENTER.")
    print("  (It doesn't matter which direction — we'll ask next.)\n")
    _watch_until_enter(results)

    roll_t, pitch_t, _ = _sample(15)
    roll_delta  = roll_t  - roll0
    pitch_delta = pitch_t - pitch0
    print(f"\n  Raw change →  roll: {roll_delta:+.1f}°   pitch: {pitch_delta:+.1f}°")

    # Which raw axis moved most?
    if abs(pitch_delta) >= abs(roll_delta):
        results["IMU_SWAP_ROLL_PITCH"] = False
        moving_delta = pitch_delta
        print("  Pitch axis moved more → no swap needed.  IMU_SWAP_ROLL_PITCH=false")
    else:
        results["IMU_SWAP_ROLL_PITCH"] = True
        moving_delta = roll_delta
        print("  Roll axis moved more → axes SWAPPED.  IMU_SWAP_ROLL_PITCH=true")

    # Ask which physical direction the tilt went
    print()
    ans = _ask("  Did the camera tilt NOSE DOWN or NOSE UP?  [down / up]: ", ["down", "up"])
    nose_down = (ans == "down")

    # Convention: nose-down = negative pitch after correction
    # moving_delta is the raw change on whichever axis responded
    # If nose-down and moving_delta > 0: needs flip
    # If nose-down and moving_delta < 0: correct
    # Opposite for nose-up
    if nose_down:
        results["IMU_PITCH_SIGN"] = -1 if moving_delta > 0 else 1
    else:
        results["IMU_PITCH_SIGN"] =  1 if moving_delta > 0 else -1

    sign_str = "-1 (flipped)" if results["IMU_PITCH_SIGN"] == -1 else "1 (correct)"
    print(f"  IMU_PITCH_SIGN={results['IMU_PITCH_SIGN']}  ({sign_str})")

    # ════════════════════════════════════════════════════════════════════════
    # Step 3 — Level offsets (computed from baseline after sign/swap known)
    # ════════════════════════════════════════════════════════════════════════
    _section("Level offsets", 3, 4)
    print("  Return the camera to FLAT AND LEVEL, then press ENTER.")
    print("  This computes the constant offsets to zero it out.\n")
    _watch_until_enter(results)
    roll_lvl, pitch_lvl, _ = _sample(20)

    # Apply sign/swap to get corrected level reading (before offset)
    r_corr, p_corr = _apply(roll_lvl, pitch_lvl, results)
    results["IMU_ROLL_OFFSET_DEG"]  = round(-r_corr, 1)
    results["IMU_PITCH_OFFSET_DEG"] = round(-p_corr, 1)
    print(f"\n  Corrected reading at level: roll={r_corr:+.1f}°  pitch={p_corr:+.1f}°")
    print(f"  IMU_ROLL_OFFSET_DEG={results['IMU_ROLL_OFFSET_DEG']}")
    print(f"  IMU_PITCH_OFFSET_DEG={results['IMU_PITCH_OFFSET_DEG']}")

    print()
    print("  Live display now shows CORRECTED values.  Tilt around to verify,")
    print("  then return to level and press ENTER.\n")
    _watch_until_enter(results)

    # ════════════════════════════════════════════════════════════════════════
    # Step 4 — Roll sign (optional tip test)
    # ════════════════════════════════════════════════════════════════════════
    _section("Roll sign", 4, 5)
    print("  Roll is side-to-side tilt.  No roll motor — options:")
    print("    a) Skip — IMU_ROLL_SIGN=1 is usually fine, fix later if needed.")
    print("    b) Physically tip the entire rig: LEFT SIDE DOWN, press ENTER.\n")
    ans = _ask("  [tip / skip]: ", ["tip", "skip"])

    if ans == "tip":
        print()
        print("  Slowly tip the rig so the LEFT SIDE goes DOWN, hold, press ENTER.\n")
        _watch_until_enter(results)
        roll_tip, _, _ = _sample(15)
        r_tip, _ = _apply(roll_tip, 0, results)
        # After all corrections, left-side-down should give negative roll
        if abs(r_tip) < 5:
            print("  ⚠  Roll barely changed — keeping IMU_ROLL_SIGN=1.")
        elif r_tip > 0:
            results["IMU_ROLL_SIGN"] = -1
            results["IMU_ROLL_OFFSET_DEG"] = round(-(-r_tip), 1)
            print(f"  Left down → positive → flipping.  IMU_ROLL_SIGN=-1")
        else:
            print("  Left down → negative → correct.  IMU_ROLL_SIGN=1")
    else:
        print("  Skipped — IMU_ROLL_SIGN=1.")

    # ════════════════════════════════════════════════════════════════════════
    # Step 5 — Compass
    # ════════════════════════════════════════════════════════════════════════
    _section("Compass heading and direction", 5, 5)

    if not _mag["ok"]:
        print("  ⚠  No compass data — skipping compass calibration.")
    else:
        print("  Part A — rotation direction")
        print("  Point the camera any direction and hold steady.")
        input("  Press ENTER when steady … ")
        _, _, hdg_a = _sample(15)
        print(f"  Reading: {hdg_a:.1f}°")

        print()
        print("  Rotate ~90° CLOCKWISE (to the right) using the PAN motor and hold.")
        input("  Press ENTER when steady … ")
        _, _, hdg_b = _sample(15)
        print(f"  Reading: {hdg_b:.1f}°")

        cw_change = (hdg_b - hdg_a + 360) % 360

        if 45 <= cw_change <= 135:
            results["MAG_HDG_INVERT"] = False
            print(f"\n  Change: +{cw_change:.0f}° → correct.  MAG_HDG_INVERT=false")
        elif 225 <= cw_change <= 315:
            results["MAG_HDG_INVERT"] = True
            print(f"\n  Change: {cw_change-360:.0f}° → inverted.  MAG_HDG_INVERT=true")
        else:
            print(f"\n  ⚠  Ambiguous change ({cw_change:.0f}°) — was rotation close to 90°?")
            ans2 = _ask("  Does heading DECREASE when rotating RIGHT? [y / n]: ", ["y", "n"])
            results["MAG_HDG_INVERT"] = (ans2 == "y")

        print()
        print("  Part B — north offset")
        print("  Point the camera NORTH (use phone compass as reference).")
        input("  Press ENTER when aimed north … ")
        _, _, hdg_north = _sample(20)
        print(f"  Raw heading pointing north: {hdg_north:.1f}°")

        if results["MAG_HDG_INVERT"]:
            effective = (360.0 - hdg_north) % 360.0
        else:
            effective = hdg_north
        offset = -effective
        if offset < -180: offset += 360
        if offset >  180: offset -= 360
        results["MAG_HDG_OFFSET_DEG"] = round(offset, 1)
        print(f"  MAG_HDG_OFFSET_DEG={results['MAG_HDG_OFFSET_DEG']}")

    # ════════════════════════════════════════════════════════════════════════
    # Results
    # ════════════════════════════════════════════════════════════════════════
    print()
    print("╔══════════════════════════════════════════════════════════════╗")
    print("║                  Calibration Results                        ║")
    print("╚══════════════════════════════════════════════════════════════╝")
    print()
    env_lines = [
        f"IMU_ROLL_SIGN={results['IMU_ROLL_SIGN']}",
        f"IMU_PITCH_SIGN={results['IMU_PITCH_SIGN']}",
        f"IMU_SWAP_ROLL_PITCH={'true' if results['IMU_SWAP_ROLL_PITCH'] else 'false'}",
        f"IMU_ROLL_OFFSET_DEG={results['IMU_ROLL_OFFSET_DEG']:.1f}",
        f"IMU_PITCH_OFFSET_DEG={results['IMU_PITCH_OFFSET_DEG']:.1f}",
        f"MAG_HDG_OFFSET_DEG={results['MAG_HDG_OFFSET_DEG']:.1f}",
        f"MAG_HDG_INVERT={'true' if results['MAG_HDG_INVERT'] else 'false'}",
    ]
    for line in env_lines:
        print(f"  {line}")

    env_path = Path(__file__).parent.parent / ".env"
    do_write = args.write
    if not do_write:
        print()
        ans = _ask(f"  Write these to {env_path}? [y / n]: ", ["y", "n", "yes", "no"])
        do_write = ans in ("y", "yes")

    if do_write:
        _write_env(env_path, results)
        print(f"\n  Written to {env_path}")
        print("  Restart the service to apply:  sudo systemctl restart ptz")
    else:
        print("\n  Not written — add the lines above to pi/.env manually.")

    print()
    _stop.set()
    port.close()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n\nAborted.")
        _stop.set()
        sys.exit(0)
