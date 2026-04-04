/**
 * app.js — Main entry point.
 *
 * Responsibilities:
 *  - Open WebSocket to /ws/control
 *  - Dispatch inbound telemetry/detection messages to UI updaters
 *  - Wire up all controls (joystick, buttons, keyboard, detection mode)
 *  - Export send() for other modules to use
 */

import { stopSending } from "./controls.js";
import {
  initOverlay, updateDetections,
  setHUDEnabled, setHUDLockRoll, setDetectionMode, updateHUDData,
} from "./detection_overlay.js";

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
      if (msg.power) updateSensorPower(msg.power);
      if (msg.env)   updateSensorEnv(msg.env);
      if (msg.gps)   updateSensorGPS(msg.gps);
      if (msg.imu)   updateSensorIMU(msg.imu);
      if (msg.mag)   updateSensorMag(msg.mag);
      updateHUDData(msg.imu ?? null, msg.mag ?? null);
      syncStabCheckboxes(msg);
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
  document.getElementById("d-pan").textContent  = `${pan.toFixed(1)}°`;
  document.getElementById("d-tilt").textContent = `${tilt.toFixed(1)}°`;
}

function updateSensorPower(p) {
  const vinEl = document.getElementById("d-vin");
  vinEl.textContent = p.ok ? `${p.vin.toFixed(2)} V` : "ERR";
  vinEl.className   = "dash-val " + (p.ok ? "" : "bad");
  document.getElementById("d-curr").textContent = p.ok ? `${p.curr.toFixed(0)} mA`          : "—";
  document.getElementById("d-pwr").textContent  = p.ok ? `${(p.pwr / 1000).toFixed(2)} W`   : "—";
}

function updateSensorEnv(e) {
  document.getElementById("d-temp").textContent  = `${e.temp_f.toFixed(1)} °F`;
  document.getElementById("d-press").textContent = `${e.press_inhg.toFixed(2)} inHg`;
  document.getElementById("d-alt").textContent   = `${e.alt_ft.toFixed(0)} ft`;
}

function updateSensorIMU(m) {
  const base = "dash-val";
  const rollEl  = document.getElementById("d-roll");
  const pitchEl = document.getElementById("d-pitch");
  if (m.ok) {
    rollEl.textContent  = `${m.roll.toFixed(1)}°`;
    pitchEl.textContent = `${m.pitch.toFixed(1)}°`;
    rollEl.className = pitchEl.className = base;
  } else {
    rollEl.textContent  = "ERR";
    pitchEl.textContent = "ERR";
    rollEl.className = pitchEl.className = base + " bad";
  }
}

function updateSensorMag(m) {
  const el = document.getElementById("d-mag-hdg");
  if (m.ok) {
    el.textContent = `${m.hdg.toFixed(0)}°`;
    el.className = "dash-val";
  } else {
    el.textContent = "ERR";
    el.className = "dash-val bad";
  }
}

function updateSensorGPS(g) {
  const fixEl = document.getElementById("d-gps-fix");
  fixEl.textContent = g.fix ? "YES" : "NO";
  fixEl.className   = "dash-val " + (g.fix ? "ok" : "bad");
  document.getElementById("d-gps-sats").textContent = g.sats;
  document.getElementById("d-gps-lat").textContent  = g.fix ? g.lat.toFixed(6) : "—";
  document.getElementById("d-gps-lon").textContent  = g.fix ? g.lon.toFixed(6) : "—";
  document.getElementById("d-gps-hdg").textContent  = `${g.hdg.toFixed(0)}°`;
  document.getElementById("d-gps-spd").textContent  = `${g.spd_mph.toFixed(1)} mph`;
}

// ---------------------------------------------------------------------------
// HUD toggle
// ---------------------------------------------------------------------------

function wireHUD() {
  const cb     = document.getElementById("hud-toggle");
  const cbLock = document.getElementById("hud-lock-roll");
  cb.onchange     = () => setHUDEnabled(cb.checked);
  cbLock.onchange = () => setHUDLockRoll(cbLock.checked);
}

// ---------------------------------------------------------------------------
// Sidebar toggle
// ---------------------------------------------------------------------------

function wireSidebarToggle() {
  const btn     = document.getElementById("btn-sidebar-toggle");
  const sidebar = document.getElementById("sidebar");
  btn.onclick = () => {
    const collapsed = sidebar.classList.toggle("collapsed");
    btn.textContent = collapsed ? "\u25B6" : "\u25C4";   // ▶ / ◀
    btn.title = collapsed ? "Show sidebar" : "Hide sidebar";
  };
}

// ---------------------------------------------------------------------------
// Stabilization checkboxes
// ---------------------------------------------------------------------------

function syncStabCheckboxes(msg) {
  // Keep checkboxes in sync when a second client connects or after reconnect.
  if (msg.stab_roll    !== undefined) document.getElementById("stab-roll").checked    = msg.stab_roll;
  if (msg.stab_pitch   !== undefined) document.getElementById("stab-pitch").checked   = msg.stab_pitch;
  if (msg.stab_heading !== undefined) document.getElementById("stab-heading").checked = msg.stab_heading;
}

function _sendStab() {
  send({
    type:    "set_stabilization",
    roll:    document.getElementById("stab-roll").checked,
    pitch:   document.getElementById("stab-pitch").checked,
    heading: document.getElementById("stab-heading").checked,
  });
}

function wireStabilization() {
  document.getElementById("stab-roll").onchange    = _sendStab;
  document.getElementById("stab-pitch").onchange   = _sendStab;
  document.getElementById("stab-heading").onchange = _sendStab;
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
    setDetectionMode(sel.value);
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
    send({ type: "update_settings", speed: v, tracking_speed: v });
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
// Face recognition
// ---------------------------------------------------------------------------

async function loadFaces() {
  try {
    const res  = await fetch("/api/faces");
    const data = await res.json();
    renderFaceList(data.faces);
    populateTrackSelect(data.faces);
  } catch (e) {
    console.warn("Could not load faces", e);
  }
}

function renderFaceList(faces) {
  const el = document.getElementById("faces-list");
  if (!faces.length) { el.innerHTML = '<p class="hint">No faces enrolled.</p>'; return; }

  // Group by name
  const byName = {};
  for (const f of faces) {
    if (!byName[f.name]) byName[f.name] = [];
    byName[f.name].push(f);
  }

  el.innerHTML = Object.entries(byName).map(([name, entries]) => `
    <div class="face-row">
      <span class="face-name">${name}</span>
      <span class="face-count">${entries.length} sample${entries.length > 1 ? "s" : ""}</span>
      <button class="btn-del-face" data-name="${name}">✕</button>
    </div>
  `).join("");

  el.querySelectorAll(".btn-del-face").forEach(btn => {
    btn.onclick = async () => {
      const name = btn.dataset.name;
      await fetch(`/api/faces/name/${encodeURIComponent(name)}`, { method: "DELETE" });
      loadFaces();
    };
  });
}

function populateTrackSelect(faces) {
  const sel = document.getElementById("track-name-select");
  const prev = sel.value;
  // Unique names
  const names = [...new Set(faces.map(f => f.name))].sort();
  sel.innerHTML = '<option value="">— Track by name —</option>' +
    names.map(n => `<option value="${n}"${n === prev ? " selected" : ""}>${n}</option>`).join("");
}

function wireFaceRecognition() {
  const toggle = document.getElementById("recog-toggle");
  toggle.onchange = () => {
    send({ type: "set_recognition", enabled: toggle.checked });
    document.getElementById("face-section").classList.toggle("recog-on", toggle.checked);
  };

  document.getElementById("btn-enroll").onclick = async () => {
    const name = document.getElementById("enroll-name").value.trim();
    const status = document.getElementById("enroll-status");
    if (!name) { status.textContent = "Enter a name first."; return; }

    status.textContent = "Enrolling…";
    try {
      const res  = await fetch("/api/faces/enroll", {
        method:  "POST",
        headers: { "Content-Type": "application/json" },
        body:    JSON.stringify({ name }),
      });
      const data = await res.json();
      if (res.ok) {
        status.textContent = `✓ Enrolled "${data.name}" (${data.samples} samples)`;
        document.getElementById("enroll-name").value = "";
        document.activeElement?.blur();
        loadFaces();
      } else {
        status.textContent = `✗ ${data.detail ?? "Enrollment failed"}`;
      }
    } catch (e) {
      status.textContent = `✗ ${e}`;
    }
  };

  document.getElementById("btn-track-name").onclick = () => {
    const name = document.getElementById("track-name-select").value;
    if (!name) return;
    send({ type: "set_recognition", enabled: true });
    document.getElementById("recog-toggle").checked = true;
    document.getElementById("face-section").classList.add("recog-on");
    send({ type: "set_tracking", enabled: true, target_name: name });
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
    window._ptz_default_speed  = cfg.max_speed_deg_s;
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
  wireHUD();
  wireSidebarToggle();
  wireSpeedSlider();
  wireAccelSlider();
  wireInvertToggles();
  wireStabilization();
  wireFaceRecognition();
  loadFaces();
  initOverlay(
    document.getElementById("feed"),
    document.getElementById("overlay"),
  );
});
