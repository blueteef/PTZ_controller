/**
 * app.js — Main entry point.
 *
 * Responsibilities:
 *  - Open WebSocket to /ws/control
 *  - Dispatch inbound telemetry/detection messages to UI updaters
 *  - Wire up all controls (joystick, buttons, keyboard, detection mode)
 *  - Export send() for other modules to use
 */

import { initJoystick, stopSending } from "./controls.js";
import { initOverlay, updateDetections } from "./detection_overlay.js";

// ---------------------------------------------------------------------------
// WebSocket
// ---------------------------------------------------------------------------

const WS_URL = `ws://${location.host}/ws/control`;
let ws = null;
let wsRetryMs = 1000;

export function send(msg) {
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(msg));
  }
}

function connect() {
  ws = new WebSocket(WS_URL);

  ws.onopen = () => {
    console.log("WS connected");
    wsRetryMs = 1000;
  };

  ws.onmessage = ({ data }) => {
    try {
      const msg = JSON.parse(data);
      handleMessage(msg);
    } catch (e) {
      console.warn("Bad WS message", e);
    }
  };

  ws.onclose = ws.onerror = () => {
    ws = null;
    setTimeout(connect, wsRetryMs);
    wsRetryMs = Math.min(wsRetryMs * 2, 10000);
    setSerialStatus(false);
  };
}

// ---------------------------------------------------------------------------
// Inbound message handlers
// ---------------------------------------------------------------------------

function handleMessage(msg) {
  switch (msg.type) {
    case "telemetry":
      setSerialStatus(msg.serial_ok);
      setTrackingStatus(msg.tracking_active);
      setPosition(msg.pan, msg.tilt);
      break;
    case "detections":
      updateDetections(msg.objects);
      break;
    case "error":
      console.warn("Server error:", msg.message);
      break;
  }
}

// ---------------------------------------------------------------------------
// Status bar
// ---------------------------------------------------------------------------

function setSerialStatus(ok) {
  const el = document.getElementById("serial-badge");
  el.textContent = ok ? "ESP32 ✓" : "ESP32 ✗";
  el.className = "badge " + (ok ? "ok" : "bad");
}

function setTrackingStatus(active) {
  const el = document.getElementById("tracking-badge");
  el.textContent = active ? "Tracking ON" : "Tracking OFF";
  el.className = "badge " + (active ? "on" : "off");

  const stopBtn = document.getElementById("btn-track-stop");
  stopBtn.disabled = !active;
}

function setPosition(pan, tilt) {
  document.getElementById("pos-display").textContent =
    `pan ${pan.toFixed(1)}°  tilt ${tilt.toFixed(1)}°`;
}

// ---------------------------------------------------------------------------
// Button wiring
// ---------------------------------------------------------------------------

function wireButtons() {
  document.getElementById("btn-stop").onclick = () => {
    stopSending();                          // kill joystick interval first
    send({ type: "stop", axis: "all" });
  };

  document.getElementById("btn-estop").onclick = () => {
    stopSending();                          // kill joystick interval first
    send({ type: "estop" });
  };

  document.getElementById("btn-home").onclick = () =>
    send({ type: "home", axis: "all" });

  document.getElementById("btn-track-stop").onclick = () =>
    send({ type: "set_tracking", enabled: false });
}

// ---------------------------------------------------------------------------
// Detection mode selector
// ---------------------------------------------------------------------------

function wireDetectionMode() {
  const sel = document.getElementById("det-mode");
  sel.onchange = () => {
    send({ type: "set_detection", mode: sel.value });
  };
}

// ---------------------------------------------------------------------------
// Speed slider — controls both joystick max vel and tracking speed cap
// ---------------------------------------------------------------------------

function wireSpeedSlider() {
  const slider = document.getElementById("speed-slider");
  const label  = document.getElementById("speed-val");

  const defaultSpeed = window._ptz_default_speed ?? 45;
  slider.value = defaultSpeed;
  label.textContent = defaultSpeed;
  window._ptz_max_vel = defaultSpeed;

  slider.oninput = () => {
    const v = parseFloat(slider.value);
    label.textContent = v;
    window._ptz_max_vel = v;
    send({ type: "update_settings", tracking_speed: v });
  };
}

// ---------------------------------------------------------------------------
// Acceleration slider
// ---------------------------------------------------------------------------

function wireAccelSlider() {
  const slider = document.getElementById("accel-slider");
  const label  = document.getElementById("accel-val");

  const defaultAccel = window._ptz_default_accel ?? 120;
  slider.value = defaultAccel;
  label.textContent = defaultAccel;

  slider.oninput = () => {
    const v = parseFloat(slider.value);
    label.textContent = v;
    send({ type: "update_settings", accel: v });
  };
}

// ---------------------------------------------------------------------------
// Axis invert checkboxes
// ---------------------------------------------------------------------------

function wireInvertToggles() {
  const panCb  = document.getElementById("pan-invert");
  const tiltCb = document.getElementById("tilt-invert");

  panCb.checked  = window._ptz_pan_invert  ?? false;
  tiltCb.checked = window._ptz_tilt_invert ?? false;

  panCb.onchange  = () => send({ type: "update_settings", pan_invert:  panCb.checked });
  tiltCb.onchange = () => send({ type: "update_settings", tilt_invert: tiltCb.checked });
}

// ---------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------

window.addEventListener("DOMContentLoaded", async () => {
  // Fetch server config before wiring controls so defaults are applied
  try {
    const res = await fetch("/api/settings/ui");
    const cfg = await res.json();
    window._ptz_default_speed  = cfg.default_speed;
    window._ptz_default_accel  = cfg.accel_deg_s2;
    window._ptz_pan_invert     = cfg.pan_invert;
    window._ptz_tilt_invert    = cfg.tilt_invert;
    window._ptz_cam_w = 1280;
    window._ptz_cam_h = 720;
  } catch (e) {
    console.warn("Could not fetch server config", e);
  }

  connect();
  wireButtons();
  wireDetectionMode();
  wireSpeedSlider();
  wireAccelSlider();
  wireInvertToggles();
  initJoystick(document.getElementById("joystick"));
  initOverlay(
    document.getElementById("feed"),
    document.getElementById("overlay"),
  );
});
