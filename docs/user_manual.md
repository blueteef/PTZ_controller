# PTZ Controller — User Manual

**Firmware:** PTZ Controller v0.2.0
**Hardware:** ESP32 WROOM-32 · A4988 stepper drivers · NEMA 17 steppers
**Control:** Serial terminal (USB or Pi UART) · Keyboard jog (WASD) · Pi remote

---

## Quick Start — Common Commands

After SSH into the Pi:

```bash
# Pull latest code
git pull

# Activate virtual environment
source ~/venv/bin/activate

# Start the server
cd ~/PTZ_controller/pi
uvicorn app.main:app --host 0.0.0.0 --port 8000
```

---

## Table of Contents

1. [What This System Does](#1-what-this-system-does)
2. [Hardware Overview](#2-hardware-overview)
3. [Wiring Reference](#3-wiring-reference)
4. [First-Time Setup](#4-first-time-setup)
5. [Keyboard Jog Control](#5-keyboard-jog-control)
6. [Full Command Reference](#6-full-command-reference)
7. [Pi Remote Control](#7-pi-remote-control)
8. [Adjusting Speed and Limits](#8-adjusting-speed-and-limits)
9. [Saving Your Settings](#9-saving-your-settings)
10. [Troubleshooting](#10-troubleshooting)
11. [Specifications](#11-specifications)

---

## 1. What This System Does

The PTZ Controller moves a two-axis pan/tilt camera gimbal using two NEMA 17
stepper motors driven by A4988 breakout boards.

Control options:
- **Keyboard jog** — type `jog` in a serial terminal and use WASD keys for
  real-time control.
- **CLI commands** — type structured commands (`move`, `vel`, `stop`, etc.)
  over USB or directly from the Raspberry Pi.
- **Pi remote** — the Raspberry Pi sends velocity commands over a direct GPIO
  UART link for autonomous tracking.

Position is tracked by counting step pulses.  There is no encoder or homing
sensor — on power-up, both axes start at 0° by definition.

---

## 2. Hardware Overview

| Component | Details |
|---|---|
| Brain | ESP32 WROOM-32 |
| Pan motor | NEMA 17 stepper, 1.8°/step |
| Tilt motor | NEMA 17 stepper, 1.8°/step |
| Motor drivers | A4988 breakout (open-loop, 16× microstep) |
| Pan gear ratio | 144 : 17 (~8.47 : 1) |
| Tilt gear ratio | 64 : 21 (~3.05 : 1) |
| Pi link | Hardware UART — GPIO16/17 on ESP32, GPIO14/15 on Pi |
| USB | Available for debugging / development |
| Status LED | GPIO 2 |

**LED meanings:**

| Pattern | Meaning |
|---|---|
| Slow blink (1 Hz) | Idle — motors stopped |
| Solid on | Moving |
| Fast blink (8 Hz) | Emergency stop active |

---

## 3. Wiring Reference

### A4988 per axis

| A4988 Pin | ESP32 Pin | Notes |
|---|---|---|
| STEP (Pan) | GPIO 32 | Step pulse |
| DIR (Pan) | GPIO 33 | Direction |
| EN (Pan) | GPIO 25 | Active LOW — driven LOW at boot |
| STEP (Tilt) | GPIO 26 | |
| DIR (Tilt) | GPIO 27 | |
| EN (Tilt) | GPIO 14 | |
| MS1, MS2, MS3 | 3.3 V | Hardwire for 16× microstepping |
| SLEEP, RESET | 3.3 V | Must be HIGH — bridge together if needed |
| VMOT | 12–24 V supply | Motor power |
| GND | Common GND | |

### Pi UART link (replaces USB cable)

| Signal | Pi GPIO | ESP32 GPIO |
|---|---|---|
| TX → RX | GPIO 14 (TX) | GPIO 16 (RX) |
| RX ← TX | GPIO 15 (RX) | GPIO 17 (TX) |
| GND | GND | GND |

> Both sides are 3.3 V logic — no level shifter needed.

**Pi one-time setup:**
```bash
sudo raspi-config
# Interface Options → Serial Port
# "Login shell over serial?" → No
# "Serial port hardware enabled?" → Yes
# Reboot
```

---

## 4. First-Time Setup

### What you need

- USB cable to ESP32 (for initial setup and debugging)
- A serial terminal at **115200 baud** (PuTTY, Tera Term, PlatformIO monitor)
- A4988 drivers wired per the table above
- 12–24 V supply for the motor drivers

### Steps

1. **Connect the ESP32 via USB** and open a terminal at 115200 baud.

2. **Power on the motor driver supply.**

3. **Press the ESP32 reset button.**  You should see:

   ```
   PTZ Controller v0.2.0  booting...
   [BOOT] MotionController OK
   [BOOT] Pi UART OK (GPIO16 RX, GPIO17 TX)
   [BOOT] CLI OK
   PTZ Controller v0.2.0 ready
   Type 'help' for commands.  'jog' for keyboard control.
   ptz>
   ```

4. **Test motion** — type `jog` and press D briefly.  The pan motor should
   make a short move.  If the direction is wrong, set `PAN_DIR_INVERT true`
   in `config.h` and reflash.

5. **Set a comfortable speed** for your rig:

   ```
   ptz> set speed 45
   ptz> set accel 180
   ptz> save
   ```

> **Note:** There is no homing.  On every power-up both axes reset to 0°.
> This is the physical position the gimbal happened to be in at boot time.
> Use `move pan 0` to return to that reference point.

---

## 5. Keyboard Jog Control

Jog mode gives real-time WASD keyboard control directly from a serial terminal.
It works via key-repeat: holding a key keeps the axis moving; releasing it
stops the motor within ~150 ms.

```
ptz> jog [speed]
```

`speed` is optional — defaults to the current max speed setting.

### Jog keys

| Key | Action |
|---|---|
| **W** | Tilt up |
| **S** | Tilt down |
| **A** | Pan left |
| **D** | Pan right |
| **F** | Toggle fine-speed mode (20% of current speed) |
| **+** / **=** | Increase speed by 10 °/s |
| **-** | Decrease speed by 10 °/s |
| **Space** | Stop all motion immediately |
| **Q** or **ESC** | Exit jog mode, return to `ptz>` prompt |

### Example

```
ptz> jog 30
── Jog mode ───────────────────────────────────────────────
  W/S = tilt ±     A/D = pan ±     SPC = stop
  F   = toggle fine (20%×)     +/- = adjust speed
  Q or ESC = exit
  Speed: 30 °/s
───────────────────────────────────────────────────────────
```

Hold **D** → pan moves right.  Release **D** → pan stops.

---

## 6. Full Command Reference

---

### `help`

```
ptz> help
ptz> help set
```

---

### `version`

Shows firmware version and build date.

---

### `status`

Shows position, motion state, limits, and current settings for both axes.

```
ptz> status
┌─ Pan  ──────────────────────────────
│  pos  :     0.00 °   [idle]
│  limits: [-180.0, 180.0] ° (off)
├─ Tilt ──────────────────────────────
│  pos  :     0.00 °   [idle]
│  limits: [-45.0, 90.0] ° (off)
├─ Motion ────────────────────────────
│  speed : 90.0 °/s
│  accel : 360.0 °/s²
│  fine  : 0.20×  e-stop: no
└─────────────────────────────────────
```

---

### `jog [speed]`

Enter real-time WASD keyboard control.  See [Section 5](#5-keyboard-jog-control).

---

### `vel <pan|tilt|all> <deg/s>`

Sets a continuous velocity on an axis.  Positive = CW/up, negative = CCW/down.
Stop with `vel pan 0` or `stop`.

This is the primary interface used by the Pi tracking loop.

```
ptz> vel pan 30
ptz> vel tilt -15
ptz> vel all 0
```

---

### `move <pan|tilt> <degrees> [--relative]`

Moves an axis to an absolute position, or by a relative offset.

```
ptz> move pan 90.0
ptz> move tilt -30.0
ptz> move pan 15.0 --relative
```

Soft limits are enforced — the target will be clamped if limits are enabled.

---

### `stop [pan|tilt|all]`

Decelerates an axis to a smooth stop.  Motor stays energised and holds position.

```
ptz> stop
ptz> stop pan
```

---

### `estop`

Immediate hard stop — no deceleration.  Sets a latch that blocks all motion
commands until cleared.

To resume after an estop:

```
ptz> enable all
```

---

### `enable [pan|tilt|all]`
### `disable [pan|tilt|all]`

- **enable** — energises the motor (EN LOW on A4988).  Also clears the estop latch.
- **disable** — de-energises the motor (EN HIGH).  Shaft spins freely — useful
  for manually repositioning the gimbal.

```
ptz> disable all   # free-wheel for manual positioning
ptz> enable all    # re-energise and hold position
```

---

### `set speed <deg/s>`

Max output-shaft speed.  Default: 90 °/s.

```
ptz> set speed 45.0
```

---

### `set accel <deg/s²>`

Acceleration ramp rate.  Higher = snappier; lower = smoother.  Default: 360 °/s².

```
ptz> set accel 180.0
```

---

### `set fine <scale>`

Fine-speed multiplier used in jog mode (press F).  Must be 0.01–1.0.
Default: 0.20 (20% of max speed).

```
ptz> set fine 0.15
```

---

### `set limits <pan|tilt> <min> <max>`
### `set limits on|off`

Soft travel limits.  Disabled by default — enable once you know your
gimbal's safe range of motion.

```
ptz> set limits pan -170.0 170.0
ptz> set limits tilt -40.0 85.0
ptz> set limits on
ptz> save
```

---

### `get <parameter>`

| Parameter | Description |
|---|---|
| `position` | Current position of both axes in degrees |
| `speed` | Max speed setting |
| `accel` | Acceleration setting |
| `limits` | Soft limit values and enabled state |

```
ptz> get position
ptz> get limits
```

---

### `save`

Saves speed, accel, fine scale, and soft limits to flash.  Survives reboots.

```
ptz> save
```

---

### `reset`

Restores all settings to factory defaults in RAM only.  Run `save` to persist.

---

### `reboot`

Restarts the ESP32.

---

## 7. Pi Remote Control

The Raspberry Pi communicates with the ESP32 over a direct 3-wire GPIO UART
link — no USB cable required.  See [Section 3](#3-wiring-reference) for wiring.

### Starting the server

```bash
source ~/venv/bin/activate
cd ~/PTZ_controller/pi
uvicorn app.main:app --host 0.0.0.0 --port 8000
```

### Python example (direct serial)

```python
import serial

esp = serial.Serial('/dev/serial0', 115200, timeout=0.1)

# Velocity control (tracking loop — call at 20–50 Hz)
esp.write(b'vel pan 25.0\n')
esp.write(b'vel tilt -10.0\n')

# Stop
esp.write(b'stop all\n')

# Query position
esp.write(b'get position\n')
response = esp.readline().decode()
```

The ESP32 accepts the same CLI commands on the Pi UART as it does on USB.
The `vel` command prints a brief confirmation line; responses from `get` and
`status` are also echoed back on the Pi UART if needed.

### Tracking mode

The Pi vision pipeline sends `vel` commands to keep a detected target centred.
The ESP32 does not need to be modified — it just executes velocity commands
as fast as they arrive.  USB stays available for monitoring / debugging.

---

## 8. Adjusting Speed and Limits

### Speed tuning

Start at the defaults and reduce if motion is too aggressive:

```
ptz> set speed 45
ptz> set accel 180
ptz> save
```

For smooth cinematic moves: 20–40 °/s, accel 60–120 °/s².
For fast tracking response: 90+ °/s, accel 360+ °/s².

Fine-mode (F in jog) runs at `maxSpeed × fineScale`.  Default is 20%, giving
18 °/s at the default 90 °/s max — good for precise framing.

### Soft limits

A4988 drivers are open-loop — there is no stall detection.  Set soft limits
conservatively inside the physical range to prevent the gimbal from hitting
hard stops:

```
ptz> set limits pan -170.0 170.0
ptz> set limits tilt -40.0 85.0
ptz> set limits on
ptz> save
```

---

## 9. Saving Your Settings

Settings are **not automatically saved**.  After making changes:

```
ptz> save
```

Saved settings: speed, acceleration, fine scale, soft limits (both axes).

---

## 10. Troubleshooting

### Motor whines but doesn't move

Most common cause: **A4988 SLEEP or RESET pin not pulled HIGH.**
- Bridge SLEEP and RESET together and connect to 3.3 V (or VMOT depending on board).
- Check MS1/MS2/MS3 are all tied HIGH for 16× microstepping.
- Verify the STEP wire is connected — the motor energises without it but won't step.

### Motor moves in the wrong direction

Set `PAN_DIR_INVERT true` or `TILT_DIR_INVERT true` in `config.h` and reflash.
No rewiring needed.

### Motor moves but position counter stays at 0°

Normal — the step counter updates only while running.  Send `vel pan 10`,
then `status`, and you should see the position changing.  If not, file a bug.

### No response from Pi UART

- Confirm raspi-config has the login shell disabled and hardware serial enabled.
- Check TX/RX are not swapped (Pi GPIO14 TX → ESP32 GPIO16 RX).
- Pi and ESP32 must share a common GND.
- Verify with: `echo "status" > /dev/serial0` from the Pi.

### Estop won't clear

```
ptz> enable all
```

This clears the estop latch and re-enables both drivers.

### Missed steps / losing position under load

- Reduce speed: `set speed 45`
- Increase acceleration gradually (counterintuitively, too-slow acceleration
  can also cause resonance stalls on A4988).
- Check motor current — Vref on the A4988 trimmer.  For most NEMA 17s,
  Vref ≈ 0.5–0.8 V (check your motor's rated current and driver formula).
- Add a heatsink to the A4988 if it is hot to the touch.

---

## 11. Specifications

| Parameter | Value |
|---|---|
| Pan range (default) | −180° to +180° |
| Tilt range (default) | −45° to +90° |
| Pan gear ratio | 144 : 17 (8.47 : 1) |
| Tilt gear ratio | 64 : 21 (3.05 : 1) |
| Motor step angle | 1.8° (200 steps/rev) |
| Microstepping | 16× (hardwired MS1/MS2/MS3 HIGH) |
| Pan resolution | ~75.3 steps/° at output shaft |
| Tilt resolution | ~27.1 steps/° at output shaft |
| Default max speed | 90 °/s at output shaft |
| Default acceleration | 360 °/s² |
| Default fine scale | 0.20× (20% of max speed) |
| CLI baud rate | 115200 (USB and Pi UART) |
| Pi UART pins | ESP32 GPIO16 (RX) / GPIO17 (TX) |
| Pi GPIO pins | GPIO14 (TX) / GPIO15 (RX) |
| MCU | ESP32 WROOM-32 |
| Flash settings | NVS (non-volatile) — persisted with `save` |
