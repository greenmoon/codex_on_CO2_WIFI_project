# CO2_WIFI macOS Handoff

更新時間：2026-10-04 22:15（Asia/Taipei）

此 repository 目前以 macOS 為主要開發與操作環境。開始工作前先讀取 `AGENTS.md`、本文件與 `docs/qa_co2_progress_daily_digest.html`；QA 最新為 V53、條目 054。

## 已驗證版本

| 項目 | 版本 / 狀態 | Source of truth |
|---|---|---|
| ESP32-C3 Gateway / local Dashboard | V1.9.0 | `src/main.cpp`、`data/index.html` |
| ESP32-S3 remote Gateway | R1.3.2，BLE decode、Wi-Fi/MQTT 與 OTA 已驗證 | `src/s3_remote_gateway.cpp` |
| Global iPhone Dashboard | R1.3.15，Voice Alert、10 分鐘 Live Curve 與 sample dots | `index.html`、`data/remote_iphone_dashboard.html` |
| MQTT payload | Schema 2 / MQTT-WSS R1 | S3 firmware 與 Global Dashboard |

## 現行資料路徑

```text
CO₂ Sensor
  → BLE advertising
  → ESP32-S3 Gateway
  → Wi-Fi STA
  → MQTT publish
  → NAS MQTT/WSS
  → Global iPhone Dashboard
```

macOS Python bridge 是保留的本機診斷工具，不是 Global Dashboard 的必要元件：

```text
MQTT broker → tools/mqtt_bridge.py → HTTP :8080 → data/iphone_dashboard.html
```

## macOS 簡化操作

啟動本機 bridge：

```zsh
./tools/start_mqtt_bridge.command
```

檢查與停止：

```zsh
curl http://127.0.0.1:8080/api/co2
lsof -nP -iTCP:8080 -sTCP:LISTEN
pkill -f tools/mqtt_bridge.py
```

PlatformIO 與 Serial Monitor 操作請讀取 `docs/macos_setup.md`。

## Active folder layout

```text
tools/
  mqtt_bridge.py
  start_mqtt_bridge.command
  configure_ota_upload.py
docs/
  macos_setup.md
logs/
  .gitkeep
offline_qr/
  README.md
  global/
```

不再使用 `tools/common/`、`tools/macos/`、`tools/windows/`、`platform/`、`logs/macos/`、`logs/windows/`、`offline_qr/current/`、`offline_qr/macos/` 或 `offline_qr/windows/`。

舊 Windows 實作位於 `archive/windows_legacy/`；舊 Mac LAN-IP QR 位於 `archive/local_bridge_qr_legacy/`。這些只供追溯，不是 active runtime。

## 安全與下一步

- Wi-Fi 或 MQTT credential 僅保存在本機 `include/secrets.h`，不要寫入 Git 或 QA。
- `.venv/`、`.pio/`、USB device path 與執行中的 process 都是 Mac 本機狀態。
- 下一次 dashboard 修改必須同步更新版本與 QA，並完成 GI 公開頁面驗證。
