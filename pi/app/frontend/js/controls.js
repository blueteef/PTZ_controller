/**
 * controls.js — Virtual joystick + keyboard → velocity commands.
 *
 * Key fixes over v1:
 *  - keyup on window (not document) — survives focus changes
 *  - window blur clears all keys and stops motion
 *  - joystick pointerup on window — survives drag outside canvas
 *  - send rate raised to 25Hz, but ONLY while actively deflected
 *  - stopSending exported so buttons can kill the interval too
 */

import { send } from "./app.js";

const VEL_SEND_HZ = 25;
let _velInterval  = null;
let _panTarget    = 0;
let _tiltTarget   = 0;

// ---------------------------------------------------------------------------
// Velocity sender
// ---------------------------------------------------------------------------

function startSending() {
  if (_velInterval) return;
  // Send one immediately so there's no 40ms delay on first input
  _sendVelocity();
  _velInterval = setInterval(_sendVelocity, 1000 / VEL_SEND_HZ);
}

function _curve(v) {
  // Squared response: preserves sign, gives precision at low deflection
  // and full speed at max. Feels much smoother than linear.
  return Math.sign(v) * v * v;
}

function _sendVelocity() {
  const maxV = window._ptz_max_vel ?? 45;
  const pan  = _curve(_panTarget)  * maxV;
  const tilt = _curve(_tiltTarget) * maxV;
  if (Math.abs(pan) < 0.5 && Math.abs(tilt) < 0.5) {
    stopSending();
    return;
  }
  send({ type: "velocity", pan, tilt });
}

export function stopSending() {
  if (_velInterval) {
    clearInterval(_velInterval);
    _velInterval = null;
  }
  _panTarget  = 0;
  _tiltTarget = 0;
  send({ type: "stop", axis: "all" });
}

// ---------------------------------------------------------------------------
// Virtual joystick
// ---------------------------------------------------------------------------

export function initJoystick(canvas) {
  const ctx    = canvas.getContext("2d");
  const CX     = canvas.width  / 2;
  const CY     = canvas.height / 2;
  const R_BASE = CX - 10;
  const R_KNOB = 18;

  let knobX    = CX, knobY = CY;
  let dragging = false;
  let capturedId = null;

  function draw() {
    ctx.clearRect(0, 0, canvas.width, canvas.height);

    // Outer ring
    ctx.beginPath();
    ctx.arc(CX, CY, R_BASE, 0, Math.PI * 2);
    ctx.strokeStyle = "#444";
    ctx.lineWidth = 1.5;
    ctx.stroke();

    // Guide lines
    ctx.strokeStyle = "#333";
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(CX, CY - R_BASE); ctx.lineTo(CX, CY + R_BASE);
    ctx.moveTo(CX - R_BASE, CY); ctx.lineTo(CX + R_BASE, CY);
    ctx.stroke();

    // Knob
    ctx.beginPath();
    ctx.arc(knobX, knobY, R_KNOB, 0, Math.PI * 2);
    ctx.fillStyle = dragging ? "#29b6f6" : "#555";
    ctx.fill();
  }

  function updateKnob(px, py) {
    const rect  = canvas.getBoundingClientRect();
    const dx    = (px - rect.left)  - CX;
    const dy    = (py - rect.top)   - CY;
    const dist  = Math.sqrt(dx * dx + dy * dy);
    const clamp = Math.min(dist, R_BASE - R_KNOB);
    const angle = Math.atan2(dy, dx);
    knobX = CX + clamp * Math.cos(angle);
    knobY = CY + clamp * Math.sin(angle);

    _panTarget  =  (knobX - CX) / (R_BASE - R_KNOB);
    _tiltTarget = -((knobY - CY) / (R_BASE - R_KNOB));

    draw();
    startSending();
  }

  function releaseKnob() {
    if (!dragging) return;
    dragging   = false;
    capturedId = null;
    knobX = CX; knobY = CY;
    draw();
    stopSending();
  }

  canvas.addEventListener("pointerdown", e => {
    dragging   = true;
    capturedId = e.pointerId;
    canvas.setPointerCapture(e.pointerId);
    updateKnob(e.clientX, e.clientY);
  });

  canvas.addEventListener("pointermove", e => {
    if (dragging && e.pointerId === capturedId)
      updateKnob(e.clientX, e.clientY);
  });

  // Listen on window so release outside canvas is always caught
  window.addEventListener("pointerup",     e => { if (e.pointerId === capturedId) releaseKnob(); });
  window.addEventListener("pointercancel", e => { if (e.pointerId === capturedId) releaseKnob(); });

  draw();
}

// ---------------------------------------------------------------------------
// Keyboard
// ---------------------------------------------------------------------------

const KEY_PAN  = { ArrowLeft: -1, ArrowRight: 1, KeyA: -1, KeyD: 1 };
const KEY_TILT = { ArrowUp: 1,   ArrowDown: -1,  KeyW:  1, KeyS: -1 };
const _keys    = new Set();

function recalcTargets() {
  _panTarget  = 0;
  _tiltTarget = 0;
  for (const k of _keys) {
    _panTarget  += KEY_PAN[k]  ?? 0;
    _tiltTarget += KEY_TILT[k] ?? 0;
  }
  _panTarget  = Math.max(-1, Math.min(1, _panTarget));
  _tiltTarget = Math.max(-1, Math.min(1, _tiltTarget));
}

window.addEventListener("keydown", e => {
  // Only block WASD when user is actively typing in a text field
  if (e.target.tagName === "INPUT" && e.target.type !== "checkbox" && e.target.type !== "range") return;
  if (e.repeat) return;   // ignore key-repeat events — we handle continuous send ourselves

  if (e.code === "Space") { e.preventDefault(); stopSending(); return; }

  if (KEY_PAN[e.code] !== undefined || KEY_TILT[e.code] !== undefined) {
    e.preventDefault();
    _keys.add(e.code);
    recalcTargets();
    startSending();
  }
});

window.addEventListener("keyup", e => {
  if (!_keys.has(e.code)) return;
  _keys.delete(e.code);
  if (_keys.size === 0) {
    stopSending();
  } else {
    recalcTargets();
  }
});

// Stop everything if the page loses focus (alt-tab, click away, etc.)
window.addEventListener("blur", () => {
  _keys.clear();
  stopSending();
});
