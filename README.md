# CO2_WIFI

ESP32-C3 將 CO₂ 裝置的 BLE 資料轉送至本機 Wi-Fi AP，供 iPhone Dashboard 顯示。Stage 1 以模擬資料建立 Wi-Fi Dashboard；Stage 2 加入 BLE 被動掃描與原始封包觀察。

## Current versions

| Component | Version | Source of truth |
|---|---:|---|
| ESP32-C3 Gateway firmware | V1.9.0 | `src/main.cpp` |
| ESP32-C3 local Dashboard | V1.9.0 | `data/index.html` |
| ESP32-S3 remote Gateway firmware | R1.3.2 | `src/s3_remote_gateway.cpp` |
| Global iPhone Dashboard | R1.3.15 | `index.html`, `data/remote_iphone_dashboard.html` |
| MQTT payload schema | 2 | S3 payload and remote Dashboard validation |
| MQTT/WSS interface | R1 | Remote Dashboard footer |
| STA-only diagnostic firmware | STA_ONLY_V1.3 | `src/sta_only.cpp` |
| STA+MQTT diagnostic firmware | STA_MQTT_TEST_V1.2 | `src/sta_mqtt_test.cpp` |

Global Dashboard R1.3.15 將 CO₂ Voice Alert 放在 Live Curve 上方；Live Curve 會在每一筆有效 MQTT 資料上畫出圓點，最新點使用較大標記與外圈，並維持 10 分鐘、60 點 circular buffer 與 PLAY／PAUSE。

## Stage 1

- AP SSID：`CO2_WIFI`
- AP 密碼：`co2wifi123`
- Dashboard：`http://192.168.98.1/`
- Health API：`http://192.168.98.1/health`
- WebSocket：`ws://192.168.98.1/ws`
- Payload source：`simulator`

## iPhone QR 快速連線

- `data/qr_wifi.svg`：標準 Wi-Fi QR，內容為 `WIFI:T:WPA;S:CO2_WIFI;P:co2wifi123;;`。
- `data/qr_dashboard.svg`：Dashboard URL QR，內容為 `http://192.168.98.1/`。
- 先掃描 Wi-Fi QR 並接受加入網路，再掃描 URL QR 開啟 Dashboard。

## 建置與燒錄

安裝 PlatformIO 後執行：

```sh
pio run
pio run --target upload
pio run --target uploadfs
pio device monitor
```

燒錄檔案系統後，iPhone 連線至 `CO2_WIFI`，再開啟 `192.168.98.1`。若瀏覽器沒有自動跳出登入頁，也可手動輸入網址。

## Stage 2

- 使用 NimBLE-Arduino 2.x 被動掃描，scan interval 100 ms、window 60 ms。
- 候選條件：名稱包含 `CO2`／`WoTH`、Service UUID `0xFD3D`，或 Manufacturer Data 開頭為 Company ID `0x0969`（little-endian `69 09`）。
- 本次實機偵測到的 CO₂ Address `b0:e9:fe:e2:48:fd` 作為 fallback 目標，避免廣告段拆分時漏過候選裝置。
- Serial 與 Dashboard 顯示名稱、Address、RSSI、Service UUID、Manufacturer Data、Service Data 及完整原始 ADV Payload。
- 目前只掃描與蒐證，不建立 GATT 連線，也不把原始 byte 解碼成真實 CO₂ 數值。

## Stage 2 限制

- Dashboard 的 CO₂ 數值仍由模擬器產生。
- BLE GATT 連線與實際 CO₂ Payload 解碼留待後續階段。
- 必須在實機上驗證 ESP32-C3 AP、WebSocket 與 iPhone 重連行為。

## Stage 2 實機結果

V1.1.1 已在 ESP32-C3 revision v0.4 編譯與燒錄成功，並同時啟動 `CO2_WIFI` AP 與 BLE Scanner。已捕獲 Address `b0:e9:fe:e2:48:fd`、RSSI `-99 dBm` 及 Company ID `0x0969` 的 Manufacturer Data；完整證據見 `docs/ble_stage02_capture.md`。iPhone Dashboard 實機顯示與斷線重連仍待人工確認。

## Stage 3 實機結果

V1.2.0 已加入獨立 `sensor_decoder`，去除 Manufacturer Data 的 Company ID 後解碼 15-byte payload。實機取得 CO₂ 668 ppm、溫度 26.1°C、濕度 68%、電池 100%；Dashboard 現在使用真實 BLE 數值，未收到有效封包時顯示等待，不再顯示模擬數值。V1.2.1 再加入 iPhone Wi-Fi 與 Dashboard URL 兩張 QR。詳細解碼證據見 `docs/ble_stage03_decode.md`。

## Stage 4

V1.3.0 將 BLE scanner、decoder、共享 snapshot、JSON schema 4、WebSocket 與 iPhone Dashboard 串成完整 Bridge，另提供 `GET /api/sensor`。Dashboard 顯示 Bridge READY、Wi-Fi client 與 WebSocket client 數；ESP32-C3 實機已確認持續解碼，iPhone WebSocket 端到端重連需在燒錄後再次開啟 Dashboard 驗證。

## Stage 5

V1.4.0 加入分層復原：BLE scanner 停止時立即重啟，目標封包超過 60 秒未更新時限頻重掃；Wi-Fi AP 每 5 秒做 health check，只重啟 AP/DNS，連續 3 次無法復原才受控重開機。Main loop 由 15 秒 task watchdog 保護，Dashboard 顯示 BLE/AP restart 計數與 watchdog 狀態。

V1.4.1 在 Dashboard 加入 Health Checking 失敗歷程，依序記錄 ID、iPhone 本地時間、原因與修復結果；紅色代表失敗、黃色代表復原中、綠色代表已恢復。最新記錄在前，最多 30 筆並保存在瀏覽器 localStorage。

V1.4.2 新增 Live Heartbeat 呼吸 LED 與 Reading cycle 進度條，由 firmware 的 `publish_interval_ms` 決定動畫週期（預設 1000 ms）。即時資料為綠色呼吸，stale 為黃色，offline 為紅色並停止週期動畫。

V1.4.3 將 ESP32-C3 SoftAP gateway 與 Dashboard 由 `192.168.4.1` 改為 `192.168.98.1`，同步更新 captive redirect、文字提示與 Dashboard URL QR。

## Stage 6

V1.9.0 將真實 BLE decode、Router STA、MQTT broker 連線與 topic `co2` publish 整合到 ESP32-C3 Gateway。Local Dashboard 已同步為 V1.9.0，可顯示 STA IP、broker、topic、publish 次數與上次 publish 年齡。

## macOS 本機診斷工具

目前 active tools 採單層 macOS 結構：

```zsh
./tools/start_mqtt_bridge.command
```

`tools/mqtt_bridge.py` 訂閱 MQTT topic `co2`，並在 `:8080` 提供 `/api/co2`、本機 Dashboard 與 Global Dashboard QR。操作說明位於 `docs/macos_setup.md`。Windows 舊實作已移至 `archive/windows_legacy/`，不屬於 active runtime。
