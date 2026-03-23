/**
 * controls.js — Virtual joystick + keyboard → velocity commands.
 *
 * The joystick is a canvas element.  Drag or touch to move the handle;
 * release to return to center and send stop.
 *
 * Keyboard: WASD or arrow keys while the page has focus.
 * Space = stop.
 */

import { send } from "./app.js";

const VEL_SEND_HZ = 20;            // how often to send velocity while moving
let _velInterval  = null;
let _panTarget  = 0;
let _tiltTarget = 0;
let _joystickActive = false;

// ---------------------------------------------------------------------------
// Velocity sender (called on interval while stick is deflected)
// ---------------------------------------------------------------------------

function startSending() {
  if (_velInterval) return;
  _velInterval = setInterval(() => {
    const maxV = window._ptz_max_vel ?? 180;
    const pan  = _panTarget  * maxV;
    const tilt = _tiltTarget * maxV;
    if (Math.abs(pan) < 0.5 && Math.abs(tilt) < 0.5) {
      stopSending();
      return;
    }
    send({ type: "velocity", pan, tilt });
  }, 1000 / VEL_SEND_HZ);
}

export function stopSending() {
  if (_velInterval) {
    clearInterval(_velInterval);
    _velInterval = null;
  }
  send({ type: "stop", axis: "all" });
  _panTarget = 0;
  _tiltTarget = 0;
}

// ---------------------------------------------------------------------------
// Joystick canvas
// ---------------------------------------------------------------------------

export function initJoystick(canvas) {
  const ctx   = canvas.getContext("2d");
  const CX    = canvas.width  / 2;
  const CY    = canvas.height / 2;
  const R_BASE = CX - 10;   // outer ring radius
  const R_KNOB = 18;

  let knobX = CX, knobY = CY;
  let dragging = false;

  function draw() {
    ctx.clearRect(0, 0, canvas.width, canvas.height);

    // Outer ring
    ctx.beginPath();
    ctx.arc(CX, CY, R_BASE, 0, Math.PI * 2);
    ctx.strokeStyle = "#444";
    ctx.lineWidth = 1.5;
    ctx.stroke();

    // Cross-hair guide lines
    ctx.strokeStyle = "#333";
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(CX, CY - R_BASE); ctx.lineTo(CX, CY + R_BASE);
    ctx.moveTo(CX - R_BASE, CY); ctx.lineTo(CX + R_BASE, CY);
    ctx.stroke();

    // Handle
    ctx.beginPath();
    ctx.arc(knobX, knobY, R_KNOB, 0, Math.PI * 2);
    ctx.fillStyle = dragging ? "#29b6f6" : "#555";
    ctx.fill();
  }

  function pointerMove(px, py) {
    const rect = canvas.getBoundingClientRect();
    const dx = (px - rect.left)  - CX;
    const dy = (py - rect.top)   - CY;
    const dist = Math.sqrt(dx * dx + dy * dy);
    const clamp = Math.min(dist, R_BASE - R_KNOB);
    const angle = Math.atan2(dy, dx);
    knobX = CX + clamp * Math.cos(angle);
    knobY = CY + clamp * Math.sin(angle);

    // Normalise to [-1, 1]
    _panTarget  =  (knobX - CX) / (R_BASE - R_KNOB);
    _tiltTarget = -((knobY - CY) / (R_BASE - R_KNOB));  // invert Y

    draw();
    startSending();
  }

  function pointerUp() {
    dragging = false;
    knobX = CX; knobY = CY;
    draw();
    stopSending();
  }

  canvas.addEventListener("pointerdown", e => {
    dragging = true;
    canvas.setPointerCapture(e.pointerId);
    pointerMove(e.clientX, e.clientY);
  });
  canvas.addEventListener("pointermove", e => {
    if (dragging) pointerMove(e.clientX, e.clientY);
  });
  canvas.addEventListener("pointerup",   pointerUp);
  canvas.addEventListener("pointercancel", pointerUp);

  draw();
}

// ---------------------------------------------------------------------------
// Keyboard
// ---------------------------------------------------------------------------

const KEY_PAN  = { ArrowLeft: -1, ArrowRight: 1, KeyA: -1, KeyD: 1 };
const KEY_TILT = { ArrowUp: 1,   ArrowDown: -1, KeyW:  1, KeyS: -1 };
const _keys = new Set();

document.addEventListener("keydown", e => {
  if (e.target.tagName === "INPUT" || e.target.tagName === "SELECT") return;
  if (e.code === "Space") { e.preventDefault(); stopSending(); return; }
  if (KEY_PAN[e.code] !== undefined || KEY_TILT[e.code] !== undefined) {
    e.preventDefault();
    _keys.add(e.code);
    _panTarget  = 0; _tiltTarget = 0;
    for (const k of _keys) {
      _panTarget  += KEY_PAN[k]  ?? 0;
      _tiltTarget += KEY_TILT[k] ?? 0;
    }
    // Clamp
    _panTarget  = Math.max(-1, Math.min(1, _panTarget));
    _tiltTarget = Math.max(-1, Math.min(1, _tiltTarget));
    startSending();
  }
});

document.addEventListener("keyup", e => {
  _keys.delete(e.code);
  if (_keys.size === 0) stopSending();
});
