# Production pump and drying timers

The Android Process screen saves pump/drying durations on the server and provides
commands for starting a cycle and changing its total time. These are separate
from the legacy 3-second/10-second checks. Times start unset.

## Operator workflow

1. Set Pump time and Drying time in minutes and seconds. Save time stores the
   setting on the server; it does not silently change an active machine.
2. While stopped, choose Enable pump for next batch, then Reset and Start forming.
   One fresh paper detection during shredding/mixing starts the pump. Subsequent
   detections cannot repeat or extend that batch's water run.
3. Mixing waits for a running pump to finish and then completes its configured
   finish-mixing interval. If no paper arrives by the end of the initial mixing
   interval, the armed pump cycle is cancelled and the machine software-stops.
4. After loading the chamber, choose Start drying. The existing IR sensor detects
   shredded paper entering the mixer; it does not start heating or count finished
   briquettes. Drying requires a stopped machine and closed door.
5. During a run, save a new total duration and choose Apply total time. Elapsed
   time is retained: changing a 60-second run to 90 seconds after 20 seconds leaves
   70 seconds. A new total below elapsed time stops the output.
6. The app displays the controller's applied total, remaining time and state.
   Reports refresh over Wi-Fi polling, not instantaneously. Command success does
   not prove physical water flow or heating.

Stop cancels the cycle; use the physical emergency stop for immediate power
removal. Door/expander/contact faults stop the output locally. Local deadlines
continue without a phone connection. ACK timeout still sends failsafe Stop.
No concurrent pump and drying cycles or automatic restart after Stop/reboot.

The separately powered heater fan must run continuously during heating/cooling.
Temperature is set on the external thermostat. The Uno enables/disables heat;
it does not program the thermostat. The ten-second exhaust-fan diagnostic is
not the heater airflow control.

## Current commissioning gates

The software path is implemented, but production outputs remain disabled in this
checkout. The operator confirmed the pump's suppression protection and heater's
independent overtemperature cutoff are absent. Neither operating maximum is set.

- PAPERBRIQ_PUMP_VERIFIED and PAPERBRIQ_HEATER_VERIFIED remain 0.
- ProductionLimits.h leaves both maximum durations 0 (not commissioned).
- PAPERBRIQ_STANDALONE_BENCH now defaults to 0. Enabling the old bench switch
  cannot bypass production gates.
- After verifying physical protection, airflow, water supply, wiring, sensor
  behavior and operating durations, commission each output's verification flag
  and measured maximum. App users cannot raise these maxima.
- The 2,147,483-second parser/storage ceiling is arithmetic capacity, not a safe
  operating limit. Commissioned maxima are enforced by app, server and Uno.

The ESP32 samples/debounces IR between network operations and forwards IR|0/IR|1
after a current production report. The Uno requires a recent clear-to-detected
transition while mixing. Brief pulses during blocking network calls may be missed.
Commissioning must verify detection at actual paper speed; a missed detection
stops the armed process instead of dosing blindly. Use a controller-local sensor
or independent event capture if physical testing shows misses.

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

POST /api/device/production-settings requires the control key and saves deviceId,
pumpSeconds and dryingSeconds (whole positive seconds, or null to unset).
Control-status returns productionControls with settings and controller evidence.
Missing, stale and legacy reports disable actions while settings remain editable.

Commands use /api/device/commands with durationSeconds:

| API action | UART example | Meaning |
| --- | --- | --- |
| pump_arm | PP:1234ABCD:60 | Arm one paper-triggered batch water run |
| drying_start | DD:2345ABCD:1800 | Start a separately loaded drying cycle |
| pump_time | PT:1234ABCD:90 | Change this pump run's total |
| drying_time | DT:2345ABCD:1200 | Change this drying run's total |

Start requests receive a server-generated nonzero eight-digit hexadecimal run ID.
Edits must supply that runId; server and Uno reject stale IDs. Commands expire
after five seconds in the queue. Duplicate edits assign a total time, not extra
time, so retries cannot repeatedly extend a deadline.

PROD|probe-id|pumpMax|dryingMax|pumpVerified|dryingVerified|kind|phase|runId|total|elapsed|remaining
precedes HEALTH. Kind is 0/1/2 (none/pump/drying); phases are 0 idle, 1 waiting,
2 running, 3 finished, 4 stopped, 5 fault. Times are seconds; report run IDs are
decimal. The ESP32 validates the probe ID and ranges before authenticated telemetry.

## Validation

- test-production-controls.js: server settings/authentication, durations, limits,
  freshness, run identity and preservation of the forming process state.
- test-production-timer.cpp: live edits, IR one-shot, faults and clock rollover.
- Native Uno production mode: actual sketch commands, IR batch start, mixer hold,
  deadlines, live edits, replay and Stop/door/board/contact shutdowns.
- Native Uno production_disabled mode: default gates cannot energize outputs.
- test-esp32-ack.py: real bridge handlers, production parsing/forwarding, delayed
  HTTP, acknowledgement failures and fragmented messages.

Software simulations and builds do not replace physical commissioning.
