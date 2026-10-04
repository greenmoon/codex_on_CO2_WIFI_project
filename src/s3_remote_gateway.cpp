// Stage R1: ESP32-S3 local BLE gateway for the remote iPhone-only dashboard.
// Data flow: CO2 BLE advertising -> ESP32-S3 -> MQTT TCP publish -> broker WSS -> iPhone.
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#ifndef CO2_ENABLE_OTA
#define CO2_ENABLE_OTA 0
#endif
#if CO2_ENABLE_OTA
#include <ArduinoOTA.h>
#endif
#include <PubSubClient.h>
#include <WiFi.h>
#include <algorithm>

#include "ble_scanner.h"
#include "secrets.h"

namespace {
constexpr char kFirmwareVersion[] = "R1.3.2";
constexpr char kBroker[] = "59.124.7.98";
constexpr uint16_t kBrokerPort = 1883;
constexpr uint32_t kWiFiAttemptTimeoutMs = 15000;
constexpr uint32_t kReconnectMaxMs = 30000;
constexpr uint32_t kBleInitialWifiWaitMs = 45000;
constexpr uint32_t kPublishIntervalMs = 10000;
constexpr uint32_t kSensorFreshMaxMs = 15000;
constexpr uint32_t kDebugIntervalMs = 5000;
constexpr uint8_t kReconnectFailuresBeforeSwitch = 3;
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

struct WifiProfile {
  const char *alias;
  const char *ssid;
  const char *password;
};

constexpr WifiProfile kWifiProfiles[] = {
    {"HOME", kS3HomeRouterSsid, kS3HomeRouterPassword},
    {"IPHONE", kS3IphoneRouterSsid, kS3IphoneRouterPassword},
};
constexpr size_t kWifiProfileCount = sizeof(kWifiProfiles) / sizeof(kWifiProfiles[0]);

enum class RgbSignal : uint8_t {
  BLE_READ,
  PAYLOAD_READY,
  MQTT_PUBLISHED,
  MQTT_FAILED,
#if CO2_ENABLE_OTA
  OTA_READY,
  OTA_FAILED,
#endif
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
bool wifiWasConnected = false;
bool wifiHasEverConnected = false;
uint8_t wifiProfileFailures = 0;
size_t activeWifiProfileIndex = 0;
size_t connectedWifiProfileIndex = 0;
String mqttTopic;
#if CO2_ENABLE_OTA
volatile bool otaInProgress = false;
bool otaReady = false;
String otaHostname;
static_assert(sizeof(kOtaPassword) >= 13,
              "kOtaPassword must contain at least 12 characters");
#else
constexpr bool otaInProgress = false;
#endif

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
    if (xQueueReceive(rgbQueue, &signal, pdMS_TO_TICKS(30)) != pdTRUE) {
#if CO2_ENABLE_OTA
      if (otaInProgress) {
      static uint8_t otaLevel = 2;
      static int8_t otaStep = 2;
      otaLevel = static_cast<uint8_t>(otaLevel + otaStep);
      if (otaLevel >= 30) otaStep = -2;
      if (otaLevel <= 2) otaStep = 2;
      rgbLed.setPixelColor(0, rgbLed.Color(otaLevel, 0, otaLevel));
      rgbLed.show();
      }
#endif
      continue;
    }

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
#if CO2_ENABLE_OTA
      case RgbSignal::OTA_READY:
        red = 24;
        blue = 24;
        holdMs = 250;
        name = "OTA_READY_PURPLE";
        break;
      case RgbSignal::OTA_FAILED:
        red = 40;
        holdMs = 900;
        name = "OTA_FAILED_RED";
        break;
#endif
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

const WifiProfile &activeWifiProfile() {
  return kWifiProfiles[activeWifiProfileIndex];
}

const char *connectedWifiAlias() {
  return kWifiProfiles[connectedWifiProfileIndex].alias;
}

void switchWifiProfile(const char *reason) {
  const WifiProfile &previous = activeWifiProfile();
  activeWifiProfileIndex = (activeWifiProfileIndex + 1) % kWifiProfileCount;
  wifiProfileFailures = 0;
  wifiRetryMs = 1000;
  lastWiFiAttemptMs = 0;
  Serial.printf("[S3_WIFI_FAILOVER] %s -> %s reason=%s\n", previous.alias,
                activeWifiProfile().alias, reason);
}

#if CO2_ENABLE_OTA
void setupOta() {
  otaHostname = gatewayId();
  ArduinoOTA.setHostname(otaHostname.c_str());
  ArduinoOTA.setPassword(kOtaPassword);
  ArduinoOTA.onStart([]() {
    otaInProgress = true;
    if (mqtt.connected()) mqtt.disconnect();
    pauseBleScanner();
    Serial.printf("[S3_OTA] start type=%s\n",
                  ArduinoOTA.getCommand() == U_FLASH ? "firmware" : "filesystem");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    static uint8_t lastReportedPct = 255;
    const uint8_t pct = total == 0 ? 0 : static_cast<uint8_t>((progress * 100U) / total);
    if (pct == 100 || lastReportedPct == 255 || pct >= lastReportedPct + 10) {
      lastReportedPct = pct;
      Serial.printf("[S3_OTA] progress=%u%%\n", pct);
    }
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("[S3_OTA] end rebooting");
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[S3_OTA] error=%u\n", error);
    otaInProgress = false;
    enqueueRgb(RgbSignal::OTA_FAILED);
    if (!resumeBleScanner()) {
      Serial.println("[S3_OTA] BLE resume failed; recovery task will retry");
    }
  });
  ArduinoOTA.begin();
  otaReady = true;
  enqueueRgb(RgbSignal::OTA_READY);
  Serial.printf("[S3_OTA] ready host=%s.local ip=%s auth=enabled sketch=%u free_ota=%u\n",
                otaHostname.c_str(), WiFi.localIP().toString().c_str(), ESP.getSketchSize(),
                ESP.getFreeSketchSpace());
}

void stopOtaForWifiChange() {
  if (!otaReady || otaInProgress) return;
  ArduinoOTA.end();
  otaReady = false;
  Serial.println("[S3_OTA] stopped reason=wifi_disconnected");
}

void serviceOta() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (!otaReady) setupOta();
  ArduinoOTA.handle();
}
#else
void serviceOta() {}
void stopOtaForWifiChange() {}
#endif

void serviceWiFi() {
  const uint32_t now = millis();
  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiWasConnected) {
      connectedWifiProfileIndex = activeWifiProfileIndex;
      Serial.printf("[S3_STA] connected profile=%s ip=%s rssi=%d\n",
                    connectedWifiAlias(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
    }
    wifiWasConnected = true;
    wifiHasEverConnected = true;
    wifiConnecting = false;
    wifiProfileFailures = 0;
    wifiRetryMs = 1000;
    return;
  }

  if (wifiWasConnected) {
    Serial.printf("[S3_STA] disconnected profile=%s status=%d\n", connectedWifiAlias(),
                  WiFi.status());
    stopOtaForWifiChange();
    wifiWasConnected = false;
    wifiConnecting = false;
    wifiProfileFailures = 0;
    wifiRetryMs = 1000;
    lastWiFiAttemptMs = 0;
  }

  if (wifiConnecting && static_cast<int32_t>(now - wifiDeadlineMs) >= 0) {
    ++wifiProfileFailures;
    Serial.printf("[S3_STA] timeout profile=%s status=%d failure=%u\n",
                  activeWifiProfile().alias, WiFi.status(), wifiProfileFailures);
    WiFi.disconnect(false, false);
    wifiConnecting = false;
    const uint8_t failureLimit = wifiHasEverConnected ? kReconnectFailuresBeforeSwitch : 1;
    if (wifiProfileFailures >= failureLimit) {
      switchWifiProfile("connect_timeout");
    }
    return;
  }
  if (wifiConnecting || now - lastWiFiAttemptMs < wifiRetryMs) return;

  const WifiProfile &profile = activeWifiProfile();
  lastWiFiAttemptMs = now;
  wifiDeadlineMs = now + kWiFiAttemptTimeoutMs;
  wifiConnecting = true;
  Serial.printf("[S3_STA] connect profile=%s channel=auto retry=%lu ms\n", profile.alias,
                static_cast<unsigned long>(wifiRetryMs));
  WiFi.begin(profile.ssid, profile.password);
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

bool publishReading(const SensorReading &reading) {
  if (!mqtt.connected() || !reading.valid) return false;
  const uint32_t ageMs = millis() - reading.seenMs;
  char payload[448];
  const int written = snprintf(payload, sizeof(payload),
                               "{\"schema\":2,\"firmware_version\":\"%s\","
                               "\"device_id\":\"%s\",\"message_type\":\"telemetry\","
                               "\"seq\":%lu,\"co2_ppm\":%u,\"temperature_c\":%.1f,"
                               "\"humidity_pct\":%u,\"battery_pct\":%u,"
                               "\"ble_rssi_dbm\":%d,\"sensor_address\":\"%s\","
                               "\"sensor_age_ms\":%lu,\"sensor_data_valid\":true,"
                               "\"wifi_profile\":\"%s\",\"wifi_rssi_dbm\":%d,"
                               "\"publish_interval_ms\":%lu,\"uptime_ms\":%lu}",
                               kFirmwareVersion, gatewayId().c_str(),
                               static_cast<unsigned long>(reading.seq), reading.co2Ppm,
                               reading.temperatureC, reading.humidityPct, reading.batteryPct,
                               reading.rssi, reading.address, static_cast<unsigned long>(ageMs),
                               connectedWifiAlias(), WiFi.RSSI(),
                               static_cast<unsigned long>(kPublishIntervalMs),
                               static_cast<unsigned long>(millis()));
  if (written < 0 || static_cast<size_t>(written) >= sizeof(payload)) {
    Serial.printf("[S3_PAYLOAD] overflow required=%d capacity=%u\n", written,
                  static_cast<unsigned>(sizeof(payload)));
    enqueueRgb(RgbSignal::MQTT_FAILED);
    return false;
  }
  enqueueRgb(RgbSignal::PAYLOAD_READY);
  Serial.printf("[S3_PAYLOAD] packed seq=%lu bytes=%u\n",
                static_cast<unsigned long>(reading.seq), strlen(payload));
  const bool ok = mqtt.publish(mqttTopic.c_str(), payload, false);
  if (ok) {
    lastPublishMs = millis();
    lastPublishedSeq = reading.seq;
    ++publishCount;
    Serial.printf("[S3_MQTT] published #%lu topic=%s seq=%lu type=telemetry age=%lums\n",
                  static_cast<unsigned long>(publishCount), mqttTopic.c_str(),
                  static_cast<unsigned long>(reading.seq), static_cast<unsigned long>(ageMs));
    enqueueRgb(RgbSignal::MQTT_PUBLISHED);
  } else {
    Serial.println("[S3_MQTT] publish failed");
    enqueueRgb(RgbSignal::MQTT_FAILED);
  }
  return ok;
}

void bleTask(void *) {
  Serial.printf("[S3_CORE1_BLE] start core=%d\n", xPortGetCoreID());
  const uint32_t wifiWaitStartedMs = millis();
  uint32_t lastWaitLogMs = 0;
  while (WiFi.status() != WL_CONNECTED &&
         millis() - wifiWaitStartedMs < kBleInitialWifiWaitMs) {
    const uint32_t now = millis();
    if (lastWaitLogMs == 0 || now - lastWaitLogMs >= 5000) {
      lastWaitLogMs = now;
      Serial.printf("[S3_CORE1_BLE] waiting for initial STA status=%d elapsed=%lus\n",
                    WiFi.status(), static_cast<unsigned long>((now - wifiWaitStartedMs) / 1000));
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  Serial.printf("[S3_CORE1_BLE] initial STA gate=%s; starting scanner\n",
                WiFi.status() == WL_CONNECTED ? "connected" : "timeout_fallback");
  if (!setupBleScanner()) {
    Serial.println("[S3_CORE1_BLE] scanner start failed");
  }
  uint32_t queuedSeq = 0;
  for (;;) {
    if (otaInProgress) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
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
    serviceOta();
    if (otaInProgress) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    serviceMqtt();

    SensorReading incoming{};
    if (xQueueReceive(readingQueue, &incoming, 0) == pdTRUE) {
      latestReading = incoming;
      hasLatestReading = true;
    }

    const uint32_t now = millis();
    if (hasLatestReading && mqtt.connected()) {
      const uint32_t readingAgeMs = now - latestReading.seenMs;
      const bool readingFresh = readingAgeMs <= kSensorFreshMaxMs;
      const bool firstPublish = publishCount == 0;
      const bool nextReadingDue = latestReading.seq != lastPublishedSeq &&
                                  now - lastPublishMs >= kPublishIntervalMs;
      if (readingFresh && (firstPublish || nextReadingDue)) {
        publishReading(latestReading);
      }
    }

    if (now - lastDebugMs >= kDebugIntervalMs) {
      lastDebugMs = now;
      const BleSnapshot ble = getBleSnapshot();
      const bool wifiConnected = WiFi.status() == WL_CONNECTED;
      Serial.printf("[S3_DEBUG] up=%lus core=%d heap=%u sta=%d profile=%s wifi_rssi=%d "
                    "mqtt=%d pub=%lu ble=%d seq=%lu age=%lums\n",
                    static_cast<unsigned long>(now / 1000), xPortGetCoreID(), ESP.getFreeHeap(),
                    WiFi.status(), wifiConnected ? connectedWifiAlias() : activeWifiProfile().alias,
                    wifiConnected ? WiFi.RSSI() : 0, mqtt.connected() ? 1 : 0,
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
  Serial.printf("[S3_BOOT] BLE core=1, Wi-Fi/MQTT core=0, OTA=%s; RF is time-shared\n",
                CO2_ENABLE_OTA ? "enabled" : "disabled");
  Serial.printf("[S3_BOOT] flash=%uMB psram=%uMB image_md5=%s\n",
                ESP.getFlashChipSize() / (1024U * 1024U),
                ESP.getPsramSize() / (1024U * 1024U), ESP.getSketchMD5().c_str());
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  WiFi.setAutoReconnect(false);
  Serial.printf("[S3_BOOT] Wi-Fi profiles priority=%s -> %s timeout=%lums reconnect_failures=%u\n",
                kWifiProfiles[0].alias, kWifiProfiles[1].alias,
                static_cast<unsigned long>(kWiFiAttemptTimeoutMs),
                kReconnectFailuresBeforeSwitch);
  mqttTopic = String("co2/") + gatewayId() + "/telemetry";
  mqtt.setServer(kBroker, kBrokerPort);
  mqtt.setBufferSize(512);
  Serial.printf("[S3_BOOT] publish_interval=%lums fresh_max=%lums topic=%s\n",
                static_cast<unsigned long>(kPublishIntervalMs),
                static_cast<unsigned long>(kSensorFreshMaxMs), mqttTopic.c_str());
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
