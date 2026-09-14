const $ = (id) => document.getElementById(id);
const HISTORY_KEY = "co2_wifi_health_v1";
const state = { socket: null, lastRx: 0, retry: 1000, timer: null, preview: location.protocol === "file:", previous: {}, history: [] };

function saveHistory() {
  try { localStorage.setItem(HISTORY_KEY, JSON.stringify(state.history)); } catch (error) { console.warn("Health history unavailable", error); }
}

function renderHistory() {
  const list = $("health-history");
  list.replaceChildren();
  if (!state.history.length) {
    const empty = document.createElement("li");
    empty.className = "health-empty";
    empty.textContent = "No failure history";
    list.append(empty);
  }
  state.history.forEach((entry) => {
    const item = document.createElement("li");
    item.className = entry.level;
    const meta = document.createElement("div");
    meta.innerHTML = `<span class="health-id">#${String(entry.id).padStart(3, "0")}</span><time class="health-time"></time>`;
    meta.querySelector("time").textContent = entry.time;
    const reason = document.createElement("span");
    reason.textContent = entry.reason;
    const result = document.createElement("span");
    result.className = "health-result";
    result.textContent = entry.result;
    item.append(meta, reason, result);
    list.append(item);
  });
  const unresolved = state.history.some((entry) => entry.level !== "recovered");
  $("health-summary").className = unresolved ? "health-warn" : "health-ok";
  $("health-summary").textContent = unresolved ? "RECOVERING" : "HEALTHY";
}

function addHealthEvent(reason, result, level = "fixing") {
  const nextId = Math.max(0, ...state.history.map((entry) => Number(entry.id) || 0)) + 1;
  state.history.unshift({ id: nextId, time: new Date().toLocaleString("zh-TW", { hour12: false }), reason, result, level });
  state.history = state.history.slice(0, 30);
  saveHistory();
  renderHistory();
}

function resolveHealthEvent(reason, result) {
  const entry = state.history.find((item) => item.reason === reason && item.level !== "recovered");
  if (entry) {
    entry.result = result;
    entry.level = "recovered";
    saveHistory();
    renderHistory();
  }
}

try { state.history = JSON.parse(localStorage.getItem(HISTORY_KEY) || "[]"); } catch (error) { state.history = []; }

function qualityFor(co2) {
  if (co2 < 800) return ["良好", "#55df91"];
  if (co2 < 1000) return ["普通", "#ffc857"];
  if (co2 < 1400) return ["偏高", "#ff9f43"];
  return ["需要通風", "#ff6b6b"];
}

function setConnection(kind, text) {
  const node = $("connection");
  node.className = `status ${kind}`;
  node.querySelector("strong").textContent = text;
  const heartbeat = $("heartbeat-card");
  heartbeat.className = `heartbeat-card ${kind}`;
  $("heartbeat-state").textContent = kind === "live" ? "LIVE" : kind === "stale" ? "STALE" : kind === "offline" ? "OFFLINE" : "WAITING";
}

function render(data) {
  state.lastRx = Date.now();
  const periodMs = Math.max(250, Number(data.publish_interval_ms) || 1000);
  $("heartbeat-card").style.setProperty("--cycle", `${periodMs}ms`);
  $("cycle-period").textContent = `${periodMs} ms`;
  const progress = $("cycle-progress");
  progress.style.animation = "none";
  void progress.offsetWidth;
  progress.style.animation = "";
  $("source").textContent = data.source === "ble" ? "真實 BLE" : "等待 BLE";
  $("ble").textContent = data.ble_scanning ? (data.ble_candidate_seen ? "已發現候選裝置" : "掃描中") : "掃描未啟動";
  $("ble-name").textContent = data.ble_name || "尚未發現";
  $("ble-address").textContent = data.ble_address || "---";
  $("ble-rssi").textContent = Number.isFinite(data.ble_rssi) && data.ble_candidate_seen ? `${data.ble_rssi} dBm` : "--- dBm";
  $("ble-uuid").textContent = data.ble_service_uuid || "---";
  $("ble-count").textContent = data.ble_packet_count ?? 0;
  $("ble-age").textContent = data.ble_candidate_seen ? `${Math.floor(data.ble_age_ms / 1000)} 秒` : "---";
  $("ble-manufacturer").textContent = data.ble_manufacturer_hex || "---";
  $("ble-service-data").textContent = data.ble_service_data_hex || "---";
  $("ble-raw").textContent = data.ble_raw_hex || "---";
  $("bridge").textContent = data.bridge_ready ? "BLE → iPhone READY" : "等待有效 BLE";
  $("wifi-clients").textContent = data.wifi_clients ?? 0;
  $("ws-clients").textContent = data.ws_clients ?? 0;
  const bleRestarts = data.ble_scan_restarts ?? 0;
  const apRestarts = data.wifi_ap_restarts ?? 0;
  $("ble-restarts").textContent = bleRestarts;
  $("ap-restarts").textContent = apRestarts;
  $("recovery").textContent = bleRestarts || apRestarts ? "RECOVERED" : "NORMAL";
  $("watchdog").textContent = data.watchdog_active ? "ACTIVE" : "INACTIVE";
  $("sta-status").textContent = data.sta_connected ? "Connected" : "Connecting";
  $("sta-ip").textContent = data.sta_ip || "---";
  $("mqtt-status").textContent = data.mqtt_connected ? "ONLINE" : "OFFLINE";
  $("mqtt-status").className = data.mqtt_connected ? "mqtt-online" : "mqtt-offline";
  $("mqtt-broker").textContent = data.mqtt_broker || "59.124.7.96:1883";
  $("mqtt-topic").textContent = data.mqtt_topic || "co2";
  $("mqtt-count").textContent = data.mqtt_publish_count ?? 0;
  $("mqtt-age").textContent = data.mqtt_publish_count ? `${Math.floor(data.mqtt_last_publish_age_ms / 1000)} 秒前` : "---";
  if (state.previous.bleScanning === true && !data.ble_scanning) addHealthEvent("BLE scanner stopped", "重啟掃描中", "failed");
  if (state.previous.bleScanning === false && data.ble_scanning) resolveHealthEvent("BLE scanner stopped", "BLE scan 已恢復");
  if (bleRestarts > (state.previous.bleRestarts ?? bleRestarts)) addHealthEvent("BLE payload stale / scan stopped", `BLE scan restart #${bleRestarts} 成功`, "recovered");
  if (apRestarts > (state.previous.apRestarts ?? apRestarts)) addHealthEvent("Wi-Fi AP health check failed", `AP/DNS restart #${apRestarts} 已執行`, "recovered");
  if (state.previous.watchdog === true && !data.watchdog_active) addHealthEvent("Main task watchdog inactive", "需要人工檢查", "failed");
  if (state.previous.staConnected === true && !data.sta_connected) addHealthEvent("Router STA disconnected", "Wi-Fi 退避重連中", "fixing");
  if (state.previous.staConnected === false && data.sta_connected) resolveHealthEvent("Router STA disconnected", "Router STA 已恢復");
  if (state.previous.mqttConnected === true && !data.mqtt_connected) addHealthEvent("MQTT broker disconnected", "MQTT 退避重連中", "fixing");
  if (state.previous.mqttConnected === false && data.mqtt_connected) resolveHealthEvent("MQTT broker disconnected", "MQTT 已恢復");
  state.previous = { bleScanning: data.ble_scanning, bleRestarts, apRestarts, watchdog: data.watchdog_active, staConnected: data.sta_connected, mqttConnected: data.mqtt_connected };
  if (!Number.isFinite(data.co2_ppm)) {
    $("co2").textContent = "---";
    $("temperature").textContent = "--.-";
    $("humidity").textContent = "--.-";
    $("battery").textContent = "---";
    $("quality").textContent = "等待真實資料";
    setConnection("connecting", state.preview ? "預覽模式" : "掃描中");
    return;
  }
  $("co2").textContent = Math.round(data.co2_ppm);
  $("temperature").textContent = Number(data.temperature_c).toFixed(1);
  $("humidity").textContent = Number(data.humidity_pct).toFixed(1);
  $("battery").textContent = data.battery_pct ?? "---";
  const [label, color] = qualityFor(data.co2_ppm);
  $("quality").textContent = label;
  $("quality").style.color = color;
  setConnection("live", state.preview ? "預覽模式" : "即時");
}

function connect() {
  if (state.preview) {
    setInterval(() => {
      render({source:"ble",co2_ppm:668,temperature_c:26.1,humidity_pct:68,battery_pct:100,ble_connected:false,ble_scanning:true,ble_candidate_seen:true,ble_name:"CO₂ Device (Preview)",ble_address:"B0:E9:FE:E2:48:FD",ble_rssi:-98,ble_service_uuid:"",ble_manufacturer_hex:"69 09 B0 E9 FE E2 48 FD 25 64 01 9A 44 00 0B 02 9C 00",ble_service_data_hex:"",ble_raw_hex:"02 01 06 13 FF 69 09 B0 E9 FE E2 48 FD 25 64 01 9A 44 00 0B 02 9C 00",ble_packet_count:5,ble_age_ms:300,sensor_data_valid:true,wifi_clients:1,ws_clients:1,bridge_ready:true,ble_scan_restarts:0,wifi_ap_restarts:0,watchdog_active:true,publish_interval_ms:1000,sta_connected:true,sta_ip:"192.168.1.80",mqtt_connected:true,mqtt_broker:"59.124.7.96:1883",mqtt_topic:"co2",mqtt_publish_count:12,mqtt_last_publish_ok:true,mqtt_last_publish_age_ms:500});
    }, 1000);
    return;
  }
  const protocol = location.protocol === "https:" ? "wss" : "ws";
  state.socket = new WebSocket(`${protocol}://${location.host}/ws`);
  state.socket.onopen = () => { state.retry = 1000; setConnection("live", "即時"); resolveHealthEvent("Dashboard WebSocket disconnected", "WebSocket 已自動重連"); };
  state.socket.onmessage = (event) => { try { render(JSON.parse(event.data)); } catch (error) { console.warn("Invalid payload", error); } };
  state.socket.onclose = () => {
    setConnection("offline", "重新連線");
    if (!state.history.some((entry) => entry.reason === "Dashboard WebSocket disconnected" && entry.level !== "recovered")) addHealthEvent("Dashboard WebSocket disconnected", "指數退避重連中", "fixing");
    clearTimeout(state.timer);
    state.timer = setTimeout(connect, state.retry);
    state.retry = Math.min(state.retry * 2, 30000);
  };
  state.socket.onerror = () => state.socket.close();
}

setInterval(() => {
  if (!state.lastRx) return;
  const age = Math.floor((Date.now() - state.lastRx) / 1000);
  $("age").textContent = `${age} 秒`;
  if (age > 10) setConnection("offline", "離線");
  else if (age > 3) setConnection("stale", "資料延遲");
}, 1000);

renderHistory();
connect();
