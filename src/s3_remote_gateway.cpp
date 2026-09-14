// Stage R1: ESP32-S3 local BLE gateway for the remote iPhone-only dashboard.
// Data flow: CO2 BLE advertising -> ESP32-S3 -> MQTT TCP publish -> broker WSS -> iPhone.
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <algorithm>

#include "ble_scanner.h"
#include "secrets.h"

namespace {
constexpr char kFirmwareVersion[] = "R1.1.0";
constexpr char kBroker[] = "59.124.7.98";
constexpr uint16_t kBrokerPort = 1883;
constexpr char kTopic[] = "co2";
constexpr uint8_t kRouterChannel = 6;
constexpr uint32_t kWiFiAttemptTimeoutMs = 15000;
constexpr uint32_t kReconnectMaxMs = 30000;
constexpr uint32_t kPublishIntervalMs = 5000;
constexpr uint32_t kHeartbeatIntervalMs = 30000;
constexpr uint32_t kDebugIntervalMs = 5000;
// ESP32-S3-DevKitC-1 boards with the standard addressable RGB LED use GPIO48.
// Change only this value if the connected S3 board routes its RGB LED elsewhere.
constexpr uint8_t kRgbLedPin = 48;
constexpr uint8_t kRgbLedCount = 1;

struct SensorReading {
  bool valid = false;
  uint32_t seq = 0;
  uint16_t co2Ppm = 0;
  float temperatureC = 0.0f;
  uint8_t humidityPct = 0;
  uint8_t batteryPct = 0;
  int rssi = 0;
  uint32_t seenMs = 0;
  char address[24]{};
};

enum class RgbSignal : uint8_t {
  BLE_READ,
  PAYLOAD_READY,
  MQTT_PUBLISHED,
  MQTT_FAILED,
};

WiFiClient network;
PubSubClient mqtt(network);
QueueHandle_t readingQueue = nullptr;
QueueHandle_t rgbQueue = nullptr;
Adafruit_NeoPixel rgbLed(kRgbLedCount, kRgbLedPin, NEO_GRB + NEO_KHZ800);
SensorReading latestReading{};
bool hasLatestReading = false;
uint32_t lastPublishedSeq = 0;
uint32_t lastPublishMs = 0;
uint32_t publishCount = 0;
uint32_t lastDebugMs = 0;
uint32_t lastWiFiAttemptMs = 0;
uint32_t wifiDeadlineMs = 0;
uint32_t wifiRetryMs = 1000;
uint32_t lastMqttAttemptMs = 0;
uint32_t mqttRetryMs = 1000;
bool wifiConnecting = false;

void enqueueRgb(RgbSignal signal) {
  if (rgbQueue != nullptr) xQueueSend(rgbQueue, &signal, 0);
}

void rgbTask(void *) {
  rgbLed.begin();
  rgbLed.clear();
  rgbLed.show();
  Serial.printf("[S3_RGB] start core=%d pin=%u\n", xPortGetCoreID(), kRgbLedPin);

  for (;;) {
    RgbSignal signal{};
    if (xQueueReceive(rgbQueue, &signal, portMAX_DELAY) != pdTRUE) continue;

    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;
    uint16_t holdMs = 0;
    const char *name = "UNKNOWN";
    switch (signal) {
      case RgbSignal::BLE_READ:
        blue = 32;
        holdMs = 160;
        name = "BLE_READ_BLUE";
        break;
      case RgbSignal::PAYLOAD_READY:
        green = 24;
        blue = 24;
        holdMs = 160;
        name = "PAYLOAD_CYAN";
        break;
      case RgbSignal::MQTT_PUBLISHED:
        green = 32;
        holdMs = 300;
        name = "MQTT_PUBLISHED_GREEN";
        break;
      case RgbSignal::MQTT_FAILED:
        red = 32;
        holdMs = 600;
        name = "MQTT_FAILED_RED";
        break;
    }
    rgbLed.setPixelColor(0, rgbLed.Color(red, green, blue));
    rgbLed.show();
    Serial.printf("[S3_RGB] %s hold=%ums\n", name, holdMs);
    vTaskDelay(pdMS_TO_TICKS(holdMs));
    rgbLed.clear();
    rgbLed.show();
  }
}

String gatewayId() {
  String mac = WiFi.macAddress();
  mac.toLowerCase();
  mac.replace(":", "");
  return String("co2s3-remote-") + mac;
}

void serviceWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    if (wifiConnecting) {
      Serial.printf("[S3_STA] connected ip=%s rssi=%d\n",
                    WiFi.localIP().toString().c_str(), WiFi.RSSI());
    }
    wifiConnecting = false;
    wifiRetryMs = 1000;
    return;
  }

  const uint32_t now = millis();
  if (wifiConnecting && now >= wifiDeadlineMs) {
    Serial.printf("[S3_STA] timeout status=%d\n", WiFi.status());
    WiFi.disconnect(false, false);
    wifiConnecting = false;
  }
  if (wifiConnecting || now - lastWiFiAttemptMs < wifiRetryMs) return;

  lastWiFiAttemptMs = now;
  wifiDeadlineMs = now + kWiFiAttemptTimeoutMs;
  wifiConnecting = true;
  Serial.printf("[S3_STA] connect ssid=%s channel=%u retry=%lu ms\n", kRouterSsid,
                kRouterChannel, static_cast<unsigned long>(wifiRetryMs));
  WiFi.begin(kRouterSsid, kRouterPassword, kRouterChannel);
  wifiRetryMs = std::min(wifiRetryMs * 2, kReconnectMaxMs);
}

void serviceMqtt() {
  if (WiFi.status() != WL_CONNECTED) {
    if (mqtt.connected()) mqtt.disconnect();
    return;
  }
  if (mqtt.connected()) {
    mqtt.loop();
    mqttRetryMs = 1000;
    return;
  }

  const uint32_t now = millis();
  if (now - lastMqttAttemptMs < mqttRetryMs) return;
  lastMqttAttemptMs = now;
  const String id = gatewayId();
  Serial.printf("[S3_MQTT] connect broker=%s:%u client=%s retry=%lu ms\n", kBroker,
                kBrokerPort, id.c_str(), static_cast<unsigned long>(mqttRetryMs));
  if (mqtt.connect(id.c_str())) {
    Serial.println("[S3_MQTT] connected");
    mqttRetryMs = 1000;
  } else {
    Serial.printf("[S3_MQTT] failed state=%d\n", mqtt.state());
    mqttRetryMs = std::min(mqttRetryMs * 2, kReconnectMaxMs);
  }
}

bool publishReading(const SensorReading &reading, bool heartbeat) {
  if (!mqtt.connected() || !reading.valid) return false;
  const uint32_t ageMs = millis() - reading.seenMs;
  char payload[384];
  snprintf(payload, sizeof(payload),
           "{\"schema\":1,\"device_id\":\"%s\",\"message_type\":\"%s\","
           "\"seq\":%lu,\"co2_ppm\":%u,\"temperature_c\":%.1f,"
           "\"humidity_pct\":%u,\"battery_pct\":%u,\"ble_rssi_dbm\":%d,"
           "\"sensor_address\":\"%s\",\"sensor_age_ms\":%lu,"
           "\"sensor_data_valid\":true,\"uptime_ms\":%lu}",
           gatewayId().c_str(), heartbeat ? "heartbeat" : "telemetry",
           static_cast<unsigned long>(reading.seq), reading.co2Ppm, reading.temperatureC,
           reading.humidityPct, reading.batteryPct, reading.rssi, reading.address,
           static_cast<unsigned long>(ageMs), static_cast<unsigned long>(millis()));
  enqueueRgb(RgbSignal::PAYLOAD_READY);
  Serial.printf("[S3_PAYLOAD] packed seq=%lu bytes=%u\n",
                static_cast<unsigned long>(reading.seq), strlen(payload));
  const bool ok = mqtt.publish(kTopic, payload, false);
  if (ok) {
    lastPublishMs = millis();
    lastPublishedSeq = reading.seq;
    ++publishCount;
    Serial.printf("[S3_MQTT] published #%lu topic=%s seq=%lu type=%s\n",
                  static_cast<unsigned long>(publishCount), kTopic,
                  static_cast<unsigned long>(reading.seq), heartbeat ? "heartbeat" : "telemetry");
    enqueueRgb(RgbSignal::MQTT_PUBLISHED);
  } else {
    Serial.println("[S3_MQTT] publish failed");
    enqueueRgb(RgbSignal::MQTT_FAILED);
  }
  return ok;
}

void bleTask(void *) {
  Serial.printf("[S3_CORE1_BLE] start core=%d\n", xPortGetCoreID());
  if (!setupBleScanner()) {
    Serial.println("[S3_CORE1_BLE] scanner start failed");
  }
  uint32_t queuedSeq = 0;
  for (;;) {
    serviceBleScanner();
    const BleSnapshot snapshot = getBleSnapshot();
    if (snapshot.decodedValid && snapshot.packetCount != queuedSeq) {
      SensorReading reading{};
      reading.valid = true;
      reading.seq = snapshot.packetCount;
      reading.co2Ppm = snapshot.co2Ppm;
      reading.temperatureC = snapshot.temperatureC;
      reading.humidityPct = snapshot.humidityPct;
      reading.batteryPct = snapshot.batteryPct;
      reading.rssi = snapshot.rssi;
      reading.seenMs = snapshot.lastSeenMs;
      strlcpy(reading.address, snapshot.address, sizeof(reading.address));
      xQueueOverwrite(readingQueue, &reading);
      enqueueRgb(RgbSignal::BLE_READ);
      queuedSeq = snapshot.packetCount;
      Serial.printf("[S3_CORE1_BLE] queue seq=%lu co2=%u\n",
                    static_cast<unsigned long>(reading.seq), reading.co2Ppm);
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void gatewayTask(void *) {
  Serial.printf("[S3_CORE0_GATEWAY] start core=%d\n", xPortGetCoreID());
  for (;;) {
    serviceWiFi();
    serviceMqtt();

    SensorReading incoming{};
    if (xQueueReceive(readingQueue, &incoming, 0) == pdTRUE) {
      latestReading = incoming;
      hasLatestReading = true;
    }

    const uint32_t now = millis();
    if (hasLatestReading && mqtt.connected()) {
      const bool firstPublish = publishCount == 0;
      const bool nextReadingDue = latestReading.seq != lastPublishedSeq &&
                                  now - lastPublishMs >= kPublishIntervalMs;
      const bool heartbeatDue = now - lastPublishMs >= kHeartbeatIntervalMs;
      if (firstPublish || nextReadingDue) {
        publishReading(latestReading, false);
      } else if (heartbeatDue) {
        publishReading(latestReading, true);
      }
    }

    if (now - lastDebugMs >= kDebugIntervalMs) {
      lastDebugMs = now;
      const BleSnapshot ble = getBleSnapshot();
      Serial.printf("[S3_DEBUG] up=%lus core=%d heap=%u sta=%d mqtt=%d pub=%lu ble=%d seq=%lu age=%lums\n",
                    static_cast<unsigned long>(now / 1000), xPortGetCoreID(), ESP.getFreeHeap(),
                    WiFi.status(), mqtt.connected() ? 1 : 0,
                    static_cast<unsigned long>(publishCount), ble.decodedValid ? 1 : 0,
                    static_cast<unsigned long>(ble.packetCount),
                    static_cast<unsigned long>(ble.candidateSeen ? now - ble.lastSeenMs : 0));
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.printf("\nCO2_S3 Remote Gateway V%s\n", kFirmwareVersion);
  Serial.println("[S3_BOOT] BLE core=1, Wi-Fi/MQTT core=0; RF is Wi-Fi/BLE time-shared");
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  WiFi.setAutoReconnect(false);
  mqtt.setServer(kBroker, kBrokerPort);
  mqtt.setBufferSize(512);
  readingQueue = xQueueCreate(1, sizeof(SensorReading));
  rgbQueue = xQueueCreate(8, sizeof(RgbSignal));
  if (readingQueue == nullptr || rgbQueue == nullptr) {
    Serial.println("[S3_BOOT] queue allocation failed; restarting");
    delay(100);
    ESP.restart();
  }
  xTaskCreatePinnedToCore(rgbTask, "rgb_status", 3072, nullptr, 1, nullptr, 0);
  xTaskCreatePinnedToCore(bleTask, "ble_decode", 6144, nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(gatewayTask, "gateway_mqtt", 6144, nullptr, 2, nullptr, 0);
}

void loop() {
  // All application work is in the two pinned tasks.
  vTaskDelay(pdMS_TO_TICKS(1000));
}
