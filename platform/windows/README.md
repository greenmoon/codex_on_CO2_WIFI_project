# Windows 操作說明

## MQTT bridge

先安裝 Python 3，並以 Windows 自己的 Python 環境安裝套件：

```powershell
py -3 -m pip install --user paho-mqtt
```

從專案根目錄啟動：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\windows\start_mqtt_bridge.ps1
```

若要指定自己的 Python executable，可設定 `CO2_WIFI_PYTHON` 環境變數。

確認 bridge：

```powershell
curl http://127.0.0.1:8080/api/co2
ipconfig
```

允許 Python 通過 Windows Firewall 的 Private network，讓同一 LAN 的 iPhone 能開啟：

```text
http://<Windows-LAN-IP>:8080/iphone_dashboard.html
```

## ESP32-C3

Windows 的 Python virtual environment、PlatformIO cache 與 COM port 都是獨立於 Mac 的本機環境。不要執行 Dropbox 專案中由 macOS 建立的 `.venv`。
