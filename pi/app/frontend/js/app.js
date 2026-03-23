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
// Speed slider
// ---------------------------------------------------------------------------

function wireSpeedSlider() {
  const slider = document.getElementById("speed-slider");
  const label  = document.getElementById("speed-val");

  // Use server-configured default if available, else 45
  const defaultSpeed = window._ptz_default_speed ?? 45;
  slider.value = defaultSpeed;
  label.textContent = defaultSpeed;
  window._ptz_max_vel = defaultSpeed;

  slider.oninput = () => {
    label.textContent = slider.value;
    window._ptz_max_vel = parseFloat(slider.value);
  };
}

// ---------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------

window.addEventListener("DOMContentLoaded", async () => {
  // Fetch server config before wiring controls so defaults are applied
  try {
    const res = await fetch("/api/settings/ui");
    const cfg = await res.json();
    window._ptz_default_speed = cfg.default_speed;
    window._ptz_cam_w = 1280;
    window._ptz_cam_h = 720;
  } catch (e) {
    console.warn("Could not fetch server config", e);
  }

  connect();
  wireButtons();
  wireDetectionMode();
  wireSpeedSlider();
  initJoystick(document.getElementById("joystick"));
  initOverlay(
    document.getElementById("feed"),
    document.getElementById("overlay"),
  );
});
