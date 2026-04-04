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


def _watch_until_enter() -> None:
    """Print live roll/pitch/heading until the user presses Enter."""
    stop = threading.Event()

    def _display() -> None:
        while not stop.is_set():
            r, p, h = _imu["roll"], _imu["pitch"], _mag["hdg"]
            print(
                f"\r  roll={r:+7.1f}°  pitch={p:+7.1f}°  heading={h:5.1f}°  [press ENTER when done]  ",
                end="", flush=True,
            )
            time.sleep(0.1)
        print()

    t = threading.Thread(target=_display, daemon=True)
    t.start()
    input()
    stop.set()
    t.join(timeout=0.5)


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
    _watch_until_enter()
    roll0, pitch0, hdg0 = _sample(20)
    print(f"\n  Baseline →  roll={roll0:+.1f}°  pitch={pitch0:+.1f}°  heading={hdg0:.1f}°")

    if abs(roll0) > 20 or abs(pitch0) > 20:
        print()
        print("  ⚠  One axis is far from 0° when level — possible axis swap.")
        print("     Step 2 will sort it out.")

    # ════════════════════════════════════════════════════════════════════════
    # Step 2 — Tilt motor test  (detects axis swap + pitch sign in one go)
    # ════════════════════════════════════════════════════════════════════════
    _section("Tilt motor test — axis swap + pitch direction", 2, 4)
    print("  The TILT motor is the only axis we can move reliably, so we use it")
    print("  for everything: which IMU axis is pitch, is it swapped with roll,")
    print("  and which direction is positive.")
    print()
    print("  Use the TILT motor to tilt the camera FORWARD (nose/lens pointing DOWN).")
    print("  Go as far as you can, hold it there, and press ENTER.\n")
    _watch_until_enter()

    roll_fwd, pitch_fwd, _ = _sample(15)
    roll_delta  = roll_fwd  - roll0
    pitch_delta = pitch_fwd - pitch0
    print(f"\n  Change from baseline →  roll: {roll_delta:+.1f}°   pitch: {pitch_delta:+.1f}°")

    # Determine which axis moved — that tells us swap status
    if abs(pitch_delta) >= abs(roll_delta):
        results["IMU_SWAP_ROLL_PITCH"] = False
        print("  Pitch changed more than roll → axes are correct, no swap needed.")
        print("  IMU_SWAP_ROLL_PITCH=false")
        # Pitch sign: nose down should be negative
        if pitch_delta > 0:
            results["IMU_PITCH_SIGN"] = -1
            print("  Pitch went POSITIVE nose-down → will flip.  IMU_PITCH_SIGN=-1")
        else:
            results["IMU_PITCH_SIGN"] = 1
            print("  Pitch went NEGATIVE nose-down → correct direction.  IMU_PITCH_SIGN=1")
    else:
        results["IMU_SWAP_ROLL_PITCH"] = True
        print("  Roll changed more than pitch → axes are SWAPPED.")
        print("  IMU_SWAP_ROLL_PITCH=true")
        # After swap, roll_delta is actually the pitch reading — apply sign fix to it
        if roll_delta > 0:
            results["IMU_PITCH_SIGN"] = -1
            print("  (swapped) Roll went POSITIVE nose-down → will flip.  IMU_PITCH_SIGN=-1")
        else:
            results["IMU_PITCH_SIGN"] = 1
            print("  (swapped) Roll went NEGATIVE nose-down → correct direction.  IMU_PITCH_SIGN=1")

    # ════════════════════════════════════════════════════════════════════════
    # Step 3 — Roll sign
    # ════════════════════════════════════════════════════════════════════════
    _section("Roll sign", 3, 4)
    print("  Roll is side-to-side tilt.  The gimbal has no roll motor, so there's")
    print("  no reliable way to test it with just pan/tilt axes.")
    print()
    print("  Options:")
    print("    a) Skip — default IMU_ROLL_SIGN=1 is usually correct.")
    print("    b) Physically tip the entire rig sideways RIGHT now by lifting")
    print("       one side of the base/tripod, then press ENTER to record it.")
    print()
    ans = _ask("  Tip the rig sideways now, or skip?  [tip / skip]: ", ["tip", "skip"])

    if ans == "tip":
        print()
        print("  Slowly tip the rig so the LEFT SIDE goes DOWN, hold, press ENTER.\n")
        _watch_until_enter()
        roll_tip, _, _ = _sample(15)
        roll_tip_delta = roll_tip - roll0
        print(f"\n  Roll delta from baseline: {roll_tip_delta:+.1f}°")
        if abs(roll_tip_delta) < 5:
            print("  ⚠  Roll barely changed — the rig may not have tipped enough,")
            print("     or this axis doesn't respond to that tilt. Keeping IMU_ROLL_SIGN=1.")
        elif roll_tip_delta > 0:
            # Left side down → positive reading → backwards from convention
            results["IMU_ROLL_SIGN"] = -1
            print("  Left side down → positive roll → will flip.  IMU_ROLL_SIGN=-1")
        else:
            results["IMU_ROLL_SIGN"] = 1
            print("  Left side down → negative roll → correct direction.  IMU_ROLL_SIGN=1")
    else:
        print("  Skipped — keeping IMU_ROLL_SIGN=1.")
        print("  If the dashboard roll reads backwards, add IMU_ROLL_SIGN=-1 to .env.")

    # ════════════════════════════════════════════════════════════════════════
    # Step 5 — Compass heading
    # ════════════════════════════════════════════════════════════════════════
    _section("Compass heading and direction", 4, 4)
    print("  This step figures out the compass offset and whether it's inverted.")
    print()

    # ── Part A: direction (CW vs CCW) ──────────────────────────────────────
    print("  Part A — Rotation direction")
    print("  Point the camera in ANY direction and hold steady.")
    input("  Press ENTER when steady … ")
    _, _, hdg_a = _sample(15)
    print(f"  Reading: {hdg_a:.1f}°")

    print()
    print("  Now use the PAN motor to rotate ~90° CLOCKWISE (to the right) and hold.")
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
