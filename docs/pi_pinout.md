# Raspberry Pi 4 — GPIO Pin Assignments

## Visual Pin Map

Board oriented with USB ports down, GPIO header at top-left.
Pin 1 is the corner pin nearest the SD card slot (top-left of header).

```
                    ┌─ SD card side
                    │
        ┌───────────┴───────────┐
        │  ● ● ← pin 1 is here  │  <- top of header
        │                       │
        │   [GPIO HEADER]       │
        │                       │
        │         Pi 4          │
        │                       │
        │  [USB 2.0] [USB 3.0]  │
        │  [USB 2.0] [USB 3.0]  │
        └───────────────────────┘

Pin 1 = top-left (odd pins on left, even on right going down)
```

```
LEFT RAIL (odd)                        RIGHT RAIL (even)
─────────────────────────────────────────────────────────
  3.3V        ○ 1 │ 2 ○  5V
  GPIO2  SDA  ○ 3 │ 4 ○  5V
  GPIO3  SCL  ○ 5 │ 6 ○  GND
  GPIO4       ○ 7 │ 8 ○  GPIO14  UART TX
  GND         ○ 9 │10 ○  GPIO15  UART RX
  GPIO17 ■ 11 │12 ■  GPIO18             ← RELAY(11)  FAN(12)
  GPIO27      ○13 │14 ○  GND
  GPIO22      ○15 │16 ○  GPIO23
  3.3V        ○17 │18 ○  GPIO24
  GPIO10 ■ 19 │20 ○  GND               ← CAN MOSI(19)
  GPIO9  ■ 21 │22 ■  GPIO25            ← CAN MISO(21)  CAN INT(22)
  GPIO11 ■ 23 │24 ■  GPIO8             ← CAN CLK(23)   CAN CS(24)
  GND         ○25 │26 ○  GPIO7
  GPIO0  ID   ○27 │28 ○  GPIO1  ID
  GPIO5       ○29 │30 ○  GND
  GPIO6       ○31 │32 ○  GPIO12
  GPIO13      ○33 │34 ○  GND
  GPIO19      ○35 │36 ○  GPIO16
  GPIO26      ○37 │38 ○  GPIO20
  GND         ○39 │40 ○  GPIO21

  ○ = free / power / GND
  ■ = in use
```

## Assigned Pins

| Physical Pin | GPIO   | Function                        | Notes                        |
|-------------|--------|---------------------------------|------------------------------|
| 1           | —      | 3.3V power                      |                              |
| 2 / 4       | —      | 5V power                        | UPS board VCC here           |
| 3           | GPIO2  | I2C SDA                         | Tilt IMU (MPU-6050) + UPS    |
| 5           | GPIO3  | I2C SCL                         | Tilt IMU (MPU-6050) + UPS    |
| 11          | GPIO17 | Main power relay                | High-active, gpio17ctl.sh    |
| 12          | GPIO18 | Fan MOSFET                      | High-active, fan_control.py  |
| 19          | GPIO10 | SPI0 MOSI → CAN MOSI (SI)       | MCP2515                      |
| 21          | GPIO9  | SPI0 MISO → CAN MISO (SO)       | MCP2515                      |
| 22          | GPIO25 | CAN INT                         | MCP2515 interrupt            |
| 23          | GPIO11 | SPI0 CLK → CAN SCK              | MCP2515                      |
| 24          | GPIO8  | SPI0 CE0 → CAN CS               | MCP2515                      |
| 6 / 9 / 14 / 20 / 25 / 30 / 34 / 39 | — | GND | Use any for ground returns   |

## CSI Camera

IMX462 (Innomaker CAM-MIPI462RAW) connects to the CSI ribbon cable connector — not GPIO pins.
`dtoverlay=imx290,clock-frequency=74250000` in `/boot/firmware/config.txt`.

## MCP2515 CAN Transceiver Wiring Summary

| Module Pin | Pi Pin | GPIO    |
|------------|--------|---------|
| VCC        | 1      | 3.3V    |
| GND        | 6      | GND     |
| SCK        | 23     | GPIO11  |
| MOSI (SI)  | 19     | GPIO10  |
| MISO (SO)  | 21     | GPIO9   |
| CS         | 24     | GPIO8   |
| INT        | 22     | GPIO25  |

Oscillator: `oscillator=8000000` (8 MHz crystal) or `16000000` — check crystal markings on module.

## UPS (MakerFocus UPSPack V3P)

| Module Pin | Pi Pin | GPIO   |
|------------|--------|--------|
| 5V / VCC   | 2 or 4 | 5V     |
| GND        | 6      | GND    |
| SDA        | 3      | GPIO2  |
| SCL        | 5      | GPIO3  |

I2C address: TBD — run `sudo i2cdetect -y 1` to confirm (expected 0x17 or 0x18).

## Free GPIOs (available for future use)

GPIO 4, 5, 6, 7, 12, 13, 14, 15, 16, 19, 20, 21, 22, 23, 24, 26, 27

GPIO 14/15 are UART TX/RX — available if serial bridge to ESP32 is not in use.
