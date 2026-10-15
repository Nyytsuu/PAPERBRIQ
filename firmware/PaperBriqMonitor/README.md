# PaperBriq ESP32 Monitor

This sketch targets the ESP-WROOM-32 30-pin board and the 1.8-inch 160x128 ST7735 display used by the PaperBriq prototype.

Before uploading, edit `secrets.h`. The sketch uses `WiFiMulti` and automatically connects to the strongest saved home Wi-Fi or phone hotspot.

Each network needs its own `*_SERVER_HOST` value because the laptop normally receives a different IPv4 address on each hotspot. Connect the laptop to a network, run `ipconfig`, and copy the laptop's IPv4 address into the corresponding host value. The laptop, ESP32, and Android phone must be on the same selected network, and that network must use 2.4 GHz for the ESP32.

`secrets.h` is ignored by Git so Wi-Fi passwords are not committed. Use `secrets.example.h` as the safe template when sharing the project.

Install these Arduino libraries:

- Adafruit GFX Library
- Adafruit ST7735 and ST7789 Library
- DHT sensor library by Adafruit
- Adafruit Unified Sensor

Select an ESP32 board such as **DOIT ESP32 DEVKIT V1**, choose the correct CP2102 COM port, and upload. Open Serial Monitor at 115200 baud. Successful telemetry reports `PaperBriq POST: 202`.

For API-key protection, set `IOT_API_KEY` in the backend `.env` and put the same value in the sketch. Restart `npm start` after changing `.env`.
