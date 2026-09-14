# Stage R1 — ESP32-S3 Local Gateway + Remote iPhone WSS Dashboard

## 產物

- ESP32-S3 firmware：`src/s3_remote_gateway.cpp`
- PlatformIO environment：`esp32-s3-remote-gateway`
- iPhone browser dashboard：`data/remote_iphone_dashboard.html`（R1.2.1，頁面內會依目前 HTTPS URL 產生分享 QR；預設 WSS 為 `jbnas03.synology.me:8084/mqtt`，舊 `.98` 設定會自動修正）

## ESP32-S3 RGB status (R1.1.0)

預設使用 ESP32-S3 DevKitC-1 的 addressable RGB LED GPIO48，由獨立 Core 0 LED task 消費事件 queue：

| Event | RGB | Duration |
|---|---:|---:|
| BLE advertising 解碼成功 | Blue | 160 ms |
| MQTT JSON payload 打包完成 | Cyan | 160 ms |
| MQTT publish `59.124.7.98:1883/co2` 成功 | Green | 300 ms |
| MQTT publish 失敗 | Red | 600 ms |

若 S3 RGB LED 沒有亮，先確認實體板是否有 WS2812 RGB LED；其 GPIO 與標準 DevKitC-1 不同時，修改 `kRgbLedPin` 後重編譯。

## 資料路徑

```text
CO2 sensor BLE advertising
  -> ESP32-S3 BLE decode (Core 1)
  -> FreeRTOS queue
  -> Wi-Fi/MQTT publisher (Core 0)
  -> 59.124.7.98:1883 / co2
  -> Broker WSS :8084 /mqtt
  -> iPhone HTTPS Dashboard
```

ESP32-S3 以 5 秒最短間隔 publish 新 reading，30 秒無新 reading 時發出 heartbeat。無 MQTT username/password 的設定僅適用於目前測試。

## Build

```zsh
.venv/bin/pio run -e esp32-s3-remote-gateway
```

硬體 board model 與 USB serial port 必須在 upload 前實機確認。此文件不宣稱已完成 S3 燒錄。

## iPhone-only prerequisites

1. 將 `remote_iphone_dashboard.html` 部署至 Broker 的 HTTPS Web Server。
2. Broker 必須提供 `wss://<domain>:8084/mqtt`。
3. 使用有效 TLS domain；裸 IP 通常無法通過 iPhone 的 certificate 驗證。
4. 先以 Dashboard setting 輸入 broker domain、port、path；收到 telemetry 後可用 `device_id` filter 限定本顆 S3。
