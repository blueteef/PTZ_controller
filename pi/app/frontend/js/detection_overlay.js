/**
 * detection_overlay.js — Canvas overlay for detection bounding boxes.
 *
 * Drawn on a <canvas> positioned absolutely over the <img> feed.
 * Clicking a box sends a set_tracking message to lock onto that detection.
 */

import { send } from "./app.js";

let _canvas = null;
let _ctx    = null;
let _img    = null;
let _detections = [];

const COLOURS = {
  face:    "#4caf50",
  default: "#ff9800",
};

export function initOverlay(imgEl, canvasEl) {
  _img    = imgEl;
  _canvas = canvasEl;
  _ctx    = canvasEl.getContext("2d");

  // Resize canvas to match img whenever it loads or resizes
  imgEl.addEventListener("load", syncSize);
  new ResizeObserver(syncSize).observe(imgEl);

  canvasEl.addEventListener("click", onCanvasClick);
}

function syncSize() {
  const imgRect  = _img.getBoundingClientRect();
  const wrapRect = _img.parentElement.getBoundingClientRect();

  _canvas.width  = _img.naturalWidth  || _img.offsetWidth;
  _canvas.height = _img.naturalHeight || _img.offsetHeight;
  _canvas.style.width  = _img.offsetWidth  + "px";
  _canvas.style.height = _img.offsetHeight + "px";
  _canvas.style.left   = (imgRect.left - wrapRect.left) + "px";
  _canvas.style.top    = (imgRect.top  - wrapRect.top)  + "px";
  redraw();
}

export function updateDetections(objects) {
  _detections = objects;
  // Enable pointer events on canvas when there are detections to click
  _canvas.className = objects.length > 0 ? "interactive" : "";
  redraw();
}

function redraw() {
  if (!_ctx) return;
  _ctx.clearRect(0, 0, _canvas.width, _canvas.height);

  const scaleX = _canvas.width  / (window._ptz_cam_w ?? 1280);
  const scaleY = _canvas.height / (window._ptz_cam_h ?? 720);

  for (const d of _detections) {
    const colour = COLOURS[d.label] ?? COLOURS.default;
    const x = d.x * scaleX, y = d.y * scaleY;
    const w = d.w * scaleX, h = d.h * scaleY;

    _ctx.strokeStyle = colour;
    _ctx.lineWidth   = 2;
    _ctx.strokeRect(x, y, w, h);

    const label = d.name ? `${d.name} (${(d.confidence * 100).toFixed(0)}%)`
                          : `${d.label} ${(d.confidence * 100).toFixed(0)}%`;

    _ctx.font      = "bold 12px system-ui";
    _ctx.fillStyle = colour;
    _ctx.fillText(label, x + 2, Math.max(y - 4, 14));
  }
}

function onCanvasClick(e) {
  const rect  = _canvas.getBoundingClientRect();
  const clickX = (e.clientX - rect.left) / rect.width  * (window._ptz_cam_w ?? 1280);
  const clickY = (e.clientY - rect.top)  / rect.height * (window._ptz_cam_h ?? 720);

  // Find which detection box was clicked
  for (const d of _detections) {
    if (clickX >= d.x && clickX <= d.x + d.w &&
        clickY >= d.y && clickY <= d.y + d.h) {
      send({ type: "set_tracking", enabled: true, target_id: d.id });
      return;
    }
  }

  // Clicked empty area — stop tracking
  send({ type: "set_tracking", enabled: false });
}
