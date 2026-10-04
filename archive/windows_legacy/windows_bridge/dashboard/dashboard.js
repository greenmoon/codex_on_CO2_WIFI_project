const $ = (id) => document.getElementById(id);
const state = { latestData: null, lastServerAt: 0, lastPayloadAt: 0, mqttMessages: null, socketConnected: false, disconnectedAt: 0, needsRecoverySnapshot: true, retryMs: 1000, timer: null };

function setLed(id, status, breathing = false) {
  $(id).className = `led ${status}${breathing ? " breathing" : ""}`;
}

function updateClock() {
  $("current-time").textContent = new Date().toLocaleString("zh-TW", { hour12: false });
}

function payloadAgeMs() {
  return state.lastPayloadAt ? Math.max(0, Date.now() - state.lastPayloadAt) : Infinity;
}

function updateMonitoring() {
  const data = state.latestData;
  const ageMs = payloadAgeMs();
  const liveWindowMs = Math.max(15000, (Number(data?.publish_interval_ms) || 1000) * 5);
  const fresh = data?.bridge_ready === true && ageMs <= liveWindowMs;
  const stale = data?.bridge_ready === true && ageMs > liveWindowMs && ageMs <= 30000;
  if (!state.socketConnected) {
    const recovering = !state.disconnectedAt || (Date.now() - state.disconnectedAt <= 30000 && ageMs <= 30000);
    const status = recovering ? "waiting" : "offline";
    setLed("led-mqtt", status);
    setLed("led-payload", status);
    setLed("led-bridge", status);
    setLed("led-iphone", status);
    setLed("led-dashboard", status);
    setLed("led-ready", status);
    setState(recovering ? "RECOVERING" : "OFFLINE", false);
    return;
  }
  setLed("led-dashboard", "live");
  setLed("led-mqtt", data?.mqtt_connected ? "live" : "offline");
  setLed("led-payload", fresh ? "live" : stale ? "waiting" : "offline", fresh);
  setLed("led-bridge", fresh ? "live" : stale ? "waiting" : "offline");
  setLed("led-iphone", Number(data?.ws_clients) > 0 ? "live" : "waiting");
  setLed("led-ready", fresh ? "live" : stale ? "waiting" : "offline");
  setState(fresh ? "LIVE" : stale ? "STALE" : "WAITING", fresh);
}

function qualityFor(co2) {
  if (co2 < 800) return ["良好", "#00a8a6"];
  if (co2 < 1000) return ["普通", "#d69a00"];
  if (co2 < 1400) return ["偏高", "#df7628"];
  return ["需要通風", "#d84545"];
}

function setState(text, live) {
  $("connection-state").textContent = text;
  document.querySelector(".bridge-dot").classList.toggle("live", live);
}

function setBar(id, value) {
  $(id).style.setProperty("--fill", `${Math.max(0, Math.min(100, Number(value) || 0))}%`);
}

function render(data) {
  state.latestData = data;
  state.lastServerAt = Date.now();
  if (state.mqttMessages !== data.mqtt_messages || (state.needsRecoverySnapshot && data.bridge_ready === true)) {
    state.mqttMessages = data.mqtt_messages;
    state.lastPayloadAt = Date.now();
  }
  if (data.bridge_ready === true) state.needsRecoverySnapshot = false;
  $("network-name").textContent = data.network_name || "Remote Wi-Fi";
  $("mqtt-topic").textContent = data.mqtt_topic || "co2";
  $("mqtt-messages").textContent = data.mqtt_messages ?? 0;
  const ready = data.bridge_ready === true && Number.isFinite(data.co2_ppm);
  $("ready-state").textContent = ready ? "MQTT → iPhone READY" : "MQTT → iPhone WAITING";
  if (!ready) {
    $("co2").textContent = "---";
    $("temperature").textContent = "--.-";
    $("humidity").textContent = "--";
    $("battery").textContent = "---";
    $("quality").textContent = data.mqtt_error || "等待 MQTT payload";
    updateMonitoring();
    return;
  }
  $("co2").textContent = Math.round(data.co2_ppm);
  $("temperature").textContent = Number(data.temperature_c).toFixed(1);
  $("humidity").textContent = Math.round(data.humidity_pct);
  $("battery").textContent = Math.round(data.battery_pct);
  setBar("temperature-bar", Math.min(100, Math.max(0, (Number(data.temperature_c) + 10) * 2)));
  setBar("humidity-bar", data.humidity_pct);
  setBar("battery-bar", data.battery_pct);
  const [quality, color] = qualityFor(data.co2_ppm);
  $("quality").textContent = quality;
  $("quality").style.color = color;
  updateMonitoring();
}

function connect() {
  const protocol = location.protocol === "https:" ? "wss" : "ws";
  const socket = new WebSocket(`${protocol}://${location.host}/ws`);
  socket.onopen = () => { state.retryMs = 1000; state.socketConnected = true; state.disconnectedAt = 0; state.needsRecoverySnapshot = true; updateMonitoring(); };
  socket.onmessage = (event) => { try { render(JSON.parse(event.data)); } catch (error) { console.warn("Invalid MQTT payload", error); } };
  socket.onclose = () => {
    state.socketConnected = false;
    state.disconnectedAt = Date.now();
    updateMonitoring();
    clearTimeout(state.timer);
    state.timer = setTimeout(connect, state.retryMs);
    state.retryMs = Math.min(state.retryMs * 2, 30000);
  };
  socket.onerror = () => socket.close();
}

setInterval(() => {
  if (!state.lastServerAt) return;
  const age = Math.floor(payloadAgeMs() / 1000);
  $("payload-age").textContent = `${age} s`;
  updateMonitoring();
}, 1000);

updateClock();
setInterval(updateClock, 1000);
connect();
