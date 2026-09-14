#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "secrets.h"

constexpr char kBroker[] = "59.124.7.96";
constexpr uint16_t kPort = 1883;
constexpr char kTopic[] = "co2";
constexpr uint8_t kRouterChannel = 6;
constexpr uint8_t kLed = 8;
constexpr bool kActiveLow = true;
constexpr uint32_t kPublishMs = 5000;
WiFiClient net;
PubSubClient mqtt(net);
uint32_t nextWifi = 0, nextMqtt = 0, lastPub = 0, value = 123, pulseUntil = 0;
uint32_t retryWifi = 1000, retryMqtt = 1000;
bool wifiConnecting = false;

void ledWrite(bool on) { digitalWrite(kLed, kActiveLow ? (on ? LOW : HIGH) : (on ? HIGH : LOW)); }
void ledTask(void *) {
  for (;;) {
    const bool sta = WiFi.status() == WL_CONNECTED;
    const bool mq = mqtt.connected();
    const uint32_t now = millis();
    bool on = false;
    if (!sta) on = (now % 1000U) < 500U;
    else if (!mq) { const uint32_t p = now % 3000U; on = p < 100U; }
    else if (now < pulseUntil) on = true;
    ledWrite(on);
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void serviceSta() {
  if (WiFi.status() == WL_CONNECTED) { wifiConnecting = false; retryWifi = 1000; return; }
  const uint32_t now = millis();
  if (wifiConnecting && now < nextWifi) return;
  if (wifiConnecting) { Serial.printf("[STA_TEST] timeout status=%d\n", WiFi.status()); WiFi.disconnect(false, false); wifiConnecting = false; }
  if (now < nextWifi) return;
  Serial.printf("[STA_TEST] connecting ssid=%s retry=%lu ms\n", kRouterSsid, (unsigned long)retryWifi);
  WiFi.begin(kRouterSsid, kRouterPassword, kRouterChannel); wifiConnecting = true; nextWifi = now + 15000; retryWifi = min(retryWifi * 2UL, 30000UL);
}

void serviceMqtt() {
  if (WiFi.status() != WL_CONNECTED) { if (mqtt.connected()) mqtt.disconnect(); return; }
  if (mqtt.connected()) { mqtt.loop(); retryMqtt = 1000; return; }
  const uint32_t now = millis(); if (now < nextMqtt) return;
  Serial.printf("[MQTT_TEST] connecting broker=%s:%u\n", kBroker, kPort);
  if (mqtt.connect("co2-sta-mqtt-test")) { Serial.println("[MQTT_TEST] connected"); retryMqtt = 1000; }
  else { Serial.printf("[MQTT_TEST] connect_fail state=%d\n", mqtt.state()); nextMqtt = now + retryMqtt; retryMqtt = min(retryMqtt * 2UL, 30000UL); }
}

void servicePublish() {
  if (!mqtt.connected()) return;
  const uint32_t now = millis(); if (now - lastPub < kPublishMs) return;
  char payload[32]; snprintf(payload, sizeof(payload), "{\"f\":%lu}", (unsigned long)value);
  Serial.printf("[MQTT_TEST] publish_start payload=%s\n", payload);
  const bool ok = mqtt.publish(kTopic, payload, false);
  Serial.printf("[MQTT_TEST] publish_result=%s f=%lu heap=%u\n", ok ? "OK" : "FAIL", (unsigned long)value, ESP.getFreeHeap());
  if (ok) { ++value; pulseUntil = now + 300; }
  lastPub = now;
}

void setup() {
  Serial.begin(115200); delay(300); pinMode(kLed, OUTPUT); ledWrite(false); xTaskCreate(ledTask, "sta_mqtt_led", 1536, nullptr, 1, nullptr);
  WiFi.mode(WIFI_STA); WiFi.setSleep(true); mqtt.setServer(kBroker, kPort);
  Serial.printf("\nSTA_MQTT_TEST_V1.2 firmware (BLE/AP disabled, channel=%u)\n", kRouterChannel);
}
void loop() { serviceSta(); serviceMqtt(); servicePublish(); delay(5); }
