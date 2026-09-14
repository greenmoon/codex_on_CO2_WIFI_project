#include "sensor_decoder.h"

#include <cassert>
#include <cmath>
#include <cstdint>

int main() {
  const uint8_t payload[] = {
      0xB0, 0xE9, 0xFE, 0xE2, 0x48, 0xFD, 0x25, 0x64,
      0x01, 0x9A, 0x44, 0x00, 0x0B, 0x02, 0x9C,
  };
  const DecodedSensor decoded = decodeSwitchBotMeterPro(payload, sizeof(payload));
  assert(decoded.valid);
  assert(decoded.co2Ppm == 668);
  assert(std::fabs(decoded.temperatureC - 26.1f) < 0.01f);
  assert(decoded.humidityPct == 68);
  assert(decoded.batteryPct == 100);

  const DecodedSensor tooShort = decodeSwitchBotMeterPro(payload, 14);
  assert(!tooShort.valid);
  return 0;
}
