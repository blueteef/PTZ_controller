"""
bridge.py — ESPBridge: serial connection to the ESP32 over GPIO UART.

All serial I/O runs in a single writer thread — no lock contention between
a command writer and a position poller.  Previously, two threads competed for
_lock: the writer for every command and the poller holding it for up to 300 ms
per query.  That caused urgent stop commands to wait behind the poller.

Thread model
────────────
  _writer_thread : drains _cmd_q, writes commands, polls position when due.
                   Only thread that touches _port — no mutex needed for I/O.

Public API (thread-safe, call from any thread or coroutine)
───────────────────────────────────────────────────────────
  send(cmd)           : fire-and-forget, normal priority
  send_urgent(cmd)    : high priority (estop, stop)
  start() / stop()    : lifecycle
"""

from __future__ import annotations

import logging
import queue
import threading
import time
from typing import Optional

import serial

from app import config
from app.state import state
from app.serial_bridge import protocol

log = logging.getLogger(__name__)

# Priority levels (lower = higher priority)
_PRIO_URGENT   = 0   # estop, stop
_PRIO_NORMAL   = 1   # velocity, settings
_QUEUE_MAXSIZE = 16


class ESPBridge:
    def __init__(self) -> None:
        self._port: Optional[serial.Serial] = None
        self._cmd_q: queue.PriorityQueue = queue.PriorityQueue(maxsize=_QUEUE_MAXSIZE)
        self._stop_event = threading.Event()
        self._seq = 0

        self._writer_thread = threading.Thread(
            target=self._writer_loop, daemon=True, name="esp-writer"
        )

    # ------------------------------------------------------------------
    # Lifecycle
    # ------------------------------------------------------------------

    def start(self) -> None:
        self._stop_event.clear()
        self._writer_thread.start()
        log.info("ESPBridge started on %s @ %d baud", config.SERIAL_PORT, config.SERIAL_BAUD)

    def stop(self) -> None:
        self._stop_event.set()
        self._writer_thread.join(timeout=3)
        if self._port and self._port.is_open:
            self._port.close()
        log.info("ESPBridge stopped")

    # ------------------------------------------------------------------
    # Public send interface
    # ------------------------------------------------------------------

    def send(self, cmd: str) -> None:
        """Fire-and-forget at normal priority."""
        self._enqueue(_PRIO_NORMAL, cmd)

    def send_urgent(self, cmd: str) -> None:
        """High-priority send (stop, estop). Jumps ahead of queued vel commands."""
        self._enqueue(_PRIO_URGENT, cmd)

    # ------------------------------------------------------------------
    # Internal: queue management
    # ------------------------------------------------------------------

    def _enqueue(self, priority: int, cmd: str) -> None:
        self._seq += 1
        item = (priority, self._seq, cmd)
        try:
            self._cmd_q.put_nowait(item)
        except queue.Full:
            # Drop the oldest normal-priority item to make room.
            # Collect all items, remove the first normal one, re-insert.
            items = []
            while True:
                try:
                    items.append(self._cmd_q.get_nowait())
                except queue.Empty:
                    break
            # Remove last (oldest seq) normal-priority item if present
            dropped = False
            for i in range(len(items) - 1, -1, -1):
                if items[i][0] >= _PRIO_NORMAL:
                    items.pop(i)
                    dropped = True
                    break
            if not dropped and items:
                items.pop()  # fallback: drop oldest
            items.append(item)
            items.sort()
            for it in items:
                try:
                    self._cmd_q.put_nowait(it)
                except queue.Full:
                    break

    # ------------------------------------------------------------------
    # Internal: writer thread  (only thread that touches _port)
    # ------------------------------------------------------------------

    def _writer_loop(self) -> None:
        last_poll = 0.0

        while not self._stop_event.is_set():
            if not self._is_connected():
                self._connect()
                if not self._is_connected():
                    time.sleep(config.SERIAL_RECONNECT_S)
                    continue

            # Position poll — runs inline here so it never blocks command delivery.
            now = time.monotonic()
            if now - last_poll >= config.TELEMETRY_INTERVAL_S:
                last_poll = now
                self._do_poll()

            # Time until next poll (caps the queue wait so poll stays on schedule)
            wait = max(0.002, config.TELEMETRY_INTERVAL_S - (time.monotonic() - last_poll))

            try:
                _, _, cmd = self._cmd_q.get(timeout=wait)
            except queue.Empty:
                continue

            try:
                self._port.write(cmd.encode())
                self._port.flush()
                # Brief pause then drain any incidental output (prompts, echoes)
                time.sleep(0.005)
                if self._port.in_waiting:
                    self._port.read(self._port.in_waiting)
            except serial.SerialException as e:
                log.warning("write failed: %s", e)
                self._mark_disconnected()

    # ------------------------------------------------------------------
    # Internal: position poll (called from writer thread only)
    # ------------------------------------------------------------------

    def _do_poll(self) -> None:
        if not self._is_connected():
            return
        try:
            self._port.write(protocol.cmd_get_position().encode())
            self._port.flush()
            # Read up to 2 response lines; stop as soon as readline times out.
            buf = []
            for _ in range(2):
                line = self._port.readline().decode(errors="replace")
                if not line:
                    break
                buf.append(line)
            result = protocol.parse_position("".join(buf))
            if result:
                state.set_gimbal_position(result[0], result[1])
        except serial.SerialException as e:
            log.warning("poll failed: %s", e)
            self._mark_disconnected()

    # ------------------------------------------------------------------
    # Internal: connection management
    # ------------------------------------------------------------------

    def _push_settings(self) -> None:
        """Send all Pi-owned motion settings to the ESP32 after connect."""
        cmds = [
            protocol.cmd_set_speed(config.MAX_SPEED_DEG_S),
            protocol.cmd_set_accel(config.ACCEL_DEG_S2),
            protocol.cmd_set_fine(config.FINE_SPEED_SCALE),
            protocol.cmd_set_invert("pan",  config.PAN_INVERT),
            protocol.cmd_set_invert("tilt", config.TILT_INVERT),
            protocol.cmd_set_limits("pan",  config.PAN_SOFT_LIMIT_MIN,  config.PAN_SOFT_LIMIT_MAX),
            protocol.cmd_set_limits("tilt", config.TILT_SOFT_LIMIT_MIN, config.TILT_SOFT_LIMIT_MAX),
            protocol.cmd_set_limits_enabled(config.SOFT_LIMITS_ENABLED),
            protocol.cmd_set_hold(config.STEPPER_HOLD_MS),
        ]
        for cmd in cmds:
            try:
                self._port.write(cmd.encode())
                self._port.flush()
                time.sleep(0.015)  # give ESP32 time to process each command
                if self._port.in_waiting:
                    self._port.read(self._port.in_waiting)
            except serial.SerialException as e:
                log.warning("push_settings failed: %s", e)
                return
        log.info("Motion settings pushed to ESP32")

    def _connect(self) -> None:
        try:
            port = serial.Serial(
                config.SERIAL_PORT,
                config.SERIAL_BAUD,
                timeout=config.SERIAL_TIMEOUT,
            )
            port.reset_input_buffer()
            port.reset_output_buffer()
            self._port = port
            state.serial_connected = True
            log.info("Connected to ESP32 on %s", config.SERIAL_PORT)
            self._push_settings()
        except serial.SerialException as e:
            log.debug("Connect failed: %s", e)
            state.serial_connected = False

    def _is_connected(self) -> bool:
        return self._port is not None and self._port.is_open

    def _mark_disconnected(self) -> None:
        if self._port:
            try:
                self._port.close()
            except Exception:
                pass
        self._port = None
        state.serial_connected = False
        log.warning("ESP32 serial disconnected — will retry")


# Module-level singleton.
bridge = ESPBridge()
