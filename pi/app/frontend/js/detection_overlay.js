/**
 * detection_overlay.js — Canvas overlay: detection boxes + HUD instruments.
 *
 * HUD elements (drawn under detection boxes, hidden during detection mode):
 *   Compass tape  — scrolling bearing tape at top edge with azimuth tick marks
 *   Artificial horizon — center, tilts with roll, shifts with pitch
 *
 * Public API:
 *   initOverlay(imgEl, canvasEl)
 *   updateDetections(objects)
 *   setHUDEnabled(bool)          — HUD checkbox
 *   setDetectionMode(mode)       — "none"|"face"|"yolo"; HUD hides when active
 *   updateHUDData(imu, mag)      — feed latest sensor values, triggers redraw
 */

import { send } from "./app.js";

let _canvas = null;
let _ctx    = null;
let _img    = null;
let _detections = [];

let _hudEnabled          = false;
let _hudLockRoll         = false;
let _detectionModeActive = false;
let _imuData = null;   // { ok, roll, pitch }
let _magData = null;   // { ok, hdg }

const COLOURS = { face: "#4caf50", default: "#ff9800" };

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
  _imuData = imu;
  _magData = mag;
  if (_hudEnabled && !_detectionModeActive) _redraw();
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

  // HUD drawn first so detection boxes render on top
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
// HUD
// ─────────────────────────────────────────────────────────────────────────────

function _drawHUD(w, h) {
  const roll  = _imuData?.roll  ?? 0;
  const pitch = _imuData?.pitch ?? 0;
  const hdg   = ((_magData?.hdg ?? 0) + 360) % 360;
  _drawCompassTape(w, h, hdg);
  _drawHorizon(w, h, roll, pitch);
}

// ── Compass tape (top edge, scrolling azimuth marks) ─────────────────────────

function _hdgLabel(deg) {
  const n = ((Math.round(deg) % 360) + 360) % 360;
  const cards = { 0:"N", 45:"NE", 90:"E", 135:"SE", 180:"S", 225:"SW", 270:"W", 315:"NW" };
  return cards[n] ?? String(n).padStart(3, "0");
}

function _drawCompassTape(w, h, hdg) {
  const ctx      = _ctx;
  const tapeH    = 48;
  const top      = h - tapeH;   // tape sits at the bottom edge
  const pxPerDeg = w / 90;      // 90° spans full width

  // Background strip
  ctx.fillStyle = "rgba(0,0,0,0.6)";
  ctx.fillRect(0, top, w, tapeH);
  ctx.strokeStyle = "rgba(255,255,255,0.1)";
  ctx.lineWidth = 1;
  ctx.beginPath(); ctx.moveTo(0, top); ctx.lineTo(w, top); ctx.stroke();

  // Azimuth tick marks + labels — ticks extend downward from the top of the strip
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
    ctx.beginPath();
    ctx.moveTo(x, top + 1);
    ctx.lineTo(x, top + 1 + tickLen);
    ctx.stroke();

    if (isCard || isInter) {
      ctx.font         = isCard ? "bold 12px monospace" : "10px monospace";
      ctx.fillStyle    = isCard ? "rgba(255,190,0,1)" : "rgba(200,200,200,0.75)";
      ctx.textAlign    = "center";
      ctx.textBaseline = "bottom";
      ctx.fillText(_hdgLabel(norm), x, h - 5);
    }
  }

  // Center heading bug — triangle pointing up from bottom edge
  const cx = w / 2;
  ctx.fillStyle = "#29b6f6";
  ctx.beginPath();
  ctx.moveTo(cx,     top + 1);
  ctx.lineTo(cx - 7, top + 12);
  ctx.lineTo(cx + 7, top + 12);
  ctx.closePath();
  ctx.fill();

  // Heading readout in a box just above the tape
  const hdgStr = String(Math.round(hdg) % 360).padStart(3, "0") + "°";
  ctx.font = "bold 13px monospace";
  const tw = ctx.measureText(hdgStr).width + 12;
  ctx.fillStyle    = "rgba(0,0,0,0.8)";
  ctx.fillRect(cx - tw / 2, top - 18, tw, 17);
  ctx.fillStyle    = "#29b6f6";
  ctx.textAlign    = "center";
  ctx.textBaseline = "top";
  ctx.fillText(hdgStr, cx, top - 17);
}

// ── Artificial horizon ────────────────────────────────────────────────────────

function _drawHorizon(w, h, roll, pitch) {
  const ctx         = _ctx;
  const cx          = w / 2;
  const cy          = h / 2;
  const pxPerDeg    = h / 90;         // 45° pitch fills half the frame height
  const effectiveRoll = _hudLockRoll ? 0 : roll;
  const rollRad     = effectiveRoll * Math.PI / 180;
  const pitchOffset = pitch * pxPerDeg;  // nose up → line moves down; nose down → line moves up

  ctx.save();
  ctx.translate(cx, cy);
  ctx.rotate(rollRad);

  // Pitch ladder marks
  for (let p = -40; p <= 40; p += 5) {
    if (p === 0) continue;
    const py    = pitchOffset + p * pxPerDeg;
    const major = p % 10 === 0;
    const len   = major ? 60 : 30;
    ctx.strokeStyle = "rgba(255,255,255,0.35)";
    ctx.lineWidth   = major ? 1.5 : 1;
    ctx.beginPath();
    ctx.moveTo(-len, py);
    ctx.lineTo( len, py);
    ctx.stroke();
    if (major) {
      const lbl = String(Math.abs(p));
      ctx.font         = "10px monospace";
      ctx.fillStyle    = "rgba(255,255,255,0.5)";
      ctx.textAlign    = "left";
      ctx.textBaseline = "middle";
      ctx.fillText(lbl,  len + 5, py);
      ctx.textAlign    = "right";
      ctx.fillText(lbl, -len - 5, py);
    }
  }

  // Horizon line — amber, split at center
  const lineLen = w * 0.28;
  const gap     = 52;
  ctx.strokeStyle = "rgba(255,190,0,0.9)";
  ctx.lineWidth   = 2;
  ctx.beginPath(); ctx.moveTo(-lineLen, pitchOffset); ctx.lineTo(-gap, pitchOffset); ctx.stroke();
  ctx.beginPath(); ctx.moveTo( gap, pitchOffset);     ctx.lineTo( lineLen, pitchOffset); ctx.stroke();

  // Aircraft reference symbol — fixed at center (doesn't move with pitch)
  ctx.strokeStyle = "#29b6f6";
  ctx.lineWidth   = 2.5;
  ctx.beginPath(); ctx.moveTo(-42, 0); ctx.lineTo(-18, 0); ctx.lineTo(-12, 7); ctx.stroke();
  ctx.beginPath(); ctx.moveTo( 42, 0); ctx.lineTo( 18, 0); ctx.lineTo( 12, 7); ctx.stroke();
  ctx.beginPath(); ctx.arc(0, 0, 3, 0, Math.PI * 2); ctx.fillStyle = "#29b6f6"; ctx.fill();

  ctx.restore();
}
