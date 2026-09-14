#!/usr/bin/env python3
"""MQTT TCP (1883) to HTTP bridge for the iPhone dashboard.

Shared by macOS and Windows. Requires: python -m pip install paho-mqtt
"""
import json
import threading
import time
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

import paho.mqtt.client as mqtt

BROKER, PORT, TOPIC = "59.124.7.96", 1883, "co2"
PROJECT_ROOT = Path(__file__).resolve().parents[2]
latest = {
    "co2_ppm": None,
    "temperature_c": None,
    "humidity_pct": None,
    "battery_pct": None,
    "received_ms": 0,
    "online": False,
}
lock = threading.Lock()


def on_connect(client, userdata, flags, rc, properties=None):
    with lock:
        latest["online"] = rc == 0
    if rc == 0:
        client.subscribe(TOPIC)


def on_message(client, userdata, msg):
    try:
        data = json.loads(msg.payload.decode())
    except (ValueError, UnicodeDecodeError):
        return
    with lock:
        latest.update(
            {
                key: data.get(key)
                for key in ("co2_ppm", "temperature_c", "humidity_pct", "battery_pct")
            }
        )
        latest["received_ms"] = time.time_ns() // 1_000_000


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path in ("/", "/iphone_dashboard.html"):
            self._send_file(PROJECT_ROOT / "data" / "iphone_dashboard.html", "text/html; charset=utf-8")
            return
        if self.path == "/iphone_qr.png":
            self._send_file(
                PROJECT_ROOT / "offline_qr" / "current" / "iphone_dashboard.png",
                "image/png",
            )
            return
        if self.path != "/api/co2":
            self.send_error(404)
            return
        with lock:
            body = json.dumps(latest).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def _send_file(self, path, content_type):
        try:
            body = path.read_bytes()
        except FileNotFoundError:
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_):
        pass


client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="co2-iphone-bridge")
client.on_connect, client.on_message = on_connect, on_message
client.connect_async(BROKER, PORT, 30)
client.loop_start()
print(f"MQTT bridge: {BROKER}:{PORT}/{TOPIC} -> http://0.0.0.0:8080/api/co2")
HTTPServer(("0.0.0.0", 8080), Handler).serve_forever()
