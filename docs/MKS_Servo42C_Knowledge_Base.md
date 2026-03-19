# MKS Servo42C Driver Knowledge Base

*Last updated: 2026-03-18*

---

## Hardware Summary

| Axis | Driver Firmware | Address | UART Port | Notes |
|------|----------------|---------|-----------|-------|
| Pan  | V3.6.1 (newer) | 0xE0    | Serial2 (TX=17, RX=16) | Sends 32-byte response to 0x3A status query |
| Tilt | Unknown/older  | 0xE1    | Serial1 (TX=4,  RX=5)  | Sends standard 2-byte response to 0x3A |

- **Baud rate:** 38400
- **Frame format (downlink):** `[Addr][Func][Data...][tCHK]`
- **Frame format (uplink):**   `[Addr][Func][Data...][tCHK]` (func IS echoed back)
- **tCHK:** 8-bit sum of all preceding bytes (addr + func + data)
- **Response checksum** is NOT verified in firmware — addr+func echo validation used instead
- **Wiring:** Use the 4-pin UART header (Tx / Rx / G / 3V3), NOT the COM pin on the step/dir signal connector
  - COM pin is either floating (standard variant) or optocoupler supply voltage (OC variant) — it is NOT a data line
  - Cross-connect: ESP32 TX → Driver Rx, ESP32 RX → Driver Tx
  - 3.3 V TTL; no level shifter required

---

## Work Mode

Both drivers are configured in **CR_UART mode** (work mode 0x02).

In CR_UART mode the driver accepts motion commands over UART and ignores step/dir pulses.

---

## What WORKS via UART (CR_UART / V3.6.1)

| Function Code | Name | Notes |
|---------------|------|-------|
| 0x30 | Read encoder angle | Returns 6 bytes on newer fw, 2 bytes on older fw |
| 0x32 | Read speed (RPM) | Works |
| 0x33 | Read pulse count | Works |
| 0x39 | Read shaft error | Works |
| 0x3A | Read status byte | Works — but pan (V3.6.1) returns **32 bytes** instead of expected 2; leftover bytes may contaminate next read |
| 0x84 | Set microsteps | Works — valid values: 1,2,4,8,16,32,64,128,256 |
| 0xF3 | Enable/disable driver | Works |
| 0xF6 | Run velocity (constant speed) | Works — primary motion command used in CR_UART mode |
| 0xF7 | Stop (decelerate to halt) | Works |
| 0xFD | Run N microsteps | Works |
| 0xFF 0xC8 | Save F6 startup velocity | Works — 0xC8 data byte required; 0xFF alone returns 0x00 failure |

---

## What DOES NOT WORK via UART (V3.6.1 CR_UART firmware restriction)

The following commands are **rejected silently** (return NO RESPONSE or echo with status 0x00) when sent via UART in V3.6.1 CR_UART mode. These settings **must be configured via the physical OLED display** instead.

| Function Code | Name | Workaround |
|---------------|------|-----------|
| 0x82 | Set work mode | Set via display |
| 0x83 | Set working current (mA) | Set via display |
| 0x85 | Set EN pin active level | Set via display |
| 0x86 | Set motor direction | Set via display |
| 0x90 | Set zero/home mode | Set via display → `0_Mode` menu |
| 0x91 | Set zero point (save home) | Set via display → `Set 0` option |
| 0x92 | Set zero speed | Set via display → `0_Speed` menu |
| 0x93 | Set zero direction | Set via display → `0_Dir` menu |
| 0x94 | Go home (return to zero) | Works ONLY after zero-mode configured via display |

> **Note:** Older firmware versions may accept some of these commands. Only V3.6.1 has been confirmed to block them.

---

## Command Encoding Details (Lessons Learned the Hard Way)

### 0x83 — Set Working Current
- Data: **1 byte** index where `current = index × 200 mA` (range 0x01–0x0F = 200–3000 mA)
- NOT a 2-byte big-endian milliamp value as one might assume

### 0x91 — Set Zero Point (NOT "go home")
- Saves current encoder position as the zero/home reference
- Data: `0x00`
- Only works if zero-mode is NOT set to Disable (mode must be DirMode=1 or NearMode=2)

### 0x94 — Go Home (NOT 0x91)
- Commands driver to return to saved zero point
- Data: `0x00`
- Requires: zero-mode ≠ Disable AND zero point previously saved (0x91 or display `Set 0`)

### 0xFF — Save Parameters
- Data byte **required:** `0xC8` = save, `0xCA` = clear
- Sending 0xFF with no data byte returns 0x00 (failure)
- Only saves the F6 startup velocity — configuration settings (current, direction, etc.) auto-save when accepted via display

---

## Homing System

The drivers have a built-in hall effect sensor on the motor shaft for homing.

### Configuration via display (V3.6.1 — UART config blocked):
1. Navigate to `0_Mode` → select `DirMode` (or `NearMode`)
2. Navigate to `0_Speed` → select `2` (medium speed)
3. Navigate to `0_Dir` → select `CW`
4. Navigate to `Set 0` → confirm to save current position as home

### Via UART (older firmware only):
```
0x90 [mode]   — 0=Disable, 1=DirMode, 2=NearMode
0x91 [0x00]   — Save current position as zero point
0x92 [speed]  — 0=fastest, 4=slowest
0x93 [dir]    — 0=CW, 1=CCW
0x94 [0x00]   — Execute go-home
```

### Software homing fallback (in firmware):
If the 0x94 HOMING bit never appears in status within `HOMING_BIT_WAIT_MS` (2000 ms), the firmware falls back to software homing: runs at `HOMING_SW_SPEED_DEG_S` (20 deg/s) until the encoder crosses 0°, detected by a wrap of `HOMING_SW_WRAP_THRESH` (30°) in one poll.

---

## Acceleration

The ACC parameter controls the deceleration ramp in CR_UART mode.

- **Menu item 18 on driver display** — labeled `ACC`
- Valid range observed: 286–1042 (driver units)
- Default from factory: `Disable` (no ramp — abrupt start/stop)
- UART command 0xA4 exists in the protocol but has NOT been tested in this project
- **To activate:** configure via display; once set it persists across power cycles

---

## Encoder Response Quirk (Pan / V3.6.1)

The pan driver (V3.6.1) returns **32 bytes** in response to a 0x3A status query instead of the expected 2 bytes. The `readResponse()` function reads only the expected number of bytes, leaving the remainder in the UART RX buffer. This can contaminate the next command's response.

**Known issue — not yet fixed.** A `uart.flush()` / drain loop after each transaction would resolve it.

---

## NVS (Non-Volatile Storage) Behaviour

Settings configured from the CLI and saved with the `save` command are stored in ESP32 NVS (namespace: `ptz_cfg`). **NVS survives firmware flashes.**

- After changing defaults in `config.h`, you must run `reset` then `save` from the CLI to overwrite the stored values
- `reset` reloads compile-time defaults into RAM
- `save` writes RAM state to NVS

---

## Setup Script (`scripts/mks_setup.py`)

A Python UART configuration tool for direct driver interaction without the ESP32.

- Connects directly to driver via FTDI USB-UART adapter (38400 baud)
- Menu options: ping, read status, set microsteps, set current, calibrate, go home, set zero point, apply all settings, save velocity
- Option 9 (Apply All Settings) — most config commands return MANUAL/NO RESPONSE on V3.6.1; only microsteps (0x84) actually applies
- `cmd_go_home` uses 0x94 (not 0x91)
- Save command uses `0xFF 0xC8` framing

---

## Recommended Display Configuration Checklist

For each driver (navigate via the physical OLED display):

- [ ] Work mode: `CR_UART`
- [ ] Current: set to motor's rated current (e.g. 800–1200 mA for typical NEMA17)
- [ ] Microsteps: `16` (also settable via UART 0x84)
- [ ] Direction: as needed for axis polarity
- [ ] ACC: enable and tune (menu item 18)
- [ ] `0_Mode`: `DirMode`
- [ ] `0_Speed`: `2`
- [ ] `0_Dir`: `CW`
- [ ] `Set 0`: jog to desired home position, then confirm
