#!/usr/bin/env python3
"""
calibrate_imu.py — Interactive IMU / compass orientation calibration.

Reads live $IMU and $MAG lines from the ESP32 over the GPIO UART and walks
you through figuring out the correct sign flips, axis swap, and compass offset
for your .env file.  No guessing required.

Usage:
    cd ~/PTZ_controller/pi
    venv/bin/python3 scripts/calibrate_imu.py

Options:
    --port   Serial device  (default: /dev/serial0 or SERIAL_PORT from .env)
    --baud   Baud rate      (default: 57600 or SERIAL_BAUD from .env)
    --write  Write results to pi/.env automatically (asks interactively by default)
"""

import argparse
import math
import os
import sys
import threading
import time
from pathlib import Path

# Load .env so defaults match the running app
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
_mag = {"hdg": 0.0, "ok": False}
_stop = threading.Event()


def _parse_kv(s: str) -> dict:
    out = {}
    for pair in s.split(","):
        if "=" in pair:
            k, v = pair.split("=", 1)
            out[k.strip()] = v.strip()
    return out


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
                    "hdg": float(kv.get("hdg", 0)),
                    "ok":  bool(int(kv.get("ok", 0))),
                })
        except Exception:
            pass


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _sample(n: int = 15, interval: float = 0.06) -> tuple[float, float, float]:
    """Return averaged (roll, pitch, heading) over n samples."""
    rolls, pitches, hdgs = [], [], []
    for _ in range(n):
        rolls.append(_imu["roll"])
        pitches.append(_imu["pitch"])
        hdgs.append(_mag["hdg"])
        time.sleep(interval)
    # Circular mean for heading to handle 0/360 wrap
    sin_s = sum(math.sin(math.radians(h)) for h in hdgs)
    cos_s = sum(math.cos(math.radians(h)) for h in hdgs)
    hdg_avg = math.degrees(math.atan2(sin_s, cos_s)) % 360.0
    return sum(rolls) / len(rolls), sum(pitches) / len(pitches), hdg_avg


def _watch(seconds: int = 8) -> None:
    """Print live roll/pitch/heading for `seconds` seconds."""
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        r, p, h = _imu["roll"], _imu["pitch"], _mag["hdg"]
        remaining = max(0, int(end - time.monotonic()))
        print(
            f"\r  roll={r:+7.1f}°  pitch={p:+7.1f}°  heading={h:5.1f}°  [{remaining:2d}s] ",
            end="", flush=True,
        )
        time.sleep(0.1)
    print()


def _ask(prompt: str, choices: list[str]) -> str:
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
# .env writer — upserts the five calibration keys
# ---------------------------------------------------------------------------

def _write_env(env_path: Path, results: dict) -> None:
    key_map = {
        "IMU_ROLL_SIGN":       str(results["IMU_ROLL_SIGN"]),
        "IMU_PITCH_SIGN":      str(results["IMU_PITCH_SIGN"]),
        "IMU_SWAP_ROLL_PITCH": "true" if results["IMU_SWAP_ROLL_PITCH"] else "false",
        "MAG_HDG_OFFSET_DEG":  f"{results['MAG_HDG_OFFSET_DEG']:.1f}",
        "MAG_HDG_INVERT":      "true" if results["MAG_HDG_INVERT"] else "false",
    }

    existing = env_path.read_text().splitlines() if env_path.exists() else []
    written: set[str] = set()
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
    parser.add_argument("--baud",  type=int, default=int(os.getenv("SERIAL_BAUD", "57600")))
    parser.add_argument("--write", action="store_true", help="Write results to .env without asking")
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

    reader = threading.Thread(target=_reader, args=(port,), daemon=True)
    reader.start()

    # Wait for first valid data
    print("  Waiting for sensor data from ESP32 …", end="", flush=True)
    deadline = time.monotonic() + 6.0
    while time.monotonic() < deadline:
        if _imu["ok"] or _mag["ok"]:
            break
        time.sleep(0.1)
        print(".", end="", flush=True)
    print()

    if not _imu["ok"]:
        print("\n  WARNING: No IMU data ($IMU) received.")
        print("  Check I2C wiring on the moving side (GPIO18/5) and MPU-6050 address.")
    if not _mag["ok"]:
        print("\n  WARNING: No compass data ($MAG) received.")
        print("  Check I2C wiring on the moving side (GPIO18/5) and QMC5883L.")
    if not _imu["ok"] and not _mag["ok"]:
        print("\n  No sensor data at all — is the ESP32 running?")
        _stop.set()
        port.close()
        sys.exit(1)

    # ── Defaults ────────────────────────────────────────────────────────────
    results = {
        "IMU_ROLL_SIGN":       1,
        "IMU_PITCH_SIGN":      1,
        "IMU_SWAP_ROLL_PITCH": False,
        "MAG_HDG_OFFSET_DEG":  0.0,
        "MAG_HDG_INVERT":      False,
    }

    # ════════════════════════════════════════════════════════════════════════
    # Step 1 — Level baseline
    # ════════════════════════════════════════════════════════════════════════
    _section("Level baseline", 1, 5)
    print("  Place the camera head FLAT AND LEVEL, aimed straight forward.")
    print("  Watch the live readings while you settle it, then press ENTER.\n")
    try:
        _watch(6)
    except KeyboardInterrupt:
        pass
    input("  Press ENTER when stable and level … ")
    roll0, pitch0, hdg0 = _sample(20)
    print(f"\n  Baseline →  roll={roll0:+.1f}°  pitch={pitch0:+.1f}°  heading={hdg0:.1f}°")

    # Possible axis swap hint
    if abs(roll0) > 20 or abs(pitch0) > 20:
        print()
        print("  ⚠  One axis is far from 0° when level — likely an axis swap.")
        print("     We'll check that in Step 3.")

    # ════════════════════════════════════════════════════════════════════════
    # Step 2 — Roll axis check
    # ════════════════════════════════════════════════════════════════════════
    _section("Roll axis direction", 2, 5)
    print("  Slowly TILT THE CAMERA LEFT (left side down) and back to level.")
    print("  Watch which way ROLL moves:\n")
    try:
        _watch(8)
    except KeyboardInterrupt:
        print()

    print()
    print("  Convention: tilting LEFT should read NEGATIVE roll.")
    ans = _ask(
        "  When you tilted left, did roll go POSITIVE or NEGATIVE?  [pos / neg / skip]: ",
        ["pos", "negative", "neg", "positive", "skip"],
    )
    if ans in ("pos", "positive"):
        results["IMU_ROLL_SIGN"] = -1
        print("  → Roll is backwards.  Will set IMU_ROLL_SIGN=-1")
    elif ans in ("neg", "negative"):
        results["IMU_ROLL_SIGN"] = 1
        print("  → Roll direction is correct.  IMU_ROLL_SIGN=1")
    else:
        print("  → Skipped.  Keeping IMU_ROLL_SIGN=1")

    # ════════════════════════════════════════════════════════════════════════
    # Step 3 — Pitch axis check
    # ════════════════════════════════════════════════════════════════════════
    _section("Pitch axis direction", 3, 5)
    print("  Slowly TILT THE CAMERA FORWARD (lens/nose pointing down) and back.")
    print("  Watch which way PITCH moves:\n")
    try:
        _watch(8)
    except KeyboardInterrupt:
        print()

    print()
    print("  Convention: tilting FORWARD (nose down) should read NEGATIVE pitch.")
    ans = _ask(
        "  When you tilted forward, did pitch go POSITIVE or NEGATIVE?  [pos / neg / skip]: ",
        ["pos", "negative", "neg", "positive", "skip"],
    )
    if ans in ("pos", "positive"):
        results["IMU_PITCH_SIGN"] = -1
        print("  → Pitch is backwards.  Will set IMU_PITCH_SIGN=-1")
    elif ans in ("neg", "negative"):
        results["IMU_PITCH_SIGN"] = 1
        print("  → Pitch direction is correct.  IMU_PITCH_SIGN=1")
    else:
        print("  → Skipped.  Keeping IMU_PITCH_SIGN=1")

    # ════════════════════════════════════════════════════════════════════════
    # Step 4 — Axis swap check
    # ════════════════════════════════════════════════════════════════════════
    _section("Axis swap check", 4, 5)
    print("  Place the camera FLAT AND LEVEL again.")
    print("  Watch the readings for a few seconds:\n")
    try:
        _watch(5)
    except KeyboardInterrupt:
        print()

    roll_now, pitch_now, _ = _sample(10)
    print(f"\n  Level reading: roll={roll_now:+.1f}°  pitch={pitch_now:+.1f}°")
    print()

    # If both axes are near zero, no swap needed.  If one is large, likely swapped.
    if abs(roll_now) > 20 and abs(pitch_now) < 10:
        print("  ⚠  Roll is large but pitch is near zero at a level position.")
        print("     This suggests roll and pitch may be swapped.")
    elif abs(pitch_now) > 20 and abs(roll_now) < 10:
        print("  ⚠  Pitch is large but roll is near zero at a level position.")
        print("     This suggests roll and pitch may be swapped.")
    else:
        print("  Both axes look reasonable at level position.")

    print()
    ans = _ask(
        "  Did tilting FORWARD change ROLL (not pitch)?  [y / n / skip]: ",
        ["y", "n", "yes", "no", "skip"],
    )
    if ans in ("y", "yes"):
        results["IMU_SWAP_ROLL_PITCH"] = True
        print("  → Axes are swapped.  Will set IMU_SWAP_ROLL_PITCH=true")
    elif ans in ("n", "no"):
        results["IMU_SWAP_ROLL_PITCH"] = False
        print("  → Axes are correct.  IMU_SWAP_ROLL_PITCH=false")
    else:
        print("  → Skipped.  Keeping IMU_SWAP_ROLL_PITCH=false")

    # ════════════════════════════════════════════════════════════════════════
    # Step 5 — Compass heading
    # ════════════════════════════════════════════════════════════════════════
    _section("Compass heading and direction", 5, 5)
    print("  This step figures out the compass offset and whether it's inverted.")
    print()

    # ── Part A: direction (CW vs CCW) ──────────────────────────────────────
    print("  Part A — Rotation direction")
    print("  Point the camera in ANY direction and hold steady.")
    input("  Press ENTER when steady … ")
    _, _, hdg_a = _sample(15)
    print(f"  Reading: {hdg_a:.1f}°")

    print()
    print("  Now slowly rotate the gimbal 90° CLOCKWISE (to the right) and hold.")
    input("  Press ENTER when steady … ")
    _, _, hdg_b = _sample(15)
    print(f"  Reading: {hdg_b:.1f}°")

    cw_change = (hdg_b - hdg_a + 360) % 360   # always 0–360

    if 45 <= cw_change <= 135:
        # Increased by ~90° going CW → normal
        results["MAG_HDG_INVERT"] = False
        print(f"\n  Change: +{cw_change:.0f}°  →  compass rotates correctly clockwise.")
        print("  MAG_HDG_INVERT=false")
    elif 225 <= cw_change <= 315:
        # Decreased by ~90° going CW → inverted (shows as ~270° CW)
        results["MAG_HDG_INVERT"] = True
        print(f"\n  Change: {cw_change-360:.0f}°  →  compass is INVERTED (decreases when rotating right).")
        print("  MAG_HDG_INVERT=true")
    else:
        print(f"\n  ⚠  Ambiguous rotation change ({cw_change:.0f}°).  Was the rotation close to 90°?")
        ans = _ask("  Is the compass reading DECREASING when you rotate RIGHT? [y / n]: ",
                   ["y", "n", "yes", "no"])
        results["MAG_HDG_INVERT"] = ans in ("y", "yes")
        if results["MAG_HDG_INVERT"]:
            print("  MAG_HDG_INVERT=true")
        else:
            print("  MAG_HDG_INVERT=false")

    # ── Part B: north offset ───────────────────────────────────────────────
    print()
    print("  Part B — North offset")
    print("  Point the camera NORTH (use your phone compass as reference).")
    input("  Press ENTER when aimed north … ")
    _, _, hdg_north = _sample(20)
    print(f"  Heading when pointing north: {hdg_north:.1f}°  (expected: 0°)")

    # If inverted, the raw sensor reading for north after inversion becomes:
    if results["MAG_HDG_INVERT"]:
        effective_north = (360.0 - hdg_north) % 360.0
    else:
        effective_north = hdg_north

    # Offset needed to shift effective_north to 0
    offset = -effective_north
    if offset < -180:
        offset += 360
    elif offset > 180:
        offset -= 360
    results["MAG_HDG_OFFSET_DEG"] = round(offset, 1)
    print(f"  → MAG_HDG_OFFSET_DEG={results['MAG_HDG_OFFSET_DEG']}")

    # Verification
    corrected = (hdg_north
                 + (360.0 - hdg_north if results["MAG_HDG_INVERT"] else 0)
                 + results["MAG_HDG_OFFSET_DEG"]) % 360.0
    if results["MAG_HDG_INVERT"]:
        corrected = ((360.0 - hdg_north) + results["MAG_HDG_OFFSET_DEG"]) % 360.0
    else:
        corrected = (hdg_north + results["MAG_HDG_OFFSET_DEG"]) % 360.0
    print(f"  Corrected north reading will be: {corrected:.1f}°  (should be ~0°)")

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
        print("  Restart the service to apply:")
        print("    sudo systemctl restart ptz")
    else:
        print("\n  Not written.  Add the lines above to pi/.env manually.")

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
