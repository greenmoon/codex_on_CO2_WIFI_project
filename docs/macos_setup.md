# macOS 操作說明

## MQTT bridge

從專案根目錄執行：

```zsh
./tools/start_mqtt_bridge.command
```

確認 bridge：

```zsh
curl http://127.0.0.1:8080/api/co2
lsof -nP -iTCP:8080 -sTCP:LISTEN
```

停止 bridge：

```zsh
pkill -f tools/mqtt_bridge.py
```

## ESP32-C3

使用專案既有的 macOS virtual environment 執行 PlatformIO；USB serial port 依實際裝置調整，例如 `/dev/cu.usbmodem101`。本專案採 macOS 單層工具結構，不再使用 `tools/macos/` 或 `platform/macos/`。

```zsh
.venv/bin/pio device monitor --port /dev/cu.usbmodem101 --baud 115200
```
