#include "sensor_decoder.h"

DecodedSensor decodeSwitchBotMeterPro(const uint8_t *payload, size_t length) {
  DecodedSensor result{};
  if (payload == nullptr || length < 15) return result;

  const float decimal = static_cast<float>(payload[8] & 0x0F) / 10.0f;
  const float integer = static_cast<float>(payload[9] & 0x7F);
  const float sign = (payload[9] & 0x80) ? 1.0f : -1.0f;

  result.co2Ppm = static_cast<uint16_t>((payload[13] << 8) | payload[14]);
  result.temperatureC = sign * (integer + decimal);
  result.humidityPct = payload[10] & 0x7F;
  result.batteryPct = payload[7] & 0x7F;
  result.valid = result.co2Ppm <= 10000 && result.humidityPct <= 100 &&
                 result.batteryPct <= 100 && result.temperatureC >= -40.0f &&
                 result.temperatureC <= 85.0f;
  return result;
}
