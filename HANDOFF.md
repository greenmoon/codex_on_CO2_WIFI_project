# CO2_WIFI Cross-Platform Handoff

更新時間：2026-10-04 21:50（Asia/Taipei）

此文件用於從 MacBook 本機 Codex task 交接到 Windows 的新 Codex task。專案檔案由 Dropbox 同步；本機 chat、USB Serial Monitor、執行中的 bridge process 不會跨電腦同步。

## 1. 續接前必讀

1. `AGENTS.md`：專案規則、QA 紀錄與完成交接格式。
2. 本文件：目前已驗證狀態與下一步。
3. `docs/qa_co2_progress_daily_digest.html`：完整工程決策與測試證據；最新為 QA V52、條目 053。

Windows 新 Codex task 可先輸入：

```text
讀取 AGENTS.md、HANDOFF.md、docs/qa_co2_progress_daily_digest.html。
這是從 MacBook 交接的 CO2_WIFI project。
先摘要目前版本、已驗證結果、未完成項目與建議下一步；不要修改檔案，等待我說 go。
```

## 2. 已完成且有證據的功能

| 項目 | 狀態 | 證據 / 檔案 |
|---|---|---|
| ESP32-C3 Gateway firmware / local Dashboard | V1.9.0，版本與 Stage 6 STA/MQTT UI 已同步 | `src/main.cpp`、`data/index.html`、`data/app.js` |
| ESP32-S3 remote Gateway firmware | R1.3.2，已實測 authenticated A/B OTA、BLE decode 與 MQTT publish | `src/s3_remote_gateway.cpp`、QA 048 |
| Global iPhone Dashboard | R1.3.15，payload schema 2，MQTT/WSS R1，Live Curve 每筆資料有 dot | `index.html`、`data/remote_iphone_dashboard.html`、QA 053 |
| Gateway 流程 | BLE advertising decode → Wi-Fi STA → MQTT publish `co2` | `src/main.cpp`、QA 049 |
| STA+MQTT 隔離測試 | 每 5 秒 `{"f":N}` 連續 publish OK | `src/sta_mqtt_test.cpp`與已保留的 Serial 實測紀錄 |
| Router 基準 | 2.4 GHz WPA2、固定 channel 6 的測試成功 | `include/secrets.example.h`與 Serial 實測紀錄 |
| Mac MQTT-to-HTTP bridge | MQTT subscribe `co2` → HTTP `/api/co2` | `tools/common/mqtt_bridge.py` |
| iPhone Dashboard | 每 2 秒 fetch bridge API，顯示 CO₂/溫濕度/電量/QR | `data/iphone_dashboard.html` |
| Dashboard 狀態 LED | GREEN/YELLOW/RED 實測完成 | QA 023–029 |

## 3. 現行資料路徑與角色

```text
CO2 Sensor --BLE advertising--> ESP32-C3 Gateway
ESP32-C3 Gateway --Wi-Fi STA / MQTT publish co2--> MQTT Broker
MQTT Broker --MQTT subscribe co2--> Bridge（目前 Mac 或未來 Windows）
Bridge --HTTP /api/co2--> iPhone Dashboard
```

- **Gateway**：第一顆 ESP32-C3；接收、解析 BLE，並 publish MQTT。
- **Bridge**：目前為電腦上的 Python 程式；subscribe MQTT 並對 iPhone 提供 HTTP。
- **Router**：提供 Gateway、Bridge、iPhone 的 LAN/WAN 連線。
- **MQTT Broker**：外部訊息中繼；topic 為 `co2`。

## 4. Dashboard LED 實測結果

| 測試 | 動作 | LED | 判定 |
|---|---|---|---|
| T1 | Gateway publish 且 payload 年齡 ≤10 秒 | GREEN | MQTT → bridge → iPhone 正常 |
| T2 | 關閉 Gateway 電源 | YELLOW | bridge 可達，但超過 10 秒無新 payload |
| T3 | 終止 bridge | RED | iPhone 無法 fetch HTTP API |
| T4 | 啟動 bridge、Gateway 仍關閉 | YELLOW | HTTP 恢復，但保留資料已 stale |
| T5 | Gateway 恢復供電與 publish | GREEN | 新 payload 到達 bridge |

紅燈測試／停止 Mac bridge：

```zsh
pkill -f tools/mqtt_bridge.py
```

Mac 重新啟動 bridge：

```zsh
./tools/macos/start_mqtt_bridge.command
```

## 5. Windows 作為 Bridge 的續接事項

Windows 與 iPhone、Gateway 應在同一 Router/LAN；允許 Python 通過 Windows Firewall 的 Private network 與 TCP port 8080。完整操作請讀取 `platform/windows/README.md`。

```powershell
cd C:\path\to\codex_on_CO2_WIFI_project
py -3 -m pip install --user paho-mqtt
powershell -ExecutionPolicy Bypass -File .\tools\windows\start_mqtt_bridge.ps1
```

檢查 Windows bridge：

```powershell
curl http://127.0.0.1:8080/api/co2
ipconfig
```

以 Windows Wi-Fi IPv4 address 組成 iPhone URL：

```text
http://<Windows-LAN-IP>:8080/iphone_dashboard.html
```

QR code 必須在 Windows LAN IP 改變後重新產生。`tools/common/mqtt_bridge.py` 是共用實作；`tools/macos/` 與 `tools/windows/` 則是平台專屬啟動器。QR 使用 `offline_qr/current/`；Mac/Windows 的保存版本分別位於 `offline_qr/macos/` 與 `offline_qr/windows/`。

## 6. 平台資料夾

```text
tools/common/       共用 bridge 實作
tools/macos/        macOS 啟動器
tools/windows/      Windows PowerShell 啟動器
platform/macos/     macOS 操作文件
platform/windows/   Windows 操作文件
logs/macos/         選擇性保存 Mac 測試 log
logs/windows/       選擇性保存 Windows 測試 log
```

## 7. 安全與操作注意

- 不在此文件、QA 或 chat 中記錄 Wi-Fi 密碼；credential 維持於本機 `include/secrets.h`。
- 不要讓 Mac 與 Windows 同時修改同一個檔案；先等待 Dropbox 同步完成。
- Mac/Windows 可同時 subscribe MQTT，但 Demo 時僅保留一台作 bridge，避免判讀混亂。
- `.venv` 與 USB Serial port 是每台電腦獨立環境；Windows 必須建立自己的 Python/PlatformIO 環境。

## 8. 建議下一步

1. 在 Windows 建立並測試 MQTT bridge，從 iPhone 開啟 Windows LAN URL。
2. 重新產生含 Windows IP 的 dashboard QR code。
3. 重做 T1–T5，驗證 Windows bridge 的 GREEN/YELLOW/RED 行為。
4. 長期方案：第二顆 ESP32-C3 實作 MQTT subscribe + Wi-Fi AP + HTTP dashboard bridge，移除對筆電的依賴。
