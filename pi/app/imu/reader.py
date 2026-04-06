"""
imu/reader.py — Direct Pi-side MPU-6050 reader with complementary filter.

The Pi is mounted on the end effector and moves with the camera in every
axis, so its I2C bus is the natural place to read the IMU — no slip ring
wires needed for IMU signals.

Runs a complementary filter at IMU_RATE_HZ in a daemon thread and writes
corrected roll/pitch into state.sensor_imu.

Hardware:
    MPU-6050 SDA → Pi GPIO 2  (i2c-1 SDA)
    MPU-6050 SCL → Pi GPIO 3  (i2c-1 SCL)
    MPU-6050 VCC → 3.3 V
    MPU-6050 GND → GND
    MPU-6050 AD0 → GND  (I2C address 0x68)

Prerequisites:
    sudo raspi-config → Interface Options → I2C → Enable
    pip install smbus2
"""

import logging
import math
import threading
import time
from typing import Optional

from app import config
from app.state import state

log = logging.getLogger(__name__)

try:
    import smbus2 as smbus
    _SMBUS_OK = True
except ImportError:
    _SMBUS_OK = False

_MPU_ADDR      = 0x68
_REG_PWR_MGMT  = 0x6B   # write 0x00 to wake
_REG_GYRO_CFG  = 0x1B   # FS_SEL=0  → ±250 °/s, 131 LSB/°/s
_REG_ACCEL_CFG = 0x1C   # AFS_SEL=0 → ±2 g,     16384 LSB/g
_REG_DATA      = 0x3B   # 14 bytes: ax,ay,az,temp,gx,gy,gz (H then L)


def _s16(high: int, low: int) -> int:
    val = (high << 8) | low
    return val - 65536 if val >= 32768 else val


class IMUReader:
    def __init__(self) -> None:
        self._bus: Optional[object] = None
        self._stop   = threading.Event()
        self._thread = threading.Thread(target=self._loop, daemon=True, name="imu-reader")
        self._roll   = 0.0
        self._pitch  = 0.0
        self._last_t: Optional[float] = None

    # ------------------------------------------------------------------
    # Lifecycle
    # ------------------------------------------------------------------

    def start(self) -> None:
        if not _SMBUS_OK:
            log.warning("smbus2 not installed — IMU disabled. Run: pip install smbus2")
            return
        try:
            self._bus = smbus.SMBus(config.IMU_I2C_BUS)
            self._init_mpu()
            self._thread.start()
            log.info("IMUReader started on i2c-%d  MPU-6050 @ 0x%02X  %d Hz",
                     config.IMU_I2C_BUS, _MPU_ADDR, config.IMU_RATE_HZ)
        except Exception as e:
            log.error("IMUReader failed to start: %s — check wiring and i2cdetect -y %d",
                      e, config.IMU_I2C_BUS)
            self._bus = None

    def stop(self) -> None:
        self._stop.set()
        self._thread.join(timeout=2)
        if self._bus:
            try:
                self._bus.close()
            except Exception:
                pass

    # ------------------------------------------------------------------
    # Internal
    # ------------------------------------------------------------------

    def _init_mpu(self) -> None:
        b = self._bus
        b.write_byte_data(_MPU_ADDR, _REG_PWR_MGMT,  0x00)   # wake up
        time.sleep(0.05)
        b.write_byte_data(_MPU_ADDR, _REG_GYRO_CFG,  0x00)   # ±250 °/s
        b.write_byte_data(_MPU_ADDR, _REG_ACCEL_CFG, 0x00)   # ±2 g

    def _loop(self) -> None:
        interval = 1.0 / config.IMU_RATE_HZ
        while not self._stop.is_set():
            t0 = time.monotonic()
            try:
                self._read()
            except Exception as e:
                log.debug("IMU read error: %s", e)
                state.sensor_imu = {"ok": False, "roll": 0.0, "pitch": 0.0}
                time.sleep(0.5)   # back off on repeated errors
                continue
            elapsed = time.monotonic() - t0
            time.sleep(max(0.0, interval - elapsed))

    def _read(self) -> None:
        data = self._bus.read_i2c_block_data(_MPU_ADDR, _REG_DATA, 14)

        ax = _s16(data[0],  data[1])
        ay = _s16(data[2],  data[3])
        az = _s16(data[4],  data[5])
        gx = _s16(data[8],  data[9])
        gy = _s16(data[10], data[11])

        axg    = ax / 16384.0
        ayg    = ay / 16384.0
        azg    = az / 16384.0
        gx_dps = gx / 131.0
        gy_dps = gy / 131.0

        accel_roll  = math.degrees(math.atan2(ayg, azg))
        accel_pitch = math.degrees(math.atan2(-axg, math.sqrt(ayg * ayg + azg * azg)))

        now = time.monotonic()
        if self._last_t is None or (now - self._last_t) > 0.5:
            # First call or stale reading — seed from accelerometer
            self._roll  = accel_roll
            self._pitch = accel_pitch
        else:
            dt    = now - self._last_t
            alpha = config.IMU_COMP_ALPHA
            self._roll  = alpha * (self._roll  + gx_dps * dt) + (1.0 - alpha) * accel_roll
            self._pitch = alpha * (self._pitch + gy_dps * dt) + (1.0 - alpha) * accel_pitch
        self._last_t = now

        # Apply Pi-side orientation corrections (same as before, just moved here)
        roll  = self._roll  * config.IMU_ROLL_SIGN
        pitch = self._pitch * config.IMU_PITCH_SIGN
        if config.IMU_SWAP_ROLL_PITCH:
            roll, pitch = pitch, roll
        roll  += config.IMU_ROLL_OFFSET_DEG
        pitch += config.IMU_PITCH_OFFSET_DEG

        state.sensor_imu = {
            "ok":    True,
            "roll":  round(roll,  2),
            "pitch": round(pitch, 2),
        }


imu_reader = IMUReader()
