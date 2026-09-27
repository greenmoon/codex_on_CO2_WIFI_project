# Global iPhone Dashboard QR

GitHub Pages 發布後，此 QR 應固定編碼為：

```text
https://greenmoon.github.io/codex_on_CO2_WIFI_project/
```

根目錄 `index.html` 會自動導向 `data/remote_iphone_dashboard.html`。此 Dashboard 仍需要有效的 WSS MQTT endpoint 才能取得 `co2` payload。

產物：

- `iphone_dashboard.png`
- `iphone_dashboard.svg`

R1.3.1 產物規格：Error correction Q、quiet zone 4 modules、8 px/module、純黑白；PNG 為 360×360。Dashboard 以 280×280 顯示，並直接引用此靜態檔案，不再依賴外部 QR JavaScript。
