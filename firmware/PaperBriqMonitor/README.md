# PaperBriq ESP32 Monitor and Command Bridge

This sketch targets the ESP-WROOM-32 30-pin board and the 1.8-inch 160x128 ST7735 display used by the PaperBriq prototype.

Before uploading, copy `secrets.example.h` to a new Arduino tab named `secrets.h`, then replace every placeholder. Arduino compiles and uploads all tabs in the sketch folder together; `secrets.h` is not uploaded separately. The sketch uses `WiFiMulti` and automatically connects to a saved home Wi-Fi or phone hotspot.

For local development, each network needs its own `*_SERVER_HOST` value because the laptop normally receives a different IPv4 address on each hotspot. Connect the laptop to a network, run `ipconfig`, and copy the laptop's IPv4 address into the corresponding host value. The laptop, ESP32, and Android phone must be on the same selected network, and that network must use 2.4 GHz for the ESP32.

For Render, set `PAPERBRIQ_API_BASE_URL` to the HTTPS service origin with no trailing slash. Set `PAPERBRIQ_IOT_API_KEY` to the backend `IOT_API_KEY` and set `PAPERBRIQ_DEVICE_ID` to `paperbriq-esp32`. Configure `PAPERBRIQ_TLS_ROOT_CA` with the PEM root CA for the deployed service. HTTPS fails closed when no CA is configured; the insecure TLS option is diagnostic only.

`secrets.h` is ignored by Git so Wi-Fi passwords are not committed. Use `secrets.example.h` as the safe template when sharing the project.

Install these Arduino libraries:

- Adafruit GFX Library
- Adafruit ST7735 and ST7789 Library
- DHT sensor library by Adafruit
- Adafruit Unified Sensor

Selecting **ESP32 Dev Module** is fine for a generic ESP-WROOM-32 board. Choose the correct CP2102 COM port and upload. Open Serial Monitor at 115200 baud. Successful telemetry reports `PaperBriq POST: 202`.

For API-key protection, set `IOT_API_KEY` in the backend `.env` and put the same value in the sketch. Restart `npm start` after changing `.env`.

## ESP32 to Arduino Uno command wiring

| Link | ESP32 | Arduino Uno |
| --- | --- | --- |
| ESP32 receives Uno events | GPIO16 RX2 | A3 TX through a 5 V-to-3.3 V level shifter or resistor divider |
| ESP32 sends commands | GPIO17 TX2 | A2 RX (direct connection is acceptable) |
| Reference | GND | GND |

Both sketches use 9600 baud for this UART link. Never connect the Uno's 5 V TX output directly to an ESP32 input. The grounds must share a reference, but motor and servo current must not flow through the breadboard or logic wiring.

The ESP32 polls Render for the oldest queued command, forwards `START`, `STOP`, or `RESET` to the Uno, and reports acknowledgements and machine events back to Render. It never drives a motor or servo directly and does not start movement at boot.

Upload this ESP32 sketch and `ArduinoUnoMotorTest.ino` to their respective boards. The IDs and keys must match the backend. When testing, issue Reset first to arm the Uno, then Start. Stop is latched until a later Reset.
