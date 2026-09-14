#pragma once

#include <Arduino.h>

struct BleSnapshot {
  bool scanning;
  bool candidateSeen;
  int rssi;
  uint32_t lastSeenMs;
  uint32_t packetCount;
  uint32_t scanRestarts;
  bool decodedValid;
  uint16_t co2Ppm;
  float temperatureC;
  uint8_t humidityPct;
  uint8_t batteryPct;
  char name[40];
  char address[24];
  char serviceUuid[40];
  char manufacturerHex[160];
  char serviceDataHex[160];
  char rawPayloadHex[256];
};

bool setupBleScanner();
void pauseBleScanner();
bool resumeBleScanner();
void serviceBleScanner();
BleSnapshot getBleSnapshot();
