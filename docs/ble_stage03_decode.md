# BLE Stage 03 解碼紀錄

- 時間：2026-09-07 07:20（Asia/Taipei）
- 韌體：`CO2_WIFI Stage 3 firmware V1.2.0`
- 目標：`b0:e9:fe:e2:48:fd`

## 60 秒採樣

共取得 5 筆 Manufacturer Data，payload 完全相同，RSSI 為 -101、-99、-94、-94、-94 dBm：

```text
69 09 B0 E9 FE E2 48 FD 25 64 01 9A 44 00 0B 02 9C 00
```

去除 Company ID `69 09` 後，以既有 CO2 專案的實機驗證規則解碼 15-byte sensor payload：

```text
battery[7]       = 0x64        → 100%
temperature[8]   = 0x01        → decimal 0.1
temperature[9]   = 0x9A        → positive 26
humidity[10]     = 0x44        → 68%
co2[13:14]       = 0x02 0x9C   → 668 ppm
```

## ESP32-C3 實機輸出

```text
[DECODE] CO2=668 ppm temp=26.1 C humidity=68% battery=100%
```

編譯、firmware 燒錄及 LittleFS 上傳均成功；RAM 14.3%，Flash 82.6%。仍需以 CO₂ 裝置螢幕同步讀值做第二來源比對，並以 iPhone 驗證 Dashboard。

