# Windows QR

Windows bridge 啟動後，使用 `ipconfig` 取得 Windows Wi-Fi 的 IPv4 address，組成：

```text
http://<Windows-LAN-IP>:8080/iphone_dashboard.html
```

以此 URL 產生 `iphone_dashboard.png` 與 `iphone_dashboard.svg` 放在本資料夾。測試成功後，將兩個檔案複製到 `../current/`，使 bridge 的 `/iphone_qr.png` 顯示 Windows QR。
