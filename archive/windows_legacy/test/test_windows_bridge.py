import unittest
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from windows_bridge import normalize_mqtt_payload


class WindowsBridgePayloadTest(unittest.TestCase):
    expected = {"co2_ppm": 668, "temperature_c": 26.1, "humidity_pct": 68, "battery_pct": 100}

    def test_decoded_gateway_json(self):
        payload = normalize_mqtt_payload(b'{"co2_ppm":668,"temperature_c":26.1,"humidity_pct":68,"battery_pct":100}')
        self.assertEqual({key: payload[key] for key in self.expected}, self.expected)

    def test_raw_manufacturer_json(self):
        payload = normalize_mqtt_payload(b'{"manufacturer_hex":"69 09 B0 E9 FE E2 48 FD 25 64 01 9A 44 00 0B 02 9C 00"}')
        self.assertEqual({key: payload[key] for key in self.expected}, self.expected)


if __name__ == "__main__":
    unittest.main()
