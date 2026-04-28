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
  updateTelemetryHUD,
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
      if (msg.mag?.ok) _lastMagHdg = msg.mag.hdg;
      updateHUDData(msg.imu ?? null, msg.mag ?? null);
      updateTelemetryHUD(msg.pan, msg.tilt, msg.power ?? null, msg.env ?? null, msg.gps ?? null);
      updateUPSStatus(msg.ups ?? null);
      syncStabCheckboxes(msg);
      break;
    case "detections":
      updateDetections(msg.objects);
      break;
    case "compass_calibrated":
      console.log(`Compass offset → ${msg.offset.toFixed(2)}°`);
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
  el.textContent = ok ? "CAN ✓" : "CAN ✗";
  el.className = "badge " + (ok ? "ok" : "bad");
}

function setTrackingStatus(active) {
  const el = document.getElementById("tracking-badge");
  el.textContent = active ? "Tracking ON" : "Tracking OFF";
  el.className = "badge " + (active ? "on" : "off");

  const stopBtn = document.getElementById("btn-track-stop");
  stopBtn.disabled = !active;
}

// Last known compass heading, kept in sync by updateHUDData via telemetry
let _lastMagHdg = null;

function updateUPSStatus(ups) {
  const el = document.getElementById("ups-badge");
  if (!el) return;
  if (!ups) { el.style.display = "none"; return; }
  el.style.display = "";
  const pct = ups.pct ?? "?";
  const charging = ups.vin_ok ? "⚡" : "🔋";
  el.textContent = `${charging} ${pct}%`;
  el.className = "badge " + (pct <= 10 ? "bad" : pct <= 30 ? "warn" : "ok");
}

function setPosition(pan, tilt) {
  document.getElementById("pos-display").textContent =
    `pan ${pan.toFixed(1)}°  tilt ${tilt.toFixed(1)}°`;
}

function wireCompassCal() {
  const input = document.getElementById("compass-cal-input");
  const btn   = document.getElementById("btn-compass-cal");

  // Pre-fill with last known heading when user focuses the box
  input.onfocus = () => {
    if (!input.value && _lastMagHdg !== null) {
      input.value = _lastMagHdg.toFixed(1);
    }
  };

  btn.onclick = () => {
    const known = parseFloat(input.value);
    if (isNaN(known)) return;
    send({ type: "calibrate_compass", heading: ((known % 360) + 360) % 360 });
    input.value = "";
    btn.textContent = "✓";
    setTimeout(() => { btn.textContent = "Set"; }, 1500);
  };
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
// Webcam PIP / Main
// ---------------------------------------------------------------------------

function wireWebcam() {
  const btn      = document.getElementById("btn-webcam");
  const pip      = document.getElementById("webcam-pip");
  const pipImg   = document.getElementById("webcam-feed");
  const pipLabel = pip.querySelector(".pip-label");
  const mainFeed = document.getElementById("feed");
  const videoWrap = document.getElementById("video-wrap");
  const overlay  = document.getElementById("overlay");

  // 0 = off, 1 = webcam PIP (IMX462 main), 2 = webcam main (IMX462 in PIP)
  let state = 0;

  function setSrc(el, url) {
    el.src = "";
    el.src = url ? url + "?t=" + Date.now() : "";
  }

  function apply() {
    if (state === 0) {
      setSrc(pipImg, "");
      pip.classList.add("pip-hidden");
      setSrc(mainFeed, "/stream");
      videoWrap.classList.remove("webcam-main");
      overlay.style.visibility = "";
      btn.textContent = "Webcam: Off";
      btn.classList.remove("active");
    } else if (state === 1) {
      setSrc(pipImg, "/webcam/stream");
      pipLabel.textContent = "WEBCAM";
      pip.classList.remove("pip-hidden");
      setSrc(mainFeed, "/stream");
      videoWrap.classList.remove("webcam-main");
      overlay.style.visibility = "";
      btn.textContent = "Webcam: PIP";
      btn.classList.add("active");
    } else {
      setSrc(pipImg, "/stream");
      pipLabel.textContent = "IMX462";
      pip.classList.remove("pip-hidden");
      setSrc(mainFeed, "/webcam/stream");
      videoWrap.classList.add("webcam-main");
      overlay.style.visibility = "hidden";
      btn.textContent = "Webcam: Main";
      btn.classList.add("active");
    }
  }

  btn.onclick = () => {
    state = (state + 1) % 3;
    apply();
  };

  pip.onclick = () => {
    if (state > 0) { state--; apply(); }
  };

  fetch("/webcam/status")
    .then(r => r.json())
    .then(data => {
      if (!data.available) {
        btn.disabled = true;
        btn.title = "Webcam not detected";
      }
    })
    .catch(e => console.error("Webcam status fetch failed:", e));
}

// ---------------------------------------------------------------------------
// Thermal PIP
// ---------------------------------------------------------------------------

function wireThermal() {
  const btn      = document.getElementById("btn-thermal");
  const pip      = document.getElementById("thermal-pip");
  const pipImg   = document.getElementById("thermal-feed");
  const pipLabel = pip.querySelector(".pip-label");
  const mainFeed = document.getElementById("feed");
  const videoWrap = document.getElementById("video-wrap");
  const overlay  = document.getElementById("overlay");

  // 0 = off, 1 = thermal PIP, 2 = thermal main (Pi cam in PIP)
  let state = 0;

  // Cache-bust MJPEG src assignments so the browser opens a fresh connection
  function setSrc(el, url) {
    el.src = "";
    el.src = url ? url + "?t=" + Date.now() : "";
  }

  function apply() {
    if (state === 0) {
      setSrc(pipImg, "");
      pip.classList.add("pip-hidden");
      setSrc(mainFeed, "/stream");
      videoWrap.classList.remove("thermal-main");
      overlay.style.visibility = "";
      btn.textContent = "Thermal: Off";
      btn.classList.remove("active");
    } else if (state === 1) {
      setSrc(pipImg, "/thermal/stream");
      pipLabel.textContent = "THERMAL";
      pip.classList.remove("pip-hidden");
      setSrc(mainFeed, "/stream");
      videoWrap.classList.remove("thermal-main");
      overlay.style.visibility = "";
      btn.textContent = "Thermal: PIP";
      btn.classList.add("active");
    } else {
      setSrc(pipImg, "/stream");
      pipLabel.textContent = "VISIBLE";
      pip.classList.remove("pip-hidden");
      setSrc(mainFeed, "/thermal/stream");
      videoWrap.classList.add("thermal-main");
      overlay.style.visibility = "hidden";
      btn.textContent = "Thermal: Main";
      btn.classList.add("active");
    }
  }

  btn.onclick = () => {
    state = (state + 1) % 3;
    apply();
  };

  // Clicking the PIP steps back one state
  pip.onclick = () => {
    if (state > 0) { state--; apply(); }
  };

  // Fetch status — disable button if unavailable, populate settings if available
  fetch("/thermal/status")
    .then(r => r.json())
    .then(data => {
      if (!data.available) {
        btn.disabled = true;
        btn.title = "Thermal camera not detected";
        return;
      }
      wireThermalSettings(data.settings, data.colormaps);
    })
    .catch(e => console.error("Thermal status fetch failed:", e));
}

// ---------------------------------------------------------------------------
// Thermal settings sidebar
// ---------------------------------------------------------------------------

function wireThermalSettings(settings, colormaps) {
  const section    = document.getElementById("thermal-settings-section");
  const cmSelect   = document.getElementById("thermal-colormap");
  const brightSldr = document.getElementById("thermal-brightness");
  const brightVal  = document.getElementById("thermal-brightness-val");
  const contSldr   = document.getElementById("thermal-contrast");
  const contVal    = document.getElementById("thermal-contrast-val");

  // Show the section first — don't let a later error keep it hidden
  section.style.display = "";

  // Populate colormap dropdown
  (colormaps || []).forEach(cm => {
    const opt = document.createElement("option");
    opt.value = cm;
    opt.textContent = cm === "camera" ? "Camera Default" : cm.charAt(0) + cm.slice(1).toLowerCase();
    if (settings && cm === settings.colormap) opt.selected = true;
    cmSelect.appendChild(opt);
  });

  // Contrast slider is x10 internally (range 5–30 = 0.5–3.0)
  if (settings) {
    brightSldr.value     = settings.brightness ?? 0;
    brightVal.textContent = settings.brightness ?? 0;
    contSldr.value        = Math.round((settings.contrast ?? 1.0) * 10);
    contVal.textContent   = (settings.contrast ?? 1.0).toFixed(1);
  }

  let debounce = null;
  function pushSettings() {
    clearTimeout(debounce);
    debounce = setTimeout(() => {
      fetch("/thermal/settings", {
        method: "PUT",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          colormap:   cmSelect.value,
          brightness: parseInt(brightSldr.value),
          contrast:   parseInt(contSldr.value) / 10,
        }),
      }).catch(() => {});
    }, 80);
  }

  cmSelect.onchange = pushSettings;

  brightSldr.oninput = () => {
    brightVal.textContent = brightSldr.value;
    pushSettings();
  };

  contSldr.oninput = () => {
    contVal.textContent = (parseInt(contSldr.value) / 10).toFixed(1);
    pushSettings();
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

  // Sidebar starts collapsed — sync the toggle button label
  const _sidebarToggle = document.getElementById("btn-sidebar-toggle");
  _sidebarToggle.textContent = "\u25B6";   // ▶
  _sidebarToggle.title = "Show sidebar";

  connect();
  wireButtons();
  wireDetectionMode();
  wireHUD();
  wireSidebarToggle();
  wireSpeedSlider();
  wireAccelSlider();
  wireStabilization();
  wireCompassCal();
  wireFaceRecognition();
  wireWebcam();
  wireThermal();
  loadFaces();
  initOverlay(
    document.getElementById("feed"),
    document.getElementById("overlay"),
  );
});
