# PaperBriq Check machine

Open **Process → Check machine → Run tests**. Confirm that the machine is
unloaded, the door is closed, and an operator will remain beside the physical
emergency stop. The app requests fresh readiness checks before submitting
`self_test`. It does not automatically Reset or arm the machine.

The controller tests nine components in sequence: shredder, mixer, mixer gate,
molder gate, presser, pusher, exhaust fan, indicators/buzzer and lock. See
[chamber setup](CHAMBER_SETUP.md) for timings and serial commands.
The heater remains disabled.

Each item changes from Waiting to Testing to **Run completed** only when its
matching controller completion event arrives. Paired movements must both finish.
A tick confirms a timed command sequence, not verified movement: there are no
current/RPM/position/bolt sensors to prove a motor ran, a gate moved, or a lock
engaged. Observe the mechanisms during these unloaded tests.

**Stop tests** sends software Stop. The physical emergency stop is the immediate
hardware control. Closing the dialog does not stop a running test. Interrupted,
offline or missing reports never produce guessed completion ticks. A new run
or controller reboot clears previous results. Finishing the sequence leaves
the controller software-stopped; another Reset is required before motion.

**Check connections and sensors** inside the dialog remains read-only. It checks
telemetry, Uno round-trip communication, valid DHT readings, both expanders,
Stop/Emergency contacts, door contact, armed/idle state and pending commands.
It does not verify the physical emergency cutoff or sensor accuracy.

## Install the update

1. Secure the machine with actuator power disconnected for firmware updates.
   Upload `ArduinoUnoMotorTest/ArduinoUnoMotorTest.ino`, keeping its
   `ChamberControl.h` in the same sketch folder. Boot does not move the gates.
2. Upload `PaperBriqMonitor/PaperBriqMonitor.ino` using the existing `secrets.h`.
3. Deploy the updated backend. Schema initialization creates the health table
   and updates PostgreSQL's command constraint to allow `self_test`; its database
   account needs the corresponding CREATE/ALTER privileges.
4. Install `frontend/android/app/build/outputs/apk/debug/app-debug.apk`.
5. Wait for fresh reports. Inspect the unloaded machine, Reset/arm, and open
   **Process → Check machine**. Older firmware without the new health fields
   cannot enable these component tests.

For a local supervised check without the app/backend, use Uno Serial Monitor:
`I` shows chamber state; `R` arms; `Q` runs the sequence; `X` stops it.
Building files does not upload firmware, install the APK or deploy Render.

## Protocol and freshness

The ESP32 sends a nonce-tagged `CHECK`; the Uno replies:

```text
HEALTH|nonce|buttonBoard|stopClosed|emergencyClosed|indicatorBoard|armed|doorClosed|fanOn|unlockOn|testActive
```

Flags are `0` or `1`. Nine flags produce version 2 telemetry. Older five-flag
reports remain readable as version 1 but do not advertise component-test support.
Only the outstanding nonce is accepted, within 5 seconds. The ESP32 expires Uno
reports after 20 seconds; the backend accepts received reports for 30 seconds.
Consequently monitoring data can be approximately 50 seconds old plus transport
time. The Uno's local input checks operate independently of these network reports.

The API requires the existing control key, `unloadedSupervised: true`, fresh
passing readiness and version 2 support before queuing `self_test`, with a
15-second command lifetime. The Uno checks local conditions again before starting.
An acknowledgement means accepted, not that every part passed. `TEST_STARTED`,
`TEST_ITEM_STARTED`, `TEST_ITEM_DONE`, `TEST_ABORTED` and `TEST_COMPLETED` events
drive the app checklist. Events are matched to the device and run command ID.

## Verification

Run the backend tests:

```text
node --test scripts/test-machine-checks.js scripts/test-machine-process.js scripts/test-component-tests.js
node scripts/check.js
```

Compile and run `scripts/test-chamber-control.cpp` and
`scripts/test-uno-integration.cpp` with a C++11 compiler; the integration test
uses `scripts/uno-test-stubs` as an include directory. No hardware is accessed.
Compile both Arduino sketches and run Android `assembleDebug lintDebug`.
Physical behavior remains to be checked by the operator after upload.
