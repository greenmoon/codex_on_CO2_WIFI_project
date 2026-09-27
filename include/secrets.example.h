#pragma once

// Copy to secrets.h and fill in local credentials. Never commit secrets.h.
constexpr char kRouterSsid[] = "YOUR_ROUTER_SSID";
constexpr char kRouterPassword[] = "YOUR_ROUTER_PASSWORD";

// The firmware defaults to kRouterPassword when this optional constant is absent.
// For better separation, uncomment and set a dedicated LAN-only OTA password.
// constexpr char kOtaPassword[] = "YOUR_PRIVATE_OTA_PASSWORD";
