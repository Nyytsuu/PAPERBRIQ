#pragma once

// Copy this file to secrets.h and replace every placeholder. Never commit
// secrets.h. All saved Wi-Fi networks must be 2.4 GHz for this ESP32 board.
#define HOME_WIFI_SSID "your-home-wifi"
#define HOME_WIFI_PASSWORD "your-home-password"
#define HOME_SERVER_HOST "192.168.1.100"

#define HOTSPOT_ONE_SSID "your-first-hotspot"
#define HOTSPOT_ONE_PASSWORD "your-first-hotspot-password"
#define HOTSPOT_ONE_SERVER_HOST "192.168.43.100"

#define HOTSPOT_TWO_SSID "your-second-hotspot"
#define HOTSPOT_TWO_PASSWORD "your-second-hotspot-password"
#define HOTSPOT_TWO_SERVER_HOST "192.168.137.100"

// Render deployment: no trailing slash. Leave empty to use the matching local
// *_SERVER_HOST above at http://<host>:4173.
#define PAPERBRIQ_API_BASE_URL "https://your-paperbriq-service.onrender.com"

// These must match IOT_API_KEY and the intended device in the backend.
#define PAPERBRIQ_IOT_API_KEY "replace-with-a-long-random-api-key"
#define PAPERBRIQ_DEVICE_ID "paperbriq-esp32"

// HTTPS is fail-closed by default. Paste the PEM root CA for the Render
// service's current certificate chain here. Keep the BEGIN/END lines.
#define PAPERBRIQ_TLS_ROOT_CA ""

// TEST ONLY: setting this to 1 encrypts traffic but does not verify the server
// identity. Never use this for an internet-connected machine controller.
#define PAPERBRIQ_ALLOW_INSECURE_TLS 0
