# BLE Stage 02 實機捕獲紀錄

- 時間：2026-09-07 07:09（Asia/Taipei）
- 當時平台：macOS
- 當時 ESP32-C3 USB：`/dev/cu.usbmodem101`
- ESP32-C3 MAC：`9c:cc:01:d0:67:28`
- 韌體：`CO2_WIFI Stage 2 firmware V1.1.1`
- 狀態：Wi-Fi AP 與 BLE 被動掃描器同時啟動

## CO₂ 候選裝置

```text
name=(unnamed)
address=b0:e9:fe:e2:48:fd
RSSI=-99 dBm
UUID=-
manufacturer=69 09 B0 E9 FE E2 48 FD 24 64 01 9A 44 00 0B 02 9C 00
serviceData=-
raw=02 01 06 13 FF 69 09 B0 E9 FE E2 48 FD 24 64 01 9A 44 00 0B 02 9C 00
```

Manufacturer Data 前兩個 byte `69 09` 對應 little-endian Company ID `0x0969`。本階段只保存原始 BLE 廣告證據，不推定後續 byte 的 CO₂、溫度、濕度或電池欄位。

## 待驗證

- iPhone 連線 `CO2_WIFI` 後開啟 `http://192.168.98.1/`。
- 確認 Dashboard 的 Address、RSSI、Manufacturer Data 與 Raw Advertisement Payload 和 Serial 證據一致。
- Stage 03 需同時記錄裝置螢幕數值與多筆 raw payload，才能驗證 byte offset。

## 跨平台提示

上述 `/dev/cu.usbmodem101` 是本次 macOS 捕獲的歷史證據，不是跨平台固定值。新的測試請使用實際 serial port：macOS 通常為 `/dev/cu.usbmodem*`，Windows 通常為 `COMx`；平台操作參考 `platform/macos/README.md` 與 `platform/windows/README.md`。
