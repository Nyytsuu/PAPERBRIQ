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
app. TFT use is enabled by default again; define
`PAPERBRIQ_TFT_ENABLED 0` in `secrets.h` to disable it when disconnected. USB Serial Monitor at
115200 baud prints readings/errors. The updated Uno's **S** command requests a
recent sample over UART. See [DHT chamber check](../DHT_CHAMBER_CHECK.md).

The ESP32 draws the dashboard, initializes the sensors and Uno UART, and
automatically connects to an available saved Wi-Fi network. It then polls the
configured backend for commands and posts telemetry when DHT readings are
available. No Serial Monitor choices or `D`, `R`, `P`, or `W` commands are needed.
If Wi-Fi disconnects, the main loop retries the saved networks automatically.

The startup banner is `PaperBriq monitor starting - automatic Wi-Fi and app connection`.
The display uses landscape rotation `1` and a 1 MHz SPI drawing speed.
Automatic network startup does not start the Uno's motors;
the existing Reset/Start command flow still applies.

TFT signal wiring: CS → GPIO33, DC/A0 → GPIO21, RESET/RES → GPIO22,
SCK/SCL → GPIO18, and MOSI/SDA → GPIO23, with a shared ESP32 GND.
SDA/SCL on this SPI display are not the Uno's I2C connections. Check the actual
module's power and backlight markings before connecting VCC and LED/BL.

Selecting **ESP32 Dev Module** is fine for a generic ESP-WROOM-32 board. Choose the correct CP2102 COM port and upload. Open Serial Monitor at 115200 baud. Successful telemetry reports `PaperBriq POST: 202`.

For API-key protection, set `IOT_API_KEY` in the backend `.env` and put the same value in the sketch. Restart `npm start` after changing `.env`.

## ESP32 to Arduino Uno command wiring

| Link | ESP32 | Arduino Uno |
| --- | --- | --- |
| ESP32 receives Uno events | GPIO16 RX2 | A3 TX through a 5 V-to-3.3 V level shifter or resistor divider |
| ESP32 sends commands | GPIO17 TX2 | A2 RX (direct connection is acceptable) |
| Reference | GND | GND |

Both sketches use 9600 baud for this UART link. Never connect the Uno's 5 V TX output directly to an ESP32 input. The grounds must share a reference, but motor and servo current must not flow through the breadboard or logic wiring.

The ESP32 polls Render for the oldest queued command, forwards `START`, `STOP`, `RESET`, `SELF_TEST`, or bounded `HEATER:<seconds>` / `PUMP:<seconds>` commands to the Uno, and reports acknowledgements and machine events back to Render. It never drives a motor or servo directly and does not start movement at boot. Version 3 health reports add timed-output support, commanded heater/pump states, and readiness to the version 2 chamber fields. Older Uno reports remain readable but cannot enable the app timer controls. See [app timer controls](../TIMED_CONTROLS.md) and [Check machine](../MACHINE_CHECK.md) for the supervised tests and required updates.

The TFT keeps the machine status and Wi-Fi indicators visible while it slides
between two sensor pages every seven seconds. **Chamber / Water** shows
temperature, humidity, and water level. **Paper Sensors** shows the shredder
inlet, shredder outlet, and briquette sensor. Readings update in place.

In the ESP32 Serial Monitor at 115200 baud, send `>` to advance a page or
`~` to freeze or resume the TFT. The ESP32 keeps its Wi-Fi and Uno links
active while the display is frozen. If the entire screen still pulses, check
the serial output for a repeated startup banner or a brownout reset.

Upload this ESP32 sketch and `ArduinoUnoMotorTest.ino` to their respective boards. The IDs and keys must match the backend. When testing, issue Reset first to arm the Uno, then Start. Stop is latched until a later Reset.

## Command acknowledgement troubleshooting

Production timers also use `PP`/`DD` start actions and `PT`/`DT` live edits with
run IDs. A matched `PROD` report accompanies HEALTH and carries output state,
limits, total/elapsed/remaining seconds. A separate sensor task reads shredder
IN GPIO26 and OUT GPIO19 even during network waits. IN triggers the prepared
batch's shredder, mixer and pump after Start and gate closure. Its continuing
level controls the shredder during feeding: detected runs, clear stops. New
paper during finish-mixing resumes shredding without restarting the pump.
OUT is monitoring only. GPIO25 remains a legacy reading and no longer triggers the pump. Serial S
also prints both new sensor levels. Neither sensor starts drying or proves
physical machine health. See [production timers](../TIMED_CONTROLS.md).

Preheat uses `HH` commands and version-2 `PRO2` reports with temperature target,
freshness and warming/ready/drying state. Update both sketches and the app/server.
The Uno requests fresh DHT samples automatically for thermal supervision; the
bridge rejects cached DHT samples older than three seconds for these replies.
The physical thermostat remains responsible for regulation; Wi-Fi or sensor
delays may stop heating locally rather than extend its permission to run.

The `ack-diagnostics-v4` startup marker identifies the updated bridge. Upload
`PaperBriqMonitor.ino` to the ESP32 and use its USB monitor at 115200 baud.
Upload `PaperBriqController/PaperBriqController.ino` to the Uno for production and specific Reset
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

## Background IR heartbeat (2026-11-06)

Firmware `ir-in-out-v9` samples GPIO26 inlet and GPIO19 outlet in the sensor
task. Once both inputs have stable samples, it sends IR2|inlet|outlet on changes and
every 250ms. This is independent of HTTP work, health reports and stage events.
The previous event gate could leave the Uno waiting without a heartbeat while
the ESP32 main loop was blocked by a network request. It is removed for the
NeoSWSerial/hardware-servo Uno configuration. Keep that Uno update installed.

The Uno retains ownership of arming, fresh clear-to-detected qualification,
three-second A-test pulse and paper-link timeout. Receiving an idle heartbeat
does not by itself start a motor. No actuator commands are sent by the sensor
task. TX frames still share the existing mutex. The change addresses availability
of IR samples; physical detection range, power and wiring still need checking.

Upload this ESP32 sketch, verify ir-in-out-v9 at 115200 baud, then use R,
wait 3s, and A on the Uno monitor at 9600. After WAITING_FOR_PAPER, clear the
inlet then present paper. The A test has its normal 2.5s direction settling
pause followed by a single 3s shredder pulse. X cancels.
