# iPhone Offline QR

`windows_mqtt_dashboard.svg` opens the current Windows MQTT Bridge:

```text
http://192.168.1.3:8080/
```

Connect the iPhone and Windows PC to the same Wi-Fi before scanning. If the Windows IPv4 address changes, regenerate the QR:

```powershell
python tools/generate_windows_qr.py --url http://NEW_WINDOWS_IP:8080/
```

No Wi-Fi QR is stored here because the `JBSALES_07_5G` password is intentionally not saved in the project.

`windows_mqtt_bridge_qr_flow.png` is the visual reference for the complete IDEA1 flow: Windows MQTT Bridge → MQTT payload → Dashboard QR → iPhone Dashboard.
