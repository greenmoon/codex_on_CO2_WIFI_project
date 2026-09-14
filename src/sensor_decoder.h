#pragma once

#include <cstddef>
#include <cstdint>

struct DecodedSensor {
  bool valid;
  uint16_t co2Ppm;
  float temperatureC;
  uint8_t humidityPct;
  uint8_t batteryPct;
};

DecodedSensor decodeSwitchBotMeterPro(const uint8_t *payload, size_t length);

