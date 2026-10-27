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

## Automatic startup

The chamber DHT22 remains on GPIO27 and publishes temperature/humidity to the
app. TFT use is disabled by default while disconnected; define
`PAPERBRIQ_TFT_ENABLED 1` in `secrets.h` to restore it. USB Serial Monitor at
115200 baud prints readings/errors. The updated Uno's **S** command requests a
recent sample over UART. See [DHT chamber check](../DHT_CHAMBER_CHECK.md).

The ESP32 draws the dashboard, initializes the sensors and Uno UART, and
automatically connects to an available saved Wi-Fi network. It then polls the
configured backend for commands and posts telemetry when DHT readings are
available. No Serial Monitor choices or `D`, `R`, `P`, or `W` commands are needed.
If Wi-Fi disconnects, the main loop retries the saved networks automatically.

The startup banner is `PaperBriq monitor starting - automatic Wi-Fi and app connection`.
The display keeps the 1 MHz SPI drawing speed and rotation `3` used during TFT
troubleshooting. Automatic network startup does not start the Uno's motors;
the existing Reset/Start command flow still applies.

Selecting **ESP32 Dev Module** is fine for a generic ESP-WROOM-32 board. Choose the correct CP2102 COM port and upload. Open Serial Monitor at 115200 baud. Successful telemetry reports `PaperBriq POST: 202`.

For API-key protection, set `IOT_API_KEY` in the backend `.env` and put the same value in the sketch. Restart `npm start` after changing `.env`.

## ESP32 to Arduino Uno command wiring

| Link | ESP32 | Arduino Uno |
| --- | --- | --- |
| ESP32 receives Uno events | GPIO16 RX2 | A3 TX through a 5 V-to-3.3 V level shifter or resistor divider |
| ESP32 sends commands | GPIO17 TX2 | A2 RX (direct connection is acceptable) |
| Reference | GND | GND |

Both sketches use 9600 baud for this UART link. Never connect the Uno's 5 V TX output directly to an ESP32 input. The grounds must share a reference, but motor and servo current must not flow through the breadboard or logic wiring.

The ESP32 polls Render for the oldest queued command, forwards `START`, `STOP`, `RESET`, or `SELF_TEST` to the Uno, and reports acknowledgements and machine events back to Render. It never drives a motor or servo directly and does not start movement at boot. Version 2 health reports include door, fan-command, unlock-command and component-test state. See [Check machine](../MACHINE_CHECK.md) for the supervised tests and required updates.

The TFT header follows Uno machine events and displays `GETTING READY`, `READY / CHECK`, `PROCESS RUNNING`, `PROCESS COMPLETE`, `STOPPED`, or `FAULT / CHECK`. Its status dot blinks while preparing, after completion, and during a fault. The Wi-Fi dot at the right of the header remains green when connected and red when disconnected.

Upload this ESP32 sketch and `ArduinoUnoMotorTest.ino` to their respective boards. The IDs and keys must match the backend. When testing, issue Reset first to arm the Uno, then Start. Stop is latched until a later Reset.

## Command acknowledgement troubleshooting

The `ack-diagnostics-v4` startup marker identifies the updated bridge. Upload
`PaperBriqMonitor.ino` to the ESP32 and use its USB monitor at 115200 baud.
Upload the updated `ArduinoUnoMotorTest.ino` to the Uno for specific Reset
rejection reasons; its USB monitor runs at 9600 baud.

`Sent command to Uno` confirms transmission was requested, not that the Uno
received it. Look for the matching ID in `ESP32 -> Uno: CMD|...|RESET` on the
Uno, followed by `ACK|...|DELIVERED|received` and a final ACK. On the ESP32,
`Uno RX: ACK|...` confirms the reply reached that board.

The ESP32 now prints the command outcome and its reason separately from the
HTTP result. `ACK report HTTP: 200` means Render accepted the report; inspect
`Command status` / `Command result` for the machine outcome. Timeouts distinguish
no matching ACK from DELIVERED received but no final result. Uno rejection
reasons include `door_open`, `chamber_offline`, `buttons_offline`,
`emergency_open`, `stop_open`, `unlock_active`, `fan_active`, `test_running`,
`process_running`, and `reset_too_soon`.

While awaiting the final ACK, the bridge pauses network requests and keeps
servicing UART. Intermediate DELIVERED reports are superseded by the final
result before uploading to Render, so HTTPS does not block the reply exchange.
The five-second timeout and failsafe STOP remain enabled. The UART receive
buffer is increased to 2048 bytes for bursts during other network requests.
These changes cannot repair a broken Uno A3 TX → voltage reduction → ESP32
GPIO16 RX2 connection or a missing common ground.

Hardware-free checks: `python scripts/test-esp32-ack.py` exercises the actual
bridge handlers with simulated UART, slow HTTP, and timeouts. The native Uno
integration test's `reset_ack` mode checks rejection reasons and replay safety.
