#include "ble_scanner.h"
#include "sensor_decoder.h"

#include <NimBLEDevice.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

namespace {
constexpr uint16_t kScanIntervalMs = 100;
constexpr uint16_t kScanWindowMs = 60;
constexpr uint32_t kLogThrottleMs = 750;
constexpr uint32_t kTargetStaleRestartMs = 60000;
constexpr char kKnownTargetAddress[] = "b0:e9:fe:e2:48:fd";

portMUX_TYPE snapshotMux = portMUX_INITIALIZER_UNLOCKED;
BleSnapshot snapshot{};

template <size_t N>
void copyText(char (&destination)[N], const std::string &source) {
  size_t out = 0;
  for (const unsigned char ch : source) {
    if (out >= N - 1) break;
    destination[out++] = std::isprint(ch) && ch != '"' && ch != '\\' ? ch : '_';
  }
  destination[out] = '\0';
}

template <size_t N, typename Container>
void copyHex(char (&destination)[N], const Container &bytes) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  size_t out = 0;
  for (const auto value : bytes) {
    if (out + 3 >= N) break;
    if (out > 0) destination[out++] = ' ';
    const uint8_t byte = static_cast<uint8_t>(value);
    destination[out++] = kHex[byte >> 4];
    destination[out++] = kHex[byte & 0x0F];
  }
  destination[out] = '\0';
}

bool containsIgnoreCase(const std::string &text, const char *needle) {
  std::string lowerText(text);
  std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(),
                 [](unsigned char value) { return std::tolower(value); });
  std::string lowerNeedle(needle);
  std::transform(lowerNeedle.begin(), lowerNeedle.end(), lowerNeedle.begin(),
                 [](unsigned char value) { return std::tolower(value); });
  return lowerText.find(lowerNeedle) != std::string::npos;
}

bool isCandidate(const NimBLEAdvertisedDevice &device) {
  if (device.getAddress().toString() == kKnownTargetAddress) return true;
  if (device.haveName()) {
    const std::string name = device.getName();
    if (containsIgnoreCase(name, "co2") || containsIgnoreCase(name, "woth")) return true;
  }
  if (device.haveServiceUUID() && device.isAdvertisingService(NimBLEUUID("FD3D"))) return true;
  for (uint8_t index = 0; index < device.getServiceDataCount(); ++index) {
    if (device.getServiceDataUUID(index) == NimBLEUUID("FD3D")) return true;
  }
  for (uint8_t index = 0; index < device.getManufacturerDataCount(); ++index) {
    const std::string data = device.getManufacturerData(index);
    if (data.size() >= 2 && static_cast<uint8_t>(data[0]) == 0x69 &&
        static_cast<uint8_t>(data[1]) == 0x09) return true;
  }
  return false;
}

class ScanCallbacks final : public NimBLEScanCallbacks {
 public:
  void onDiscovered(const NimBLEAdvertisedDevice *device) override {
    handleDevice(device);
  }

  void onResult(const NimBLEAdvertisedDevice *device) override {
    handleDevice(device);
  }

 private:
  void handleDevice(const NimBLEAdvertisedDevice *device) {
    if (device == nullptr || !isCandidate(*device)) return;

    const uint32_t now = millis();
    const std::string address = device->getAddress().toString();
    portENTER_CRITICAL(&snapshotMux);
    const bool throttle = snapshot.candidateSeen && now - snapshot.lastSeenMs < kLogThrottleMs &&
                          std::strncmp(snapshot.address, address.c_str(), sizeof(snapshot.address)) == 0;
    portEXIT_CRITICAL(&snapshotMux);
    if (throttle) return;

    BleSnapshot next{};
    next.scanning = true;
    next.candidateSeen = true;
    next.rssi = device->getRSSI();
    next.lastSeenMs = now;
    copyText(next.name, device->haveName() ? device->getName() : std::string("(unnamed)"));
    copyText(next.address, address);
    if (device->haveServiceUUID()) copyText(next.serviceUuid, device->getServiceUUID().toString());
    if (device->getManufacturerDataCount() > 0) {
      const std::string manufacturer = device->getManufacturerData(0);
      copyHex(next.manufacturerHex, manufacturer);
      const uint8_t *bytes = reinterpret_cast<const uint8_t *>(manufacturer.data());
      size_t offset = 0;
      if (manufacturer.size() >= 2 && bytes[0] == 0x69 && bytes[1] == 0x09) offset = 2;
      const DecodedSensor decoded =
          decodeSwitchBotMeterPro(bytes + offset, manufacturer.size() - offset);
      next.decodedValid = decoded.valid;
      next.co2Ppm = decoded.co2Ppm;
      next.temperatureC = decoded.temperatureC;
      next.humidityPct = decoded.humidityPct;
      next.batteryPct = decoded.batteryPct;
    }
    if (device->getServiceDataCount() > 0) {
      copyHex(next.serviceDataHex, device->getServiceData(0));
      if (next.serviceUuid[0] == '\0') copyText(next.serviceUuid, device->getServiceDataUUID(0).toString());
    }
    copyHex(next.rawPayloadHex, device->getPayload());

    portENTER_CRITICAL(&snapshotMux);
    next.packetCount = snapshot.packetCount + 1;
    next.scanRestarts = snapshot.scanRestarts;
    snapshot = next;
    portEXIT_CRITICAL(&snapshotMux);

    Serial.printf("[BLE] #%lu name=%s address=%s RSSI=%d UUID=%s\n",
                  static_cast<unsigned long>(next.packetCount), next.name, next.address,
                  next.rssi, next.serviceUuid[0] ? next.serviceUuid : "-");
    Serial.printf("[BLE] manufacturer=%s\n", next.manufacturerHex[0] ? next.manufacturerHex : "-");
    Serial.printf("[BLE] serviceData=%s\n", next.serviceDataHex[0] ? next.serviceDataHex : "-");
    Serial.printf("[BLE] raw=%s\n", next.rawPayloadHex);
    if (next.decodedValid) {
      Serial.printf("[DECODE] CO2=%u ppm temp=%.1f C humidity=%u%% battery=%u%%\n",
                    next.co2Ppm, next.temperatureC, next.humidityPct, next.batteryPct);
    }
  }
};

ScanCallbacks scanCallbacks;
}  // namespace

bool setupBleScanner() {
  NimBLEDevice::init("");
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&scanCallbacks, true);
  scan->setActiveScan(false);
  scan->setInterval(kScanIntervalMs);
  scan->setWindow(kScanWindowMs);
  scan->setDuplicateFilter(0);
  scan->setMaxResults(0);
  const bool started = scan->start(0, false, true);
  portENTER_CRITICAL(&snapshotMux);
  snapshot.scanning = started;
  portEXIT_CRITICAL(&snapshotMux);
  return started;
}

BleSnapshot getBleSnapshot() {
  portENTER_CRITICAL(&snapshotMux);
  const BleSnapshot copy = snapshot;
  portEXIT_CRITICAL(&snapshotMux);
  return copy;
}

void pauseBleScanner() {
  NimBLEScan *scan = NimBLEDevice::getScan();
  if (scan->isScanning()) scan->stop();
  portENTER_CRITICAL(&snapshotMux);
  snapshot.scanning = false;
  portEXIT_CRITICAL(&snapshotMux);
  Serial.println("[BLE] Paused for Wi-Fi diagnostic scan");
}

bool resumeBleScanner() {
  NimBLEScan *scan = NimBLEDevice::getScan();
  const bool started = scan->start(0, false, true);
  portENTER_CRITICAL(&snapshotMux);
  snapshot.scanning = started;
  portEXIT_CRITICAL(&snapshotMux);
  Serial.printf("[BLE] Resumed after Wi-Fi diagnostic scan result=%s\n", started ? "ok" : "failed");
  return started;
}

void serviceBleScanner() {
  static uint32_t lastCheckMs = 0;
  static uint32_t lastRecoveryMs = 0;
  const uint32_t now = millis();
  if (now - lastCheckMs < 5000) return;
  lastCheckMs = now;

  NimBLEScan *scan = NimBLEDevice::getScan();
  const BleSnapshot current = getBleSnapshot();
  const bool stopped = !scan->isScanning();
  const bool targetStale = current.candidateSeen &&
                           now - current.lastSeenMs > kTargetStaleRestartMs;
  if (!stopped && !targetStale) return;
  if (!stopped && now - lastRecoveryMs < kTargetStaleRestartMs) return;

  lastRecoveryMs = now;
  scan->stop();
  const bool restarted = scan->start(0, false, true);
  portENTER_CRITICAL(&snapshotMux);
  snapshot.scanning = restarted;
  ++snapshot.scanRestarts;
  portEXIT_CRITICAL(&snapshotMux);
  Serial.printf("[RECOVERY] BLE scan restart #%lu reason=%s result=%s\n",
                static_cast<unsigned long>(current.scanRestarts + 1),
                stopped ? "stopped" : "target_stale", restarted ? "ok" : "failed");
}
