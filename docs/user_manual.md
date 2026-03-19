# PTZ Controller — User Manual

**Firmware:** PTZ Controller v0.1.0
**Hardware:** ESP32 WROOM · MKS Servo42C · NEMA 17 steppers
**Controller:** Xbox One S (Bluetooth)

---

## Table of Contents

1. [What This System Does](#1-what-this-system-does)
2. [Hardware Overview](#2-hardware-overview)
3. [First-Time Setup](#3-first-time-setup)
4. [Using the Xbox Controller](#4-using-the-xbox-controller)
5. [Serial Command Interface (CLI)](#5-serial-command-interface-cli)
6. [Full Command Reference](#6-full-command-reference)
7. [Homing the System](#7-homing-the-system)
8. [Adjusting Speed and Limits](#8-adjusting-speed-and-limits)
9. [Saving Your Settings](#9-saving-your-settings)
10. [Troubleshooting](#10-troubleshooting)
11. [Specifications](#11-specifications)

---

## 1. What This System Does

The PTZ Controller moves a two-axis pan/tilt camera gimbal using two stepper
motors.  You can control it with an **Xbox One S controller** over Bluetooth,
or by typing commands into a **serial terminal** connected over USB.

The motors are **closed-loop** — each driver has a built-in magnetic sensor
that detects if the motor slips and corrects immediately, giving smooth and
accurate positioning even under load.

---

## 2. Hardware Overview

| Component | Details |
|---|---|
| Brain | ESP32 WROOM-32 (Wi-Fi / Bluetooth) |
| Pan motor | NEMA 17 stepper, 1.8 °/step |
| Tilt motor | NEMA 17 stepper, 1.8 °/step |
| Motor drivers | MKS Servo42C (closed-loop, 16× microstep) |
| Pan gear ratio | 144 : 17 (~8.47 : 1) |
| Tilt gear ratio | 64 : 21 (~3.05 : 1) |
| Wireless input | Xbox One S via Bluetooth Classic |
| Status indicator | Single LED on GPIO 2 |

**LED meanings:**

| Pattern | Meaning |
|---|---|
| Slow blink (1 Hz) | Idle — no controller connected, or motors stopped |
| Solid on | Moving |
| Double pulse (~1 Hz) | Homing in progress |
| Fast blink (8 Hz) | Error or emergency stop active |

---

## 3. First-Time Setup

### What you need
- A computer with a USB-A or USB-C port
- A USB cable to the ESP32
- A serial terminal program (e.g. PuTTY, Tera Term, or the built-in monitor in
  PlatformIO / Arduino IDE)
- An **Xbox One S** controller with fresh batteries

### Steps

1. **Connect the ESP32 to your computer** via USB.

2. **Open a serial terminal** at **115200 baud**, 8N1.

3. **Power on the motor drivers** (the MKS Servo42C boards need a separate
   12–24 V supply).

4. **Press the ESP32 reset button** (or power it on).  You should see:

   ```
   PTZ Controller v0.1.0  booting...
   [BOOT] MotionController OK
   [BOOT] Bluepad32 OK — waiting for controller...
   [BOOT] CLI OK
   PTZ Controller v0.1.0 ready
   Type 'help' for available commands.
   ptz>
   ```

5. **Home the axes** before using the gimbal for the first time:

   ```
   ptz> home all
   ```

   The motors will spin slowly to find their reference positions.  Wait until
   the `status` command shows both axes as `[idle]`.

6. **Pair the Xbox controller:**
   - Hold the controller's pairing button (small button on top, next to the
     USB port) until the Xbox button flashes rapidly.
   - The ESP32 will find it automatically.  The serial terminal will print
     `[INPUT] Controller connected` when it is ready.

7. **You're done.**  Move the left stick to pan and tilt the gimbal.

---

## 4. Using the Xbox Controller

```
             ┌─────────────────────────────────┐
             │  Y = Home all axes              │
             │  X = (see CLI for zero)         │
             │  A = Stop all motion            │
             │  B = EMERGENCY STOP             │
             │                                 │
             │  Left stick = Pan + Tilt        │
             │  Hold RB    = Fine-speed mode   │
             │  Menu/Start = Toggle limits     │
             └─────────────────────────────────┘
```

### Stick directions

| Stick | Direction | Result |
|---|---|---|
| Left stick → right | Pan right |
| Left stick → left | Pan left |
| Left stick ↑ up | Tilt up |
| Left stick ↓ down | Tilt down |

### Buttons

| Button | Action |
|---|---|
| **A** | Stop both axes smoothly (decelerates) |
| **B** | **Emergency stop** — cuts motion instantly.  Re-enable with `enable all` in the CLI. |
| **Y** | Home both axes using the hall-effect sensors |
| **RB** (hold) | Fine-speed mode — slows movement to ~15% of normal speed for precise framing |
| **Menu / Start** | Toggle soft travel limits on/off |

### Tips

- **Fine mode** is great for making small adjustments during a live shot.
  Hold RB and nudge the stick for frame-accurate moves.
- If the gimbal hits a hard stop and the motor stutters, the closed-loop
  system will detect the stall.  Press **A** to stop, then **home** the
  affected axis to re-establish position reference.
- The controller auto-stops both axes when it disconnects (e.g. low battery).

---

## 5. Serial Command Interface (CLI)

Connect with any terminal at **115200 baud**.  The prompt looks like:

```
ptz>
```

Type a command and press Enter.  Commands are case-sensitive and lowercase.

### Quick examples

```bash
# Show current positions and settings
ptz> status

# Move pan to 45 degrees (absolute)
ptz> move pan 45.0

# Move tilt 10 degrees downward from current position
ptz> move tilt -10.0 --relative

# Home tilt axis only
ptz> home tilt

# Set max speed to 30 degrees per second
ptz> set speed 30.0

# Save all settings so they survive a reboot
ptz> save
```

---

## 6. Full Command Reference

---

### `help`

```
help [command]
```

Without arguments, lists all commands.  With a command name, shows detailed
usage for that command.

```
ptz> help
ptz> help set
ptz> help move
```

---

### `version`

Shows the firmware version and build date.

```
ptz> version
PTZ Controller  version 0.1.0  (built Mar 17 2026 20:00:00)
```

---

### `status`

Shows a snapshot of both axes: position, motion state, limits, and settings.

```
ptz> status
```

---

### `home [pan|tilt|all]`

Starts the homing sequence for one or both axes.  The MKS driver uses its
built-in hall-effect sensor to find a repeatable reference point.

- The motor moves slowly until it finds the magnetic home position.
- When complete, the position counter resets to 0°.
- You can monitor progress with `status`.

```
ptz> home all      # home both axes
ptz> home pan      # home pan only
ptz> home tilt     # home tilt only
```

> **Note:** Always home after powering on if you need accurate position
> readback.  The system does not remember its position across power cycles.

---

### `move <pan|tilt> <degrees> [--relative]`

Moves an axis to a position.

- Default: **absolute** — moves to the specified angle from the home position.
- `--relative`: treats the angle as an offset from the **current** position.
- Soft limits are enforced; the move will be clamped if it would exceed them.

```
ptz> move pan 90.0           # pan to 90° (absolute)
ptz> move tilt -30.0         # tilt to -30° (absolute)
ptz> move pan 15.0 --relative  # pan 15° further right
```

---

### `stop [pan|tilt|all]`

Decelerates an axis to a smooth stop.  Does **not** disable the motor — it
remains energised and will hold its position.

```
ptz> stop          # same as: stop all
ptz> stop pan
```

---

### `estop`

**Immediate hard stop** — no deceleration ramp.  Both axes halt instantly.

After an emergency stop, the motors are still energised but will not accept
motion commands.  To resume:

```
ptz> enable all
```

---

### `enable [pan|tilt|all]`
### `disable [pan|tilt|all]`

Turns the motor output stage on or off.

- **Enabled:** motor holds position.
- **Disabled:** motor shaft spins freely (good for manually positioning the
  gimbal by hand).

```
ptz> disable all   # free-wheel both axes
ptz> enable all    # re-energise and hold position
```

---

### `set speed <deg/s>`

Sets the maximum output-shaft speed in degrees per second.  Default: 60 °/s.

```
ptz> set speed 45.0
```

---

### `set accel <deg/s²>`

Sets the acceleration ramp rate.  A lower value gives smoother starts and
stops; a higher value gives snappier response.  Default: 30 °/s².

```
ptz> set accel 20.0
```

---

### `set fine <scale>`

Sets the fine-speed multiplier used when holding RB on the controller.
Value must be between 0.01 and 1.0.  Default: 0.15 (15% of max speed).

```
ptz> set fine 0.10   # 10% of max speed in fine mode
```

---

### `set limits <pan|tilt> <min> <max>`
### `set limits on|off`

Configures soft travel limits for an axis, or enables/disables limit
enforcement globally.

```
ptz> set limits pan -90.0 90.0
ptz> set limits tilt -30.0 60.0
ptz> set limits off     # disable limits (allows full range of motion)
ptz> set limits on      # re-enable
```

---

### `get <parameter>`

Reads back a setting or sensor value.

| Parameter | Description |
|---|---|
| `position` | Current position of both axes in degrees |
| `speed` | Max speed setting |
| `accel` | Acceleration setting |
| `limits` | Soft limit values for both axes |
| `encoder [pan\|tilt]` | Raw encoder angle from the MKS driver |

```
ptz> get position
ptz> get encoder pan
ptz> get limits
```

---

### `ping [pan|tilt|all]`

Tests the UART communication link to the MKS driver.  Useful for diagnosing
wiring issues.

```
ptz> ping all
ping pan  ... OK
ping tilt ... OK
```

If you see `TIMEOUT`, check:
1. The UART wiring between the ESP32 and the MKS driver.
2. That the driver is powered on.
3. That the baud rate (38400) matches the driver's DIP switch setting.

---

### `cal [pan|tilt|all]`

Runs the MKS encoder calibration routine.  The motor rotates exactly one
full revolution and the driver maps the magnetic sensor readings to position.

**When to use:** After first installing a motor/driver, or if you see
erratic closed-loop behaviour.

```
ptz> cal pan
Calibrating pan encoder (motor will rotate one revolution)...
pan calibration complete
```

> **Warning:** The motor will move.  Make sure the axis has a full revolution
> of clear space before running calibration.

---

### `save`

Saves the current settings (speed, accel, fine scale, soft limits) to the
ESP32's built-in flash storage.  Settings survive power cycles.

```
ptz> save
Settings saved to flash
```

---

### `reset`

Restores all settings to factory defaults **in memory only** — does not save
to flash.  Run `save` afterward if you want the defaults to persist.

---

### `reboot`

Restarts the ESP32.

---

## 7. Homing the System

Homing is the process of finding a known reference position so the system
knows where the gimbal is pointing.

### When to home

- **After powering on** — the position counter starts at 0 but that 0 may not
  align with physical reality until you home.
- **After an emergency stop** that caused the motor to slip.
- **After manually repositioning** the gimbal with motors disabled.

### Homing procedure

1. Make sure both axes have a clear path to rotate to their home position
   (the home position is defined by the hall-effect sensor magnet placement
   on each motor).
2. Type `home all` or press **Y** on the Xbox controller.
3. Watch the LED — it will pulse with a double-blink pattern while homing.
4. When the LED returns to slow-blink (idle), homing is complete.
5. Confirm with `get position` — both axes should read near 0.0°.

---

## 8. Adjusting Speed and Limits

### Speed

- Start with the default 60 °/s and reduce if motion feels too fast for your
  application.
- For broadcast camera work, 20–40 °/s is often more appropriate.
- Fine-speed mode (hold RB) gives ~15% of max speed; adjust with `set fine`.

### Soft limits

Soft limits prevent the gimbal from commanding a position that could damage
the rig.  They are **not** hardware limit switches — if the motor slips or
power is lost, the limits offer no mechanical protection.

Set limits conservatively — slightly inside the physical range:

```
ptz> set limits pan -170.0 170.0   # 10° inside each mechanical hard stop
ptz> set limits tilt -40.0 85.0
ptz> save
```

---

## 9. Saving Your Settings

Settings are **not automatically saved**.  After making changes, always run:

```
ptz> save
```

Settings saved include: speed, acceleration, fine-speed scale, and soft limits
for both axes.

---

## 10. Troubleshooting

### Controller won't connect

- Make sure the controller has good batteries.
- Hold the pairing button on top of the controller until the Xbox button
  flashes rapidly.
- If it still won't connect, reboot the ESP32 (`reboot` command or press the
  reset button) — this clears any stale Bluetooth pairing data.

### Motors not responding / axis always at 0°

- Run `ping all` to verify UART communication to the drivers.
- Check that the motor driver power supply is on.
- Run `enable all` to make sure the output stage is active.

### Position drifts over time

- The position counter is based on step pulses, not on the encoder.  Some
  drift is normal over long sessions.
- Home periodically to re-synchronise: `home all`.

### `TIMEOUT` on ping / cal / home

- Check wiring: confirm TX and RX are not swapped.
- Confirm baud rate: default is 38400.  If you changed the driver's baud
  rate via its DIP switches, update `MKS_BAUD_RATE` in `config.h` and
  rebuild.

### Stuttering or missed steps under load

- The MKS Servo42C will detect stalls automatically.  If it trips, the
  driver's status LED will indicate an error.
- Reduce speed: `set speed 30.0`
- Reduce acceleration: `set accel 15.0`
- Check that the motor working current matches your NEMA 17 spec.

### Emergency stop triggered accidentally

- Press **A** (stop) on the controller, or type `estop` in the CLI to clear
  the flag, then `enable all` to resume.

---

## 11. Specifications

| Parameter | Value |
|---|---|
| Pan range (default) | −180 ° to +180 ° |
| Tilt range (default) | −45 ° to +90 ° |
| Pan gear ratio | 144 : 17 (8.47 : 1) |
| Tilt gear ratio | 64 : 21 (3.05 : 1) |
| Motor step angle | 1.8 ° (200 steps/rev) |
| Microstep setting | 16× |
| Pan resolution | ~75.3 steps / ° at output shaft |
| Tilt resolution | ~27.1 steps / ° at output shaft |
| Default max speed | 60 °/s at output shaft |
| Default acceleration | 30 °/s² |
| MKS UART baud rate | 38400 |
| CLI baud rate | 115200 |
| Bluetooth protocol | Bluetooth Classic (BR/EDR) |
| Supported controller | Xbox One S |
| MCU | ESP32 WROOM-32 |
| Flash storage | NVS (non-volatile storage) for settings |
