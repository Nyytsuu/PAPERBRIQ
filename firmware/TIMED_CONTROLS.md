# Production pump and drying timers

The actual Uno process sketch is now
[`PaperBriqController/PaperBriqController.ino`](PaperBriqController/PaperBriqController.ino).
Use `ArduinoUnoMotorTest` for individual actuator diagnostics and water-dose
measurement, and `PaperBriqMonitor` on the ESP32. Production limits are in
`PaperBriqController/ProductionLimits.h`; the production sketch cannot enable
standalone bench mode.

The Android Process screen uses a fixed 20-second pump preset for app-started
forming. The machine must report a commissioned limit that permits it. Drying
time starts unset and can be set and adjusted separately. These controls are
separate from the legacy 3-second/10-second checks.

## Operator workflow

1. Set Drying time with the hours/minutes/seconds wheels. Save time stores the
   setting on the server; it does not silently change an active machine.
2. While stopped, choose Enable preset pump for next batch, then Reset and Start forming.
   Gates close, then Waiting for paper is reported with motors and pump off.
   A fresh clear-to-detected transition at shredder IN (GPIO26) starts shredding,
   mixing and the pump together. Subsequent detections cannot repeat or extend
   that batch's water run. Clear the sensor before feeding. Start is rejected
   unless a pump cycle has been prepared. Legacy demonstration operation is
   available only with the explicit standalone bench build flag.
3. During feeding, the shredder runs while IN detects paper and stops when IN
   clears. There is no five-second production shredding limit. The mixer keeps
   running. New paper before feeding ends resumes shredding without another
   water dose. After IN stays clear and the pump has finished, a five-second
   finish-mixing interval completes before transfer. New paper resets that
   interval. Once transfer starts, detections cannot restart the shredder.
4. Optional: with forming stopped and the empty chamber closed, select Start
   preheat. Set the physical thermostat to the reported target (initially 45 C).
   Preheat includes warming and holding, with a fixed ten-minute ceiling. Ready
   to load requires fresh temperature within 1 C below target for ten continuous
   seconds. The ready indication returns to warming if temperature drops below
   that band. Press Stop before opening/loading. Heating is cancelled by an open
   door and never resumes just because it closes.
5. After loading and closing the chamber, use Start drying. Its selected timer
   begins only after temperature qualifies, with a separate ten-minute maximum
   warm-up. No paper sensor starts heating or counts finished briquettes. Forming,
   pumping and heating remain mutually exclusive because combined supply demand
   is not verified. Shredder OUT (GPIO19) is monitoring only; GPIO25 remains a
   legacy reading until its new physical location is specified.
6. During a drying run, save a new total duration and choose Apply total time. Elapsed
   time is retained: changing a 60-second run to 90 seconds after 20 seconds leaves
   70 seconds. A new total below elapsed time stops the output.
7. For drying, the app displays the controller's applied total, remaining time and state.
   Reports refresh over Wi-Fi polling, not instantaneously. Command success does
   not prove physical water flow or heating.

Stop cancels the cycle; use the physical emergency stop for immediate power
removal. Door/expander/contact faults stop the output locally. Local deadlines
continue without a phone connection. ACK timeout still sends failsafe Stop.
No concurrent pump and drying cycles or automatic restart after Stop/reboot.

The operator confirmed on 2026-10-31 that the separately powered heater fan runs
continuously with its 12 V supply. Keep it powered during heating/cooling. The
software does not switch that fan or claim to measure its airflow.
Temperature is set on the external thermostat. The Uno enables/disables heat;
it does not program the thermostat. The ten-second exhaust-fan diagnostic is
not the heater airflow control.

## Current commissioning gates

On 2026-10-31 the operator confirmed pump suppression and a separate heater
overtemperature cutoff are installed, and all four 5-second presser/pusher
travels were checked. These are operator reports, not remotely measured results.

- PAPERBRIQ_PUMP_VERIFIED and PAPERBRIQ_HEATER_VERIFIED now default to 1.
- Pump maximum remains 0 until actual flow and batch water volume are measured.
  This blocks automatic dosing and Start forming, not the existing three-second
  Y measurement pulse.
- Drying maximum is initially 600 seconds (10 minutes) per deliberate start.
  This is an initial operator ceiling, not a claim that briquettes are dry in
  ten minutes. Temperature remains on the external thermostat; 45 C was proposed
  as a starting setting for assessment, not a validated drying recipe.
- Preheat is explicitly started while stopped, never automatically with Start
  forming. It has a 600-second total ceiling; warming for drying also has a
  600-second ceiling. A new command is needed after Stop, door opening, expiry,
  sensor failure, or reboot. No automatic door unlock is added.
- The Uno polls the ESP32 for DHT readings every 2.5 seconds. Heating requires a
  valid Uno reading younger than 5 seconds; the bridge sends only cached samples
  younger than 3 seconds. Lost/invalid temperature or temperature at target + 5 C
  cancels heating locally. This supervisory check does not replace the physical
  thermostat or independent cutoff. Network/UART delays can cause safe shutdowns.
- PAPERBRIQ_STANDALONE_BENCH now defaults to 0. Enabling the old bench switch
  cannot bypass production gates.
- App users cannot raise these maxima. Unknown limits no longer expose the
  arithmetic storage ceiling in the duration picker.
- The 2,147,483-second parser/storage ceiling is arithmetic capacity, not a safe
  operating limit. Commissioned maxima are enforced by app, server and Uno.

The ESP32 samples GPIO26 and GPIO19 in a separate FreeRTOS task every 10 ms,
accepting a level after 50 ms stability. This continues during HTTP/Wi-Fi waits.
IN is forwarded as IR|0/IR|1 on changes and every 250 ms after a fresh production
report. A mutex keeps entire UART frames intact across tasks. The Uno requires
a clear-to-detected transition less than 1500 ms after the previous clear frame.
Missing inlet heartbeats while waiting, shredding or finish-mixing cancel the
batch and require Reset; a reconnection does not restart it. Pulses shorter than the debounce window are
rejected; check actual paper transit speed. These are not personnel safety sensors.

## Remaining pump measurement

Supply/prime the pump, secure the outlet into a graduated container and keep the
machine stopped. The existing Uno Y command gives a three-second measurement
pulse after settling, with the same door/Stop/contact checks. It does not arm a
production batch. Measure delivered volume and repeat to establish consistency.
Required seconds = desired batch volume / measured volume per second. Account for
the actual hose/head and startup behavior. Establish the batch dose and maximum
from those measurements before changing the zero pump ceiling. A 6 A label does
not specify flow. Never choose a 10- or 30-minute water dose from that rating.

## Deployment

Deploy the backend, install the rebuilt APK, and upload both updated sketches
with actuator power off. PostgreSQL/MySQL startup creates per-device settings
storage; PostgreSQL updates the command constraint. Use the replacement ESP32.

Verbose USB log copies default off to leave Uno flash space. ACK/EVENT lines and
USB input commands remain. Diagnostic builds can set PAPERBRIQ_VERBOSE_USB_LOGS=1,
subject to flash capacity. An isolated QA build used example limits to verify
capacity, never as actual commissioning values. No firmware was uploaded and no
machine was operated during software validation.

## API and UART

POST /api/device/production-settings requires the control key and saves
dryingSeconds by deviceId (whole positive seconds, or null to unset).
Control-status returns productionControls with settings and controller evidence.
Missing, stale and legacy reports disable actions while settings remain editable.

Commands use /api/device/commands. For pump_arm, the server supplies the preset
regardless of any duration sent by older clients. The drying and preheat
commands take durationSeconds.

| API action | UART example | Meaning |
| --- | --- | --- |
| pump_arm | PP:1234ABCD:20 | Arm one paper-triggered preset water run |
| drying_start | DD:2345ABCD:1800 | Start a separately loaded drying cycle |
| preheat_start | HH:3456ABCD:600 | Start bounded preheating while forming is stopped |
| drying_time | DT:2345ABCD:1200 | Change this drying run's total |

Start requests receive a server-generated nonzero eight-digit hexadecimal run ID.
Edits must supply that runId; server and Uno reject stale IDs. Commands expire
after five seconds in the queue. Duplicate edits assign a total time, not extra
time, so retries cannot repeatedly extend a deadline.

PRO2|probe-id|pumpMax|dryingMax|pumpVerified|dryingVerified|kind|phase|runId|total|elapsed|remaining|preheatMax|targetC|temperatureFresh|thermalStage|heaterFanConfirmed
precedes HEALTH. Kind is 0/1/2 (none/pump/drying); phases are 0 idle, 1 waiting,
2 running, 3 finished, 4 stopped, 5 fault. Times are seconds; report run IDs are
decimal. The ESP32 validates the probe ID and ranges before authenticated telemetry.
Kind 3 means preheat. Thermal stages are 0 idle, 1 warming, 2 ready to load,
3 timed drying, 4 fault. Drying phase 1 means waiting for temperature, not paper.
The bridge accepts old PROD reports as version 1; the app/server require version
2 for preheat/drying. Existing pump commands remain compatible. Deploy the server,
app and both sketches together; PostgreSQL startup adds HH to the command constraint.

## Validation

- test-production-controls.js: server settings/authentication, durations, limits,
  freshness, run identity and preservation of the forming process state.
- test-production-timer.cpp: live edits, IR one-shot, faults and clock rollover.
- Native Uno production mode: actual sketch commands, IR batch start, mixer hold,
  deadlines, live edits, replay and Stop/door/board/contact shutdowns.
- Native Uno production_disabled mode: unmeasured pump and excessive drying
  requests are rejected; an explicitly started 600-second drying cycle stops
  locally at its deadline.
- test-paper-input.cpp: rejects short noise, accepts stable input transitions,
  invalidates stale samples and handles clock rollover.
- test-esp32-ack.py: real bridge handlers, production parsing/forwarding, delayed
  HTTP, acknowledgement failures and fragmented messages.

Software simulations and builds do not replace physical commissioning.
