#!/usr/bin/env python3
"""
sensor_monitor.py — Live sensor telemetry dump to terminal.

Connects to the PTZ WebSocket and pretty-prints all sensor data.
Run on the Pi:
    python3 scripts/sensor_monitor.py

Ctrl+C to exit.
"""

import asyncio
import json
import sys

try:
    import websockets
except ImportError:
    print("Install websockets: pip install websockets")
    sys.exit(1)

WS_URL = "ws://localhost:8000/ws/control"


def fmt(val, unit="", decimals=1):
    if val is None:
        return "—"
    return f"{val:.{decimals}f}{unit}"


async def monitor():
    print(f"Connecting to {WS_URL} ...")
    async with websockets.connect(WS_URL) as ws:
        print("Connected. Ctrl+C to exit.\n")
        while True:
            raw = await ws.recv()
            msg = json.loads(raw)
            if msg.get("type") != "telemetry":
                continue

            pan   = fmt(msg.get("pan"),  "°")
            tilt  = fmt(msg.get("tilt"), "°")
            can   = "CAN ✓" if msg.get("serial_ok") else "CAN ✗"

            imu  = msg.get("imu")  or {}
            mag  = msg.get("mag")  or {}
            env  = msg.get("env")  or {}
            pwr  = msg.get("power") or {}
            gps  = msg.get("gps")  or {}
            ups  = msg.get("ups")  or {}

            lines = [
                f"  [{can}]  pan={pan}  tilt={tilt}",
                f"  IMU   roll={fmt(imu.get('roll'),'°')}  pitch={fmt(imu.get('pitch'),'°')}  yaw={fmt(imu.get('yaw'),'°')}",
                f"  MAG   hdg={fmt(mag.get('hdg'),'°')}  ok={mag.get('ok',False)}",
                f"  ENV   temp={fmt(env.get('temp'),'°C')}  press={fmt(env.get('press'),'hPa',0)}",
                f"  PWR   vin={fmt(pwr.get('vin'),'V',2)}  curr={fmt(pwr.get('curr'),'mA',0)}  pwr={fmt(pwr.get('pwr'),'mW',0)}",
                f"  GPS   fix={gps.get('fix',False)}  sats={gps.get('sats','—')}  lat={fmt(gps.get('lat'),'°',5)}  lon={fmt(gps.get('lon'),'°',5)}",
                f"  UPS   {fmt(ups.get('pct'),'%',0)}  vin_ok={ups.get('vin_ok','—')}",
            ]

            # Clear previous block and reprint
            print("\033[2J\033[H", end="")   # clear screen
            print("\n".join(lines))


if __name__ == "__main__":
    try:
        asyncio.run(monitor())
    except KeyboardInterrupt:
        print("\nExiting.")
