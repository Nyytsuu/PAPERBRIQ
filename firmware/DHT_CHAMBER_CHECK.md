# Drying-chamber DHT22 check

The sensor stays on **ESP32 GPIO27**. The ESP32 reads it and sends temperature
and humidity to the existing app telemetry endpoint. The Uno requests readings
over the existing UART link; it does not need another sensor connection.

## Update and test

1. Upload the updated `PaperBriqMonitor` to the ESP32 using the existing
   `secrets.h`. TFT operation is disabled by default while disconnected.
2. Upload the updated `ArduinoUnoMotorTest` to the Uno, with actuator power
   disconnected during the update.
3. Power both boards. Open **Uno Serial Monitor at 9600 baud** and send **S**.
   Reset/arm is not required. No motor, servo, fan, lock or heater is activated.
   Example output (not an actual measurement):

   ```text
   Chamber DHT22: 29.4 C, 63.2 %RH (ESP32 reading)
   ```

4. Send S again for another recent reading. Alternatively, open **ESP32 Serial
   Monitor at 115200 baud** to see each sample or sensor error directly.
   Firmware `chamber-dht-v3` and newer (`ack-diagnostics-v4`) also accept **S directly on ESP32 USB Serial**.
   Its startup line includes `DHT22 GPIO27 | USB S and Uno S supported`.
   If Uno S times out, check this ESP32 output first: the timeout does not prove
   the DHT22 is faulty. Sampling now runs before network requests, and Wi-Fi
   retries return to the main loop between attempts. Network calls can still
   delay a response.
5. Install the rebuilt Android APK and open **Sensors**. Home also shows chamber
   temperature and humidity. For older servers without `machineChecks`, the app
   can display numeric ESP32 readings less than 30 seconds old using the existing
   telemetry timestamps. Machine-start checks still require the updated backend.

Both firmware uploads are needed for Uno's S command. App readings require Wi-Fi
and the configured backend. The UART check does not require a successful upload
to the backend. Network operations can delay the ESP32's normal 2.5-second sample
interval and its response to the Uno.

## Reading the result

- **Numeric values:** a recent valid sensor response, not proof of calibration.
- **No fresh valid reading:** ESP32 replied but has no successful sample less
  than 10 seconds old. Check the sensor connection and try again after sampling.
- **ESP32 reply timed out:** no matching response within 10 seconds. Check both
  firmware versions, ESP32 power and the existing UART link.
- **Waiting for a fresh drying chamber reading:** the app lacks fresh sensor
  health. Failed or expired checks hide previous numbers instead of presenting
  them as current.

If Render reports the device online but Temperature/Humidity have `value: null`
and `updatedAt: Never`, it has no stored readings in its current telemetry store.
Device command polling alone can make the device appear online. Inspect the
ESP32's sensor messages and `PaperBriq telemetry POST` result; HTTP 202 is the
expected accepted response. Newer servers supply `machineChecks` for sensor
health; a reported failure still hides the readings. Missing `machineChecks`
alone no longer hides fresh ESP32 measurements in the updated Android app.

Requests use `DHT_CHECK|requestNumber`; replies use
`DHT|requestNumber|OK|temperature|humidity` or `DHT|requestNumber|ERROR`.
Replies must match the pending request. They do not change actuator replay IDs
or clear a software stop. Simulated tests cover errors, stale/invalid replies
and preservation of stopped outputs; they do not test the physical sensor.

To restore the TFT later, add `#define PAPERBRIQ_TFT_ENABLED 1` to ESP32
`secrets.h` and upload again. Existing display pins and rotation are retained.
Heater control remains disabled; this update only monitors the chamber.
