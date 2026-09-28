#pragma once

// Copy to secrets.h and fill in local credentials. Never commit secrets.h.
constexpr char kRouterSsid[] = "YOUR_ROUTER_SSID";
constexpr char kRouterPassword[] = "YOUR_ROUTER_PASSWORD";

// ESP32-S3 remote gateway Wi-Fi failover profiles.
// These aliases are published as HOME/IPHONE; the SSIDs are never sent in MQTT payloads.
constexpr char kS3HomeRouterSsid[] = "YOUR_HOME_2_4_GHZ_SSID";
constexpr char kS3HomeRouterPassword[] = "YOUR_HOME_PASSWORD";
constexpr char kS3IphoneRouterSsid[] = "YOUR_IPHONE_HOTSPOT_SSID";
constexpr char kS3IphoneRouterPassword[] = "YOUR_IPHONE_HOTSPOT_PASSWORD";

// The firmware defaults to kRouterPassword when this optional constant is absent.
// For better separation, uncomment and set a dedicated LAN-only OTA password.
// constexpr char kOtaPassword[] = "YOUR_PRIVATE_OTA_PASSWORD";
