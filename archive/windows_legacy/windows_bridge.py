"""IDEA1: MQTT payload -> Windows Bridge -> remote iPhone Dashboard.

Example:
  python windows_bridge.py --mqtt-host 192.168.1.20 --mqtt-topic co2/gateway/sensor
"""
from __future__ import annotations

import argparse
import asyncio
import json
import logging
import os
import pathlib
import time
from dataclasses import dataclass, field
from typing import Any

import paho.mqtt.client as mqtt

ROOT = pathlib.Path(__file__).resolve().parent
WINDOWS_DASHBOARD = ROOT / "windows_bridge" / "dashboard"
DEFAULT_PORT = 8080
PUBLISH_INTERVAL = 1.0
DEFAULT_MQTT_HOST = "59.124.7.96"
DEFAULT_MQTT_TOPIC = "co2"


def decode_sensor(manufacturer: bytes) -> dict[str, Any] | None:
    """Decode Company ID 0x0969 plus the verified 15-byte sensor payload."""
    payload = manufacturer[2:] if len(manufacturer) >= 17 and manufacturer[:2] == b"\x69\x09" else manufacturer
    if len(payload) < 15:
        return None
    temperature = (payload[9] & 0x7F) + (payload[8] & 0x0F) / 10
    if not payload[9] & 0x80:
        temperature = -temperature
    decoded = {"co2_ppm": (payload[13] << 8) | payload[14], "temperature_c": round(temperature, 1),
               "humidity_pct": payload[10] & 0x7F, "battery_pct": payload[7] & 0x7F}
    return decoded if decoded["co2_ppm"] <= 10000 and decoded["humidity_pct"] <= 100 else None


def as_number(value: Any) -> int | float | None:
    if isinstance(value, bool) or value is None:
        return None
    if isinstance(value, (int, float)):
        return value
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def normalize_mqtt_payload(message: bytes) -> dict[str, Any] | None:
    """Accept gateway schema 5, flat sensor JSON, or raw manufacturer hex."""
    try:
        raw = json.loads(message.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        return None
    if not isinstance(raw, dict):
        return None
    raw = raw.get("payload", raw)
    if not isinstance(raw, dict):
        return None
    decoded = {key: as_number(raw.get(key)) for key in ("co2_ppm", "temperature_c", "humidity_pct", "battery_pct")}
    if decoded["co2_ppm"] is None:
        hex_value = raw.get("ble_manufacturer_hex") or raw.get("manufacturer_hex") or raw.get("manufacturer_data")
        if isinstance(hex_value, str):
            try:
                decoded = decode_sensor(bytes.fromhex(hex_value.replace("0x", "").replace(",", " "))) or decoded
            except ValueError:
                pass
    if decoded["co2_ppm"] is None:
        return None
    return {
        **decoded,
        "name": str(raw.get("ble_name") or raw.get("name") or "CO₂ Gateway"),
        "address": str(raw.get("ble_address") or raw.get("address") or ""),
        "rssi": as_number(raw.get("ble_rssi") or raw.get("rssi")),
        "manufacturer_hex": str(raw.get("ble_manufacturer_hex") or raw.get("manufacturer_hex") or ""),
        "raw_hex": str(raw.get("ble_raw_hex") or raw.get("raw_hex") or ""),
        "packet_count": as_number(raw.get("ble_packet_count") or raw.get("packet_count")),
    }


@dataclass
class Snapshot:
    network_name: str
    started: float = field(default_factory=time.monotonic)
    last_seen: float = 0
    mqtt_connected: bool = False
    mqtt_has_connected: bool = False
    mqtt_messages: int = 0
    mqtt_reconnects: int = 0
    mqtt_error: str = "Waiting for MQTT broker configuration"
    topic: str = ""
    reading: dict[str, Any] | None = None
    subscribers: set[asyncio.Queue[str]] = field(default_factory=set)

    def payload(self) -> dict[str, Any]:
        age = int((time.monotonic() - self.last_seen) * 1000) if self.last_seen else 0
        reading = self.reading or {}
        fresh = bool(reading) and age <= 30000
        return {
            "schema": 5, "stage": 1, "source": "mqtt" if reading else "waiting_mqtt",
            "co2_ppm": reading.get("co2_ppm"), "temperature_c": reading.get("temperature_c"),
            "humidity_pct": reading.get("humidity_pct"), "battery_pct": reading.get("battery_pct"),
            "ble_connected": False, "ble_scanning": self.mqtt_connected,
            "ble_candidate_seen": bool(reading), "ble_name": reading.get("name", ""),
            "ble_address": reading.get("address", ""), "ble_rssi": reading.get("rssi"),
            "ble_service_uuid": "", "ble_manufacturer_hex": reading.get("manufacturer_hex", ""),
            "ble_service_data_hex": "", "ble_raw_hex": reading.get("raw_hex", ""),
            "ble_packet_count": reading.get("packet_count") or self.mqtt_messages, "ble_age_ms": age,
            "sensor_data_valid": fresh, "wifi_clients": 0, "ws_clients": len(self.subscribers),
            "bridge_ready": fresh, "ble_scan_restarts": self.mqtt_reconnects,
            "wifi_ap_restarts": 0, "watchdog_active": True, "publish_interval_ms": 1000,
            "uptime_ms": int((time.monotonic() - self.started) * 1000),
            "firmware": "windows-mqtt-bridge-idea1", "bridge_mode": "windows-mqtt",
            "bridge_label": "MQTT → iPhone READY" if fresh else "等待 MQTT Payload",
            "network_name": self.network_name, "mqtt_connected": self.mqtt_connected,
            "mqtt_messages": self.mqtt_messages, "mqtt_topic": self.topic, "mqtt_error": self.mqtt_error,
        }

    def ingest(self, reading: dict[str, Any]) -> None:
        self.reading = reading
        self.last_seen = time.monotonic()
        self.mqtt_messages += 1
        message = json.dumps(self.payload(), ensure_ascii=False)
        for queue in list(self.subscribers):
            queue.put_nowait(message)


class MqttSubscriber:
    def __init__(self, snapshot: Snapshot, loop: asyncio.AbstractEventLoop, args: argparse.Namespace) -> None:
        self.snapshot, self.loop, self.args = snapshot, loop, args
        self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=args.mqtt_client_id)
        if args.mqtt_username:
            self.client.username_pw_set(args.mqtt_username, args.mqtt_password)
        self.client.on_connect = self.on_connect
        self.client.on_disconnect = self.on_disconnect
        self.client.on_message = self.on_message
        self.client.reconnect_delay_set(min_delay=1, max_delay=30)

    def start(self) -> None:
        if not self.args.mqtt_host:
            logging.warning("MQTT is not configured; use --mqtt-host and --mqtt-topic")
            return
        self.snapshot.topic = self.args.mqtt_topic
        self.client.connect_async(self.args.mqtt_host, self.args.mqtt_port, keepalive=30)
        self.client.loop_start()

    def on_connect(self, client: mqtt.Client, userdata: Any, flags: Any, reason_code: Any, properties: Any = None) -> None:
        if reason_code.is_failure:
            self.loop.call_soon_threadsafe(self.set_error, f"MQTT connect rejected: {reason_code}")
            return
        client.subscribe(self.args.mqtt_topic, qos=0)
        self.loop.call_soon_threadsafe(self.set_connected, True)

    def on_disconnect(self, client: mqtt.Client, userdata: Any, disconnect_flags: Any, reason_code: Any, properties: Any = None) -> None:
        self.loop.call_soon_threadsafe(self.set_connected, False)

    def on_message(self, client: mqtt.Client, userdata: Any, message: mqtt.MQTTMessage) -> None:
        reading = normalize_mqtt_payload(message.payload)
        if reading is None:
            self.loop.call_soon_threadsafe(self.set_error, "Ignored invalid MQTT sensor payload")
            return
        self.loop.call_soon_threadsafe(self.snapshot.ingest, reading)

    def set_connected(self, connected: bool) -> None:
        if connected and self.snapshot.mqtt_has_connected and not self.snapshot.mqtt_connected:
            self.snapshot.mqtt_reconnects += 1
        if connected:
            self.snapshot.mqtt_has_connected = True
        self.snapshot.mqtt_connected = connected
        self.snapshot.mqtt_error = "" if connected else "MQTT disconnected; reconnecting"

    def set_error(self, message: str) -> None:
        self.snapshot.mqtt_error = message


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mqtt-host", default=os.getenv("CO2_MQTT_HOST", DEFAULT_MQTT_HOST))
    parser.add_argument("--mqtt-port", type=int, default=int(os.getenv("CO2_MQTT_PORT", "1883")))
    parser.add_argument("--mqtt-topic", default=os.getenv("CO2_MQTT_TOPIC", DEFAULT_MQTT_TOPIC))
    parser.add_argument("--mqtt-username", default=os.getenv("CO2_MQTT_USERNAME", ""))
    parser.add_argument("--mqtt-password", default=os.getenv("CO2_MQTT_PASSWORD", ""))
    parser.add_argument("--mqtt-client-id", default=os.getenv("CO2_MQTT_CLIENT_ID", "co2-windows-bridge"))
    parser.add_argument("--network-name", default=os.getenv("CO2_NETWORK_NAME", "Remote Wi-Fi"))
    parser.add_argument("--port", type=int, default=int(os.getenv("CO2_BRIDGE_PORT", str(DEFAULT_PORT))))
    return parser.parse_args()


def main() -> None:
    from aiohttp import web
    from aiohttp.client_exceptions import ClientConnectionResetError

    args = parse_args()
    snapshot = Snapshot(network_name=args.network_name)
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")

    async def health(_: Any) -> web.Response:
        return web.json_response({"ok": True, "stage": 1, "source": "mqtt", "mqtt_connected": snapshot.mqtt_connected})

    async def sensor(_: Any) -> web.Response:
        return web.json_response(snapshot.payload())

    no_cache = {"Cache-Control": "no-store, max-age=0"}

    async def index(_: Any) -> web.FileResponse:
        return web.FileResponse(WINDOWS_DASHBOARD / "index.html", headers=no_cache)

    async def dashboard_css(_: Any) -> web.FileResponse:
        return web.FileResponse(WINDOWS_DASHBOARD / "dashboard.css", headers=no_cache)

    async def dashboard_js(_: Any) -> web.FileResponse:
        return web.FileResponse(WINDOWS_DASHBOARD / "dashboard.js", headers=no_cache)

    async def websocket(request: Any) -> web.WebSocketResponse:
        ws = web.WebSocketResponse(heartbeat=30)
        await ws.prepare(request)
        queue: asyncio.Queue[str] = asyncio.Queue()
        snapshot.subscribers.add(queue)
        try:
            await ws.send_str(json.dumps(snapshot.payload(), ensure_ascii=False))
            while not ws.closed:
                try:
                    await ws.send_str(await asyncio.wait_for(queue.get(), timeout=PUBLISH_INTERVAL))
                except asyncio.TimeoutError:
                    await ws.send_str(json.dumps(snapshot.payload(), ensure_ascii=False))
        except (ConnectionResetError, ClientConnectionResetError, asyncio.CancelledError):
            pass
        finally:
            snapshot.subscribers.discard(queue)
        return ws

    app = web.Application()
    app.add_routes([web.get("/health", health), web.get("/api/sensor", sensor), web.get("/ws", websocket),
                    web.get("/", index), web.get("/dashboard.css", dashboard_css),
                    web.get("/dashboard.js", dashboard_js)])

    async def runner() -> None:
        runner = web.AppRunner(app)
        await runner.setup()
        await web.TCPSite(runner, "0.0.0.0", args.port).start()
        MqttSubscriber(snapshot, asyncio.get_running_loop(), args).start()
        logging.info("Windows MQTT Bridge: http://127.0.0.1:%s/", args.port)
        await asyncio.Event().wait()

    asyncio.run(runner())


if __name__ == "__main__":
    main()
