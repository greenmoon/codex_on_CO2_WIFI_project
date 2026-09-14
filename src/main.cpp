#include <Arduino.h>
#include <AsyncTCP.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <algorithm>

#include "ble_scanner.h"
#include "secrets.h"

namespace {
constexpr char kApSsid[] = "CO2_WIFI";
constexpr char kApPassword[] = "co2wifi123";
constexpr uint16_t kDnsPort = 53;
constexpr uint32_t kPublishIntervalMs = 1000;
constexpr uint32_t kApHealthIntervalMs = 5000;
constexpr uint32_t kMqttMinPublishMs = 1000;
constexpr uint32_t kMqttHeartbeatMs = 30000;
constexpr uint32_t kReconnectMaxMs = 30000;
constexpr char kMqttBroker[] = "59.124.7.96";
constexpr uint16_t kMqttPort = 1883;
constexpr char kMqttTopic[] = "co2";
constexpr char kFirmwareVersion[] = "1.9.0";
// ESP32-C3 DevKitM-1 onboard indicator is routed to GPIO8. Keep this as a
// simple digital BLUE status output; boards with an external blue LED can
// override it here without changing the state machine.
constexpr uint8_t kBlueLedPin = 8;
constexpr bool kBlueLedActiveLow = true;
const IPAddress kApIp(192, 168, 98, 1);
const IPAddress kApSubnet(255, 255, 255, 0);

AsyncWebServer server(80);
AsyncWebSocket websocket("/ws");
DNSServer dnsServer;
WiFiClient mqttNetwork;
PubSubClient mqttClient(mqttNetwork);
uint32_t lastPublishMs = 0;
uint32_t lastApHealthMs = 0;
uint32_t apRecoveryCount = 0;
uint8_t consecutiveApFailures = 0;
uint32_t lastStaAttemptMs = 0;
uint32_t staRetryMs = 1000;
uint32_t lastMqttAttemptMs = 0;
uint32_t mqttRetryMs = 1000;
uint32_t lastMqttPublishMs = 0;
uint32_t mqttPublishCount = 0;
uint32_t lastPublishedPacketCount = 0;
volatile uint32_t ledPublishPulseUntil = 0;
bool forceMqttPublish = false;
bool mqttLastPublishOk = false;
bool staConnecting = false;
bool peripheralsStarted = false;
void configureWebServer();
uint32_t staConnectDeadlineMs = 0;
uint32_t lastDebugMs = 0;

void debugWifiScan() {
  pauseBleScanner();
  Serial.println("[WIFI_DEBUG] scan begin");
  const int count = WiFi.scanNetworks(false, true);
  bool targetFound = false;
  for (int i = 0; i < count; ++i) {
    const String ssid = WiFi.SSID(i);
    const bool target = ssid == kRouterSsid;
    targetFound = targetFound || target;
    Serial.printf("[WIFI_DEBUG] ap=%s rssi=%d channel=%d auth=%d target=%d\n",
                  ssid.c_str(), WiFi.RSSI(i), WiFi.channel(i),
                  static_cast<int>(WiFi.encryptionType(i)), target ? 1 : 0);
  }
  Serial.printf("[WIFI_DEBUG] scan end count=%d target_found=%d\n", count, targetFound ? 1 : 0);
  WiFi.scanDelete();
  resumeBleScanner();
}

void serviceDebugLog() {
  const uint32_t now = millis();
  if (now - lastDebugMs < 5000) return;
  lastDebugMs = now;
  const BleSnapshot ble = getBleSnapshot();
  const uint32_t bleAge = ble.candidateSeen ? now - ble.lastSeenMs : 0;
  const bool router = WiFi.status() == WL_CONNECTED;
  const bool mqtt = mqttClient.connected();
  const char *led = !router ? "WIFI_SEARCH" : !mqtt ? "MQTT_SEARCH" : !mqttLastPublishOk ? "MQTT_READY" : "PUBLISH_OK";
  Serial.printf("[DEBUG_V%s] up=%lus heap=%u sta=%d connecting=%d ip=%s rssi=%d mqtt=%d mqttState=%d pub=%lu lastPubOk=%d ble=%d age=%lums seq=%lu led=%s\n",
                kFirmwareVersion,
                static_cast<unsigned long>(now / 1000), ESP.getFreeHeap(), WiFi.status(), staConnecting ? 1 : 0,
                router ? WiFi.localIP().toString().c_str() : "-", router ? WiFi.RSSI() : 0, mqtt ? 1 : 0,
                mqttClient.state(), static_cast<unsigned long>(mqttPublishCount), mqttLastPublishOk ? 1 : 0,
                ble.decodedValid ? 1 : 0, static_cast<unsigned long>(bleAge), static_cast<unsigned long>(ble.packetCount), led);
}

enum class BlueLedState : uint8_t { PowerOn, Router, Mqtt, Payload };

void serviceBlueLed() {
  const bool router = WiFi.status() == WL_CONNECTED;
  const bool mqtt = mqttClient.connected();
  const BlueLedState state = !router ? BlueLedState::PowerOn
      : !mqtt ? BlueLedState::Router
      : !mqttLastPublishOk ? BlueLedState::Mqtt : BlueLedState::Payload;
  const uint32_t now = millis();
  bool on = false;
  if (state == BlueLedState::PowerOn) {
    on = (now % 1000U) < 500U; // 1 Hz
  } else if (state == BlueLedState::Payload) {
    on = now < ledPublishPulseUntil;
  } else {
    const uint32_t phase = now % 3000U;
    const uint8_t pulses = state == BlueLedState::Router ? 1
        : state == BlueLedState::Mqtt ? 2 : 3;
    for (uint8_t i = 0; i < pulses; ++i) {
      const uint32_t start = static_cast<uint32_t>(i) * 200U;
      if (phase >= start && phase < start + 100U) on = true;
    }
  }
  digitalWrite(kBlueLedPin, kBlueLedActiveLow ? (on ? LOW : HIGH) : (on ? HIGH : LOW));
}

void ledTask(void *) {
  BlueLedState lastState = BlueLedState::PowerOn;
  bool lastOutput = false;
  for (;;) {
    const bool router = WiFi.status() == WL_CONNECTED;
    const bool mqtt = mqttClient.connected();
    const BlueLedState state = !router ? BlueLedState::PowerOn
        : !mqtt ? BlueLedState::Router
        : !mqttLastPublishOk ? BlueLedState::Mqtt : BlueLedState::Payload;
    const uint32_t now = millis();
    bool on = false;
    if (state == BlueLedState::PowerOn) {
      on = (now % 1000U) < 500U;
    } else if (state == BlueLedState::Payload) {
      on = now < ledPublishPulseUntil;
    } else {
      const uint32_t phase = now % 3000U;
      const uint8_t pulses = state == BlueLedState::Router ? 1
          : state == BlueLedState::Mqtt ? 2 : 3;
      for (uint8_t i = 0; i < pulses; ++i) {
        const uint32_t start = static_cast<uint32_t>(i) * 200U;
        if (phase >= start && phase < start + 100U) on = true;
      }
    }
    digitalWrite(kBlueLedPin, kBlueLedActiveLow ? (on ? LOW : HIGH) : (on ? HIGH : LOW));
    if (state != lastState || on != lastOutput) {
      const char *name = state == BlueLedState::PowerOn ? "WIFI_SEARCH"
          : state == BlueLedState::Router ? "MQTT_SEARCH"
          : state == BlueLedState::Mqtt ? "MQTT_READY" : "PUBLISH_OK";
      Serial.printf("[LED_TASK] state=%s output=%s\n", name, on ? "ON" : "OFF");
      lastState = state;
      lastOutput = on;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

String gatewayId() {
  String mac = WiFi.macAddress();
  mac.toLowerCase();
  mac.replace(":", "");
  return String("co2wifi-") + mac;
}

bool startAccessPoint() {
  WiFi.mode(WIFI_AP_STA);
  // ESP32-C3 requires Wi-Fi modem sleep while Wi-Fi/BLE coexistence is active.
  WiFi.setSleep(true);
  const bool configured = WiFi.softAPConfig(kApIp, kApIp, kApSubnet);
  const bool started = configured && WiFi.softAP(kApSsid, kApPassword);
  if (started) {
    dnsServer.stop();
    dnsServer.start(kDnsPort, "*", WiFi.softAPIP());
    consecutiveApFailures = 0;
  }
  return started;
}

void serviceAccessPoint() {
  const uint32_t now = millis();
  if (now - lastApHealthMs < kApHealthIntervalMs) return;
  lastApHealthMs = now;
  if ((WiFi.getMode() == WIFI_AP_STA || WiFi.getMode() == WIFI_AP) &&
      WiFi.softAPIP()[0] != 0) return;

  ++apRecoveryCount;
  ++consecutiveApFailures;
  Serial.printf("[RECOVERY] Wi-Fi AP restart #%lu\n",
                static_cast<unsigned long>(apRecoveryCount));
  WiFi.softAPdisconnect(true);
  delay(100);
  if (startAccessPoint()) {
    Serial.printf("[RECOVERY] Wi-Fi AP restored at %s\n",
                  WiFi.softAPIP().toString().c_str());
    return;
  }
  Serial.printf("[RECOVERY] Wi-Fi AP restart failed (%u/3)\n", consecutiveApFailures);
  if (consecutiveApFailures >= 3) {
    Serial.println("[RECOVERY] Controlled system restart");
    delay(100);
    ESP.restart();
  }
}

void serviceStation() {
  if (WiFi.status() == WL_CONNECTED) {
    staRetryMs = 1000;
    if (staConnecting) Serial.printf("[STA] Connected IP=%s RSSI=%d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    staConnecting = false;
    if (!peripheralsStarted) {
      Serial.println("[BOOT_FLOW] STA connected; starting AP/BLE/web services");
      if (!startAccessPoint()) {
        Serial.println("[WiFi] AP startup failed; retaining STA and retrying AP later");
        return;
      }
      if (!setupBleScanner()) Serial.println("[BLE] Scanner startup failed");
      else Serial.println("[BLE] Passive scanner started");
      configureWebServer();
      peripheralsStarted = true;
      Serial.printf("[BOOT_FLOW] AP=%s BLE=ready web=ready\n", WiFi.softAPIP().toString().c_str());
    }
    return;
  }
  const uint32_t now = millis();
  if (staConnecting) {
    if (now < staConnectDeadlineMs) return;
    Serial.printf("[STA] Connect timeout status=%d; retrying\n", WiFi.status());
    WiFi.disconnect(false, false);
    staConnecting = false;
    delay(150);
    debugWifiScan();
  }
  if (now - lastStaAttemptMs < staRetryMs) return;
  lastStaAttemptMs = now;
  Serial.printf("[STA] Connecting to %s; next retry in %lu ms\n", kRouterSsid,
                static_cast<unsigned long>(staRetryMs));
  WiFi.begin(kRouterSsid, kRouterPassword);
  staConnecting = true;
  staConnectDeadlineMs = now + 15000;
  staRetryMs = std::min(staRetryMs * 2, kReconnectMaxMs);
}

void serviceMqttConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    if (mqttClient.connected()) mqttClient.disconnect();
    return;
  }
  if (mqttClient.connected()) {
    mqttRetryMs = 1000;
    mqttClient.loop();
    return;
  }
  const uint32_t now = millis();
  if (now - lastMqttAttemptMs < mqttRetryMs) return;
  lastMqttAttemptMs = now;
  const String clientId = gatewayId();
  Serial.printf("[MQTT] Connecting to %s:%u as %s; next retry in %lu ms\n",
                kMqttBroker, kMqttPort, clientId.c_str(),
                static_cast<unsigned long>(mqttRetryMs));
  if (mqttClient.connect(clientId.c_str())) {
    Serial.println("[MQTT] Connected");
    mqttRetryMs = 1000;
    forceMqttPublish = true;
  } else {
    Serial.printf("[MQTT] Connect failed state=%d\n", mqttClient.state());
    mqttRetryMs = std::min(mqttRetryMs * 2, kReconnectMaxMs);
  }
}

bool publishMqttSnapshot(bool heartbeat) {
  if (!mqttClient.connected()) return false;
  const BleSnapshot ble = getBleSnapshot();
  if (!ble.decodedValid) return false;
  const uint32_t sensorAgeMs = millis() - ble.lastSeenMs;
  const bool fresh = sensorAgeMs <= 30000;
  char payload[512];
  snprintf(payload, sizeof(payload),
           "{\"schema\":1,\"device_id\":\"%s\",\"message_type\":\"%s\","
           "\"seq\":%lu,\"co2_ppm\":%u,\"temperature_c\":%.1f,"
           "\"humidity_pct\":%u,\"battery_pct\":%u,\"ble_rssi_dbm\":%d,"
           "\"sensor_address\":\"%s\",\"sensor_age_ms\":%lu,"
           "\"sensor_data_valid\":%s,\"uptime_ms\":%lu}",
           gatewayId().c_str(), heartbeat ? "heartbeat" : "telemetry",
           static_cast<unsigned long>(ble.packetCount), ble.co2Ppm, ble.temperatureC,
           ble.humidityPct, ble.batteryPct, ble.rssi, ble.address,
           static_cast<unsigned long>(sensorAgeMs), fresh ? "true" : "false",
           static_cast<unsigned long>(millis()));
  mqttLastPublishOk = mqttClient.publish(kMqttTopic, payload, false);
  if (mqttLastPublishOk) {
    ledPublishPulseUntil = millis() + 300U;
    lastMqttPublishMs = millis();
    lastPublishedPacketCount = ble.packetCount;
    ++mqttPublishCount;
    Serial.printf("[MQTT] Published #%lu topic=%s seq=%lu type=%s\n",
                  static_cast<unsigned long>(mqttPublishCount), kMqttTopic,
                  static_cast<unsigned long>(ble.packetCount),
                  heartbeat ? "heartbeat" : "telemetry");
  } else {
    Serial.println("[MQTT] Publish failed");
  }
  return mqttLastPublishOk;
}

void serviceMqttPublish() {
  if (!mqttClient.connected()) return;
  const uint32_t now = millis();
  const BleSnapshot ble = getBleSnapshot();
  const bool newPacket = ble.decodedValid && ble.packetCount != lastPublishedPacketCount;
  const bool minIntervalReady = now - lastMqttPublishMs >= kMqttMinPublishMs;
  const bool heartbeatDue = ble.decodedValid && now - lastMqttPublishMs >= kMqttHeartbeatMs;
  if (forceMqttPublish && ble.decodedValid) {
    forceMqttPublish = false;
    publishMqttSnapshot(false);
  } else if (newPacket && minIntervalReady) {
    publishMqttSnapshot(false);
  } else if (heartbeatDue) {
    publishMqttSnapshot(true);
  }
}

String makeSensorJson() {
  const BleSnapshot ble = getBleSnapshot();
  const uint32_t bleAgeMs = ble.candidateSeen ? millis() - ble.lastSeenMs : 0;
  const bool fresh = ble.decodedValid && bleAgeMs <= 30000;
  char co2[12] = "null";
  char temperature[16] = "null";
  char humidity[12] = "null";
  char battery[12] = "null";
  if (ble.decodedValid) {
    snprintf(co2, sizeof(co2), "%u", ble.co2Ppm);
    snprintf(temperature, sizeof(temperature), "%.1f", ble.temperatureC);
    snprintf(humidity, sizeof(humidity), "%u", ble.humidityPct);
    snprintf(battery, sizeof(battery), "%u", ble.batteryPct);
  }
  const bool staConnected = WiFi.status() == WL_CONNECTED;
  const bool mqttConnected = mqttClient.connected();
  const uint32_t mqttAgeMs = mqttPublishCount ? millis() - lastMqttPublishMs : 0;
  char payload[1400];
  snprintf(payload, sizeof(payload),
           "{\"schema\":6,\"stage\":6,\"source\":\"%s\",\"co2_ppm\":%s,"
           "\"temperature_c\":%s,\"humidity_pct\":%s,\"battery_pct\":%s,"
           "\"ble_connected\":false,\"ble_scanning\":%s,\"ble_candidate_seen\":%s,"
           "\"ble_name\":\"%s\",\"ble_address\":\"%s\",\"ble_rssi\":%d,"
           "\"ble_service_uuid\":\"%s\",\"ble_manufacturer_hex\":\"%s\","
           "\"ble_service_data_hex\":\"%s\",\"ble_raw_hex\":\"%s\","
           "\"ble_packet_count\":%lu,\"ble_age_ms\":%lu,"
           "\"sensor_data_valid\":%s,\"wifi_clients\":%u,\"ws_clients\":%u,"
           "\"bridge_ready\":%s,\"ble_scan_restarts\":%lu,"
           "\"wifi_ap_restarts\":%lu,\"watchdog_active\":true,"
           "\"publish_interval_ms\":%lu,"
           "\"sta_connected\":%s,\"sta_ip\":\"%s\","
           "\"mqtt_connected\":%s,\"mqtt_broker\":\"%s:%u\","
           "\"mqtt_topic\":\"%s\",\"mqtt_publish_count\":%lu,"
           "\"mqtt_last_publish_ok\":%s,\"mqtt_last_publish_age_ms\":%lu,"
           "\"uptime_ms\":%lu,\"firmware\":\"%s\"}",
           ble.decodedValid ? "ble" : "waiting_ble", co2, temperature, humidity, battery,
           ble.scanning ? "true" : "false",
           ble.candidateSeen ? "true" : "false", ble.name, ble.address, ble.rssi,
           ble.serviceUuid, ble.manufacturerHex, ble.serviceDataHex, ble.rawPayloadHex,
           static_cast<unsigned long>(ble.packetCount), static_cast<unsigned long>(bleAgeMs),
           fresh ? "true" : "false",
           WiFi.softAPgetStationNum(), websocket.count(), fresh ? "true" : "false",
           static_cast<unsigned long>(ble.scanRestarts),
           static_cast<unsigned long>(apRecoveryCount),
           static_cast<unsigned long>(kPublishIntervalMs),
           staConnected ? "true" : "false",
           staConnected ? WiFi.localIP().toString().c_str() : "",
           mqttConnected ? "true" : "false", kMqttBroker, kMqttPort, kMqttTopic,
           static_cast<unsigned long>(mqttPublishCount),
           mqttLastPublishOk ? "true" : "false",
           static_cast<unsigned long>(mqttAgeMs),
           static_cast<unsigned long>(millis()), kFirmwareVersion);
  return String(payload);
}

void onWebSocketEvent(AsyncWebSocket *serverSocket, AsyncWebSocketClient *client,
                      AwsEventType type, void *arg, uint8_t *data, size_t len) {
  (void)serverSocket;
  (void)arg;
  (void)data;
  (void)len;

  if (type == WS_EVT_CONNECT) {
    Serial.printf("[WS] Client #%u connected from %s\n", client->id(),
                  client->remoteIP().toString().c_str());
    client->text(makeSensorJson());
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("[WS] Client #%u disconnected\n", client->id());
  }
}

void configureWebServer() {
  websocket.onEvent(onWebSocketEvent);
  server.addHandler(&websocket);

  server.on("/health", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json",
                  "{\"ok\":true,\"stage\":6,\"source\":\"ble_mqtt\"}");
  });
  server.on("/api/sensor", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", makeSensorJson());
  });

  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  server.onNotFound([](AsyncWebServerRequest *request) {
    request->redirect(String("http://") + kApIp.toString() + "/");
  });
  server.begin();
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.printf("[BOOT_FLOW] t=%lums phase=setup_begin\n", (unsigned long)millis());
  pinMode(kBlueLedPin, OUTPUT);
  digitalWrite(kBlueLedPin, kBlueLedActiveLow ? HIGH : LOW);
  xTaskCreate(ledTask, "blue_led", 2048, nullptr, 1, nullptr);
  Serial.printf("\nCO2_WIFI Stage 6 firmware V%s\n", kFirmwareVersion);

  if (!LittleFS.begin(true)) {
    Serial.println("[FS] LittleFS mount failed; dashboard unavailable");
  }
  Serial.printf("[BOOT_FLOW] t=%lums phase=filesystem_ready\n", (unsigned long)millis());

  // STA is deliberately started before AP/BLE to avoid ESP32-C3 coexistence
  // association failures. serviceStation() starts peripherals after DHCP.
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  Serial.printf("[BOOT_FLOW] t=%lums phase=sta_only_ready\n", (unsigned long)millis());
  lastStaAttemptMs = 0;
  mqttClient.setServer(kMqttBroker, kMqttPort);
  Serial.printf("[BOOT_FLOW] t=%lums phase=mqtt_config_ready\n", (unsigned long)millis());
  mqttClient.setBufferSize(512);

  Serial.printf("[WiFi] SSID: %s (starts after STA)\n", kApSsid);
  Serial.printf("[STA] Router SSID: %s\n", kRouterSsid);
  Serial.printf("[MQTT] Broker: %s:%u topic=%s\n", kMqttBroker, kMqttPort, kMqttTopic);
  esp_task_wdt_init(15, true);
  esp_task_wdt_add(nullptr);
  Serial.println("[WATCHDOG] Main task watchdog active: 15 seconds");
}

void loop() {
  if (peripheralsStarted) {
    dnsServer.processNextRequest();
    websocket.cleanupClients();
  }
  serviceBleScanner();
  serviceAccessPoint();
  serviceStation();
  serviceMqttConnection();
  serviceMqttPublish();
  serviceDebugLog();
  esp_task_wdt_reset();

  const uint32_t now = millis();
  if (now - lastPublishMs >= kPublishIntervalMs) {
    lastPublishMs = now;
    websocket.textAll(makeSensorJson());
  }
  delay(2);
}
