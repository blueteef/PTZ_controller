/**
 * detection_overlay.js — Canvas overlay: detection boxes + HUD instruments.
 *
 * HUD (on by default, hides during detection mode):
 *   Compass tape    — bottom edge, scrolling azimuth marks
 *   Horizon         — center, tilts with roll, shifts with pitch
 *   Gimbal readout  — top-left, pan/tilt angles
 *   Info strip      — above compass tape, power / GPS / env
 *
 * All IMU/compass values are EMA-smoothed before drawing.
 */

import { send } from "./app.js";

let _canvas = null;
let _ctx    = null;
let _img    = null;
let _detections = [];

let _hudEnabled          = true;    // on at boot
let _hudLockRoll         = true;    // locked until roll is calibrated
let _detectionModeActive = false;

// Latest sensor inputs (set by app.js)
let _imuData = null;
let _magData = null;
let _telData = {};   // { pan, tilt, power, env, gps }

// EMA-smoothed display values
let _sRoll  = null;
let _sPitch = null;
let _sHdg   = null;

const ROLL_ALPHA  = 0.6;
const PITCH_ALPHA = 0.6;
const HDG_ALPHA   = 0.4;

const COLOURS = { face: "#4caf50", default: "#ff9800" };
const TAPE_H  = 48;   // compass tape height (px at native res)

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

export function initOverlay(imgEl, canvasEl) {
  _img    = imgEl;
  _canvas = canvasEl;
  _ctx    = canvasEl.getContext("2d");
  imgEl.addEventListener("load", _syncSize);
  new ResizeObserver(_syncSize).observe(imgEl);
  canvasEl.addEventListener("click", _onCanvasClick);
}

export function updateDetections(objects) {
  _detections = objects;
  _canvas.className = objects.length > 0 ? "interactive" : "";
  _redraw();
}

export function setHUDEnabled(enabled) {
  _hudEnabled = enabled;
  _redraw();
}

export function setHUDLockRoll(locked) {
  _hudLockRoll = locked;
  _redraw();
}

export function setDetectionMode(mode) {
  _detectionModeActive = mode !== "none";
  _redraw();
}

export function updateHUDData(imu, mag) {
  // Apply EMA smoothing
  if (imu) {
    _sRoll  = _emaVal(_sRoll,  imu.roll  ?? 0, ROLL_ALPHA);
    _sPitch = _emaVal(_sPitch, imu.pitch ?? 0, PITCH_ALPHA);
    _imuData = imu;
  }
  if (mag) {
    _sHdg = _emaAngle(_sHdg, mag.hdg ?? 0, HDG_ALPHA);
    _magData = mag;
  }
  if (_hudEnabled && !_detectionModeActive) _redraw();
}

export function updateTelemetryHUD(pan, tilt, power, env, gps) {
  _telData = { pan, tilt, power, env, gps };
  if (_hudEnabled && !_detectionModeActive) _redraw();
}

// ─────────────────────────────────────────────────────────────────────────────
// EMA helpers
// ─────────────────────────────────────────────────────────────────────────────

function _emaVal(prev, next, alpha) {
  return prev === null ? next : prev + alpha * (next - prev);
}

function _emaAngle(prev, next, alpha) {
  if (prev === null) return next;
  const diff = ((next - prev + 540) % 360) - 180;
  // Large sudden jumps are motor EMI spikes, not real heading changes.
  // Reduce alpha to 3% for jumps >25° so spikes barely move the display.
  const a = Math.abs(diff) > 25 ? alpha * 0.03 : alpha;
  return (prev + a * diff + 360) % 360;
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal
// ─────────────────────────────────────────────────────────────────────────────

function _syncSize() {
  _canvas.width  = _img.naturalWidth  || _img.offsetWidth;
  _canvas.height = _img.naturalHeight || _img.offsetHeight;
  _canvas.style.width  = _img.offsetWidth  + "px";
  _canvas.style.height = _img.offsetHeight + "px";
  _canvas.style.left   = _img.offsetLeft + "px";
  _canvas.style.top    = _img.offsetTop  + "px";
  _redraw();
}

function _redraw() {
  if (!_ctx) return;
  const w = _canvas.width;
  const h = _canvas.height;
  _ctx.clearRect(0, 0, w, h);

  if (_hudEnabled && !_detectionModeActive) {
    _drawHUD(w, h);
  }

  const scaleX = w / (window._ptz_cam_w ?? 1280);
  const scaleY = h / (window._ptz_cam_h ?? 720);
  for (const d of _detections) {
    const colour = COLOURS[d.label] ?? COLOURS.default;
    const x = d.x * scaleX, y = d.y * scaleY;
    const bw = d.w * scaleX, bh = d.h * scaleY;
    _ctx.strokeStyle = colour;
    _ctx.lineWidth   = 2;
    _ctx.strokeRect(x, y, bw, bh);
    const label = d.name ? `${d.name} (${(d.confidence * 100).toFixed(0)}%)`
                          : `${d.label} ${(d.confidence * 100).toFixed(0)}%`;
    _ctx.font      = "bold 12px system-ui";
    _ctx.fillStyle = colour;
    _ctx.fillText(label, x + 2, Math.max(y - 4, 14));
  }
}

function _onCanvasClick(e) {
  const rect   = _canvas.getBoundingClientRect();
  const clickX = (e.clientX - rect.left) / rect.width  * (window._ptz_cam_w ?? 1280);
  const clickY = (e.clientY - rect.top)  / rect.height * (window._ptz_cam_h ?? 720);
  for (const d of _detections) {
    if (clickX >= d.x && clickX <= d.x + d.w &&
        clickY >= d.y && clickY <= d.y + d.h) {
      send({ type: "set_tracking", enabled: true, target_id: d.id });
      return;
    }
  }
  send({ type: "set_tracking", enabled: false });
}

// ─────────────────────────────────────────────────────────────────────────────
// HUD root
// ─────────────────────────────────────────────────────────────────────────────

function _drawHUD(w, h) {
  const roll  = _sRoll  ?? 0;
  const pitch = _sPitch ?? 0;
  const hdg   = _sHdg   ?? 0;

  _drawCompassTape(w, h, hdg);
  _drawInfoStrip(w, h);
  _drawHorizon(w, h, roll, pitch);
  _drawGimbalReadout(w, h);
}

// ─────────────────────────────────────────────────────────────────────────────
// Compass tape — bottom edge
// ─────────────────────────────────────────────────────────────────────────────

function _hdgLabel(deg) {
  const n = ((Math.round(deg) % 360) + 360) % 360;
  const cards = { 0:"N", 45:"NE", 90:"E", 135:"SE", 180:"S", 225:"SW", 270:"W", 315:"NW" };
  return cards[n] ?? String(n).padStart(3, "0");
}

function _drawCompassTape(w, h, hdg) {
  const ctx      = _ctx;
  const top      = h - TAPE_H;
  const pxPerDeg = w / 90;

  ctx.fillStyle = "rgba(0,0,0,0.6)";
  ctx.fillRect(0, top, w, TAPE_H);
  ctx.strokeStyle = "rgba(255,255,255,0.1)";
  ctx.lineWidth = 1;
  ctx.beginPath(); ctx.moveTo(0, top); ctx.lineTo(w, top); ctx.stroke();

  const start = Math.floor((hdg - 47) / 5) * 5;
  for (let d = start; d <= hdg + 47; d += 5) {
    const norm = ((d % 360) + 360) % 360;
    const x    = w / 2 + (d - hdg) * pxPerDeg;
    const isCard  = norm % 90 === 0;
    const isInter = norm % 45 === 0;
    const isTen   = norm % 10 === 0;
    const tickLen = isCard ? 18 : isInter ? 13 : isTen ? 9 : 5;

    ctx.strokeStyle = isCard ? "rgba(255,190,0,0.95)" : "rgba(200,200,200,0.45)";
    ctx.lineWidth   = isCard ? 2 : 1;
    ctx.beginPath(); ctx.moveTo(x, top + 1); ctx.lineTo(x, top + 1 + tickLen); ctx.stroke();

    if (isCard || isInter) {
      ctx.font         = isCard ? "bold 12px monospace" : "10px monospace";
      ctx.fillStyle    = isCard ? "rgba(255,190,0,1)" : "rgba(200,200,200,0.75)";
      ctx.textAlign    = "center";
      ctx.textBaseline = "bottom";
      ctx.fillText(_hdgLabel(norm), x, h - 5);
    }
  }

  // Center bug
  const cx = w / 2;
  ctx.fillStyle = "#29b6f6";
  ctx.beginPath();
  ctx.moveTo(cx, top + 1); ctx.lineTo(cx - 7, top + 12); ctx.lineTo(cx + 7, top + 12);
  ctx.closePath(); ctx.fill();

  // Heading readout
  const hdgStr = String(Math.round(hdg) % 360).padStart(3, "0") + "°";
  ctx.font = "bold 13px monospace";
  const tw = ctx.measureText(hdgStr).width + 12;
  ctx.fillStyle = "rgba(0,0,0,0.8)";
  ctx.fillRect(cx - tw / 2, top - 18, tw, 17);
  ctx.fillStyle = "#29b6f6";
  ctx.textAlign = "center"; ctx.textBaseline = "top";
  ctx.fillText(hdgStr, cx, top - 17);
}

// ─────────────────────────────────────────────────────────────────────────────
// Info strip — power / GPS / env, just above compass tape
// ─────────────────────────────────────────────────────────────────────────────

function _drawInfoStrip(w, h) {
  const { power, env, gps } = _telData;
  const parts = [];

  if (power?.ok) {
    parts.push(`${power.vin.toFixed(1)}V  ${power.curr.toFixed(0)}mA`);
  }
  if (gps?.fix) {
    parts.push(
      `GPS ${gps.lat.toFixed(5)}  ${gps.lon.toFixed(5)}  ${gps.sats}\u25C6  ${gps.spd_mph.toFixed(1)}mph`
    );
  } else if (gps) {
    parts.push("GPS NO FIX");
  }
  if (env) {
    parts.push(`${env.temp_f.toFixed(0)}\u00B0F  ${env.alt_ft.toFixed(0)}ft  ${env.press_inhg.toFixed(2)}inHg`);
  }

  if (!parts.length) return;

  const text = parts.join("   \u2502   ");
  const ctx  = _ctx;
  const y    = h - TAPE_H - 10;

  ctx.font = "22px monospace";
  ctx.textAlign    = "center";
  ctx.textBaseline = "bottom";
  const tw = ctx.measureText(text).width + 24;
  ctx.fillStyle = "rgba(0,0,0,0.55)";
  ctx.fillRect(w / 2 - tw / 2, y - 26, tw, 30);
  ctx.fillStyle = "rgba(220,220,220,0.85)";
  ctx.fillText(text, w / 2, y);
}

// ─────────────────────────────────────────────────────────────────────────────
// Gimbal readout — top-left
// ─────────────────────────────────────────────────────────────────────────────

function _drawGimbalReadout(w, h) {
  const { pan, tilt } = _telData;
  if (pan === undefined || tilt === undefined) return;

  const ctx  = _ctx;
  const panStr  = `PAN  ${pan  >= 0 ? "+" : ""}${pan.toFixed(1)}\u00B0`;
  const tiltStr = `TILT ${tilt >= 0 ? "+" : ""}${tilt.toFixed(1)}\u00B0`;

  ctx.font = "bold 26px monospace";
  const lw = Math.max(ctx.measureText(panStr).width, ctx.measureText(tiltStr).width) + 20;
  const lh = 68;

  ctx.fillStyle = "rgba(0,0,0,0.55)";
  ctx.fillRect(8, 8, lw, lh);

  ctx.fillStyle    = "#29b6f6";
  ctx.textAlign    = "left";
  ctx.textBaseline = "top";
  ctx.fillText(panStr,  16, 12);
  ctx.fillText(tiltStr, 16, 44);
}

// ─────────────────────────────────────────────────────────────────────────────
// Artificial horizon
// ─────────────────────────────────────────────────────────────────────────────

function _drawHorizon(w, h, roll, pitch) {
  const ctx      = _ctx;
  const cx       = w / 2;
  const cy       = h / 2;
  const pxPerDeg = h / 90;
  const rollRad  = (_hudLockRoll ? 0 : roll) * Math.PI / 180;
  const cosR     = Math.cos(rollRad);
  const sinR     = Math.sin(rollRad);

  // Rotate a canvas-relative offset [dx,dy] by roll around (cx,cy)
  function pt(dx, dy) {
    return [cx + dx * cosR - dy * sinR,
            cy + dx * sinR + dy * cosR];
  }

  // Clamp so horizon never leaves the canvas regardless of calibration state.
  const pitchOff = Math.max(-(cy - 20), Math.min(cy - 20, pitch * pxPerDeg));

  // Pitch ladder
  for (let p = -40; p <= 40; p += 5) {
    if (p === 0) continue;
    const py    = pitchOff + p * pxPerDeg;
    const major = p % 10 === 0;
    const len   = major ? 120 : 60;
    const [x1, y1] = pt(-len, py);
    const [x2, y2] = pt( len, py);
    ctx.strokeStyle = major ? "rgba(255,255,255,0.45)" : "rgba(255,255,255,0.25)";
    ctx.lineWidth   = major ? 1.5 : 1;
    ctx.beginPath(); ctx.moveTo(x1, y1); ctx.lineTo(x2, y2); ctx.stroke();
    if (major) {
      const lbl = String(Math.abs(p));
      const [lx, ly] = pt(len + 8, py);
      const [rx, ry] = pt(-(len + 8), py);
      ctx.font = "20px monospace"; ctx.fillStyle = "rgba(255,255,255,0.50)";
      ctx.textAlign = "left";  ctx.textBaseline = "middle"; ctx.fillText(lbl, lx, ly);
      ctx.textAlign = "right"; ctx.fillText(lbl, rx, ry);
    }
  }

  // Horizon line — amber, split
  const lineLen = w * 0.28;
  const gap     = 80;
  ctx.strokeStyle = "rgba(255,190,0,0.95)"; ctx.lineWidth = 4;
  const [ax1,ay1] = pt(-lineLen, pitchOff); const [ax2,ay2] = pt(-gap, pitchOff);
  const [bx1,by1] = pt( gap,     pitchOff); const [bx2,by2] = pt( lineLen, pitchOff);
  ctx.beginPath(); ctx.moveTo(ax1,ay1); ctx.lineTo(ax2,ay2); ctx.stroke();
  ctx.beginPath(); ctx.moveTo(bx1,by1); ctx.lineTo(bx2,by2); ctx.stroke();
}
