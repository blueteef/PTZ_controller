"""
bridge.py — ESPBridge: serial connection to the ESP32 over USB.

Runs a background thread that:
  1. Maintains a persistent serial connection to /dev/ttyUSB0
  2. Drains a priority queue of outgoing commands
  3. Reconnects automatically on disconnect

Thread model
────────────
  - _writer_thread  : drains _cmd_queue, writes bytes to serial, reads/discards echo
  - _poller_thread  : polls 'get position' every TELEMETRY_INTERVAL_S, updates AppState

Public API (thread-safe, call from any thread or coroutine)
───────────────────────────────────────────────────────────
  send(cmd)           : fire-and-forget, enqueues at normal priority
  send_urgent(cmd)    : enqueues at high priority (estop, stop)
  query(cmd, timeout) : blocking — sends cmd, reads response, returns str
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

# Priority levels for the command queue (lower number = higher priority)
_PRIO_URGENT = 0   # estop, stop
_PRIO_NORMAL = 1   # velocity, home, settings
_PRIO_TELEMETRY = 2  # position queries

_QUEUE_MAXSIZE = 8   # drop oldest normal commands if queue fills


class ESPBridge:
    def __init__(self) -> None:
        self._port:  Optional[serial.Serial] = None
        self._lock   = threading.Lock()           # protects _port for query()
        self._cmd_q: queue.PriorityQueue = queue.PriorityQueue()
        self._stop_event = threading.Event()
        self._seq   = 0   # tie-breaker for equal-priority items

        self._writer_thread  = threading.Thread(target=self._writer_loop,  daemon=True, name="esp-writer")
        self._poller_thread  = threading.Thread(target=self._poller_loop,  daemon=True, name="esp-poller")

    # ------------------------------------------------------------------
    # Lifecycle
    # ------------------------------------------------------------------

    def start(self) -> None:
        self._stop_event.clear()
        self._writer_thread.start()
        self._poller_thread.start()
        log.info("ESPBridge started on %s @ %d baud", config.SERIAL_PORT, config.SERIAL_BAUD)

    def stop(self) -> None:
        self._stop_event.set()
        self._writer_thread.join(timeout=3)
        self._poller_thread.join(timeout=3)
        with self._lock:
            if self._port and self._port.is_open:
                self._port.close()
        log.info("ESPBridge stopped")

    # ------------------------------------------------------------------
    # Public send interface
    # ------------------------------------------------------------------

    def send(self, cmd: str) -> None:
        """Fire-and-forget at normal priority. Drops oldest if queue full."""
        self._enqueue(_PRIO_NORMAL, cmd)

    def send_urgent(self, cmd: str) -> None:
        """High-priority send (stop, estop). Never dropped."""
        self._enqueue(_PRIO_URGENT, cmd)

    def query(self, cmd: str, timeout: float = 0.5) -> str:
        """
        Blocking query: sends cmd, reads response lines until timeout.
        Returns concatenated response text (may include echo and prompt noise).
        Acquires _lock — do not call from the writer thread.
        """
        with self._lock:
            if not self._is_connected():
                return ""
            try:
                self._port.write(cmd.encode())
                self._port.flush()
                deadline = time.monotonic() + timeout
                buf = []
                while time.monotonic() < deadline:
                    line = self._port.readline().decode(errors="replace")
                    if line:
                        buf.append(line)
                return "".join(buf)
            except serial.SerialException as e:
                log.warning("query failed: %s", e)
                self._mark_disconnected()
                return ""

    # ------------------------------------------------------------------
    # Internal: queue management
    # ------------------------------------------------------------------

    def _enqueue(self, priority: int, cmd: str) -> None:
        self._seq += 1
        try:
            self._cmd_q.put_nowait((priority, self._seq, cmd))
        except queue.Full:
            # Drop the oldest normal-priority item and retry
            try:
                self._cmd_q.get_nowait()
            except queue.Empty:
                pass
            self._cmd_q.put_nowait((priority, self._seq, cmd))

    # ------------------------------------------------------------------
    # Internal: writer thread
    # ------------------------------------------------------------------

    def _writer_loop(self) -> None:
        while not self._stop_event.is_set():
            if not self._is_connected():
                self._connect()
                if not self._is_connected():
                    time.sleep(config.SERIAL_RECONNECT_S)
                    continue

            try:
                priority, _, cmd = self._cmd_q.get(timeout=0.05)
            except queue.Empty:
                continue

            with self._lock:
                if not self._is_connected():
                    continue
                try:
                    self._port.write(cmd.encode())
                    self._port.flush()
                    # Drain any echo or prompt bytes (vel sends nothing back now)
                    time.sleep(0.008)
                    if self._port.in_waiting:
                        self._port.read(self._port.in_waiting)
                except serial.SerialException as e:
                    log.warning("write failed: %s", e)
                    self._mark_disconnected()

    # ------------------------------------------------------------------
    # Internal: telemetry poller thread
    # ------------------------------------------------------------------

    def _poller_loop(self) -> None:
        """Polls 'get position' periodically and updates AppState."""
        while not self._stop_event.is_set():
            time.sleep(config.TELEMETRY_INTERVAL_S)
            if not self._is_connected():
                continue
            resp = self.query(protocol.cmd_get_position(), timeout=0.3)
            result = protocol.parse_position(resp)
            if result:
                state.set_gimbal_position(result[0], result[1])

    # ------------------------------------------------------------------
    # Internal: connection management
    # ------------------------------------------------------------------

    def _connect(self) -> None:
        try:
            port = serial.Serial(
                config.SERIAL_PORT,
                config.SERIAL_BAUD,
                timeout=config.SERIAL_TIMEOUT,
            )
            port.reset_input_buffer()
            port.reset_output_buffer()
            with self._lock:
                self._port = port
            state.serial_connected = True
            log.info("Connected to ESP32 on %s", config.SERIAL_PORT)
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
