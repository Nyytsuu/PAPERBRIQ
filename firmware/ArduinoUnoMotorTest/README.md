# PaperBriq Uno diagnostics and calibration

## W with dual IR monitoring (2026-11-06)

Upload this Uno sketch and ESP32 PaperBriqMonitor ir-in-out-v9 together. The
ESP32 sends IR2|inlet|outlet from GPIO26/GPIO19 every 250ms and on changes.
Legacy IR frames remain accepted for A and the separate P recipe. W requires
fresh dual-sensor data while feeding/mixing. A missing heartbeat stops W.

W retains its 10s shredder/water lead, 45s mixer pattern with shredder running,
10s gate wait, transfer, three loads with a repeat press on load 3, and final ejection/drying. At the mixer
deadline the gate stays closed until both sensors have been clear for 5s.
While waiting, the shredder continues and the mixer pauses for 2s before
running forward. Stop, faults and the existing 30-minute session limit can
still end the run.

Each W load has a 20s feeding water window and one 40s mixing dose.
Five seconds clear ends feeding water and starts the mixing dose.
After the presser retracts, a fresh inlet detection can start load 2 or 3.
A feed extension does not prove paper capacity or detect motor rotation.
Diagnostic prose was shortened/shared to fit Uno flash. Protocol event types,
command identifiers and safety checks are retained.


## Hardware servo and UART wiring update (2026-11-06)

This sketch now uses PWMServo 2.1.0 and NeoSWSerial 3.0.5. Both are installed
in the configured Arduino library directory. Power down before moving signals.

| Signal | Previous Uno pin | New Uno pin |
| --- | --- | --- |
| Mixer gate servo signal | A0 | D9 |
| Molder servo signal | A1 | D10 |
| Mixer driver RPWM | D9 | A0 |
| Mixer driver LPWM | D10 | A1 |

PWMServo generates the two gate pulses using Timer1 hardware. NeoSWSerial
supports simultaneous RX/TX at the existing 9600 baud on A2/A3. The servo power
comes from the existing servo supply. A2/A3 UART still requires the existing
voltage conversion and common ground. This update targets interrupt-induced
servo jitter and receive losses. Electrical noise remains a hardware-test item.

Upload with actuator power off after moving all four wires. At 9600 baud the
Uno startup banner identifies D9/D10 servos, A0/A1 mixer, and NeoSWSerial.
With the mechanisms clear, door closed and Stop/E-stop healthy, arm using R,
wait 3s, then check mixer gate O/C and molder gate 5/6 individually. Confirm
angles and direction before running a cycle. Servo pulse generation now runs
independently of CPU interrupt delays. Hardware travel still needs checking.

Both local and linked builds compile. Native tests cover the remapped motor
outputs, servo attach pins, the batch sequence, stops, UART parsing and pump
checks. These tests do not measure pulse widths or physical UART waveforms.
Current saved press durations are 15/13/11s, plus a 13s repeat on load 3 after
the first retraction. The presser retracts again before ejection. Earlier timing tables below are
historical snapshots; source constants take precedence.


For normal operation, upload [PaperBriqController](../PaperBriqController/PaperBriqController.ino).
This sketch is retained for individual motor/servo tests, component diagnostics,
and a separate configurable pump measurement pulse. It also retains the previous
process routines for diagnosis. Uploading a sketch replaces the one on the Uno.
See [production setup](../TIMED_CONTROLS.md), [chamber wiring](../CHAMBER_SETUP.md)
and [component checks](../MACHINE_CHECK.md).

## Three-load local sequence (2026-11-05)

W / physical Start combines three material loads into one molded briquette.
Start/W confirms preheat and begins the first load. The first paper detection
starts water delivery. Later loads can start on a new inlet detection
after the previous press retracts. Timed pumping does not measure water volume.

At the preheat confirmation or warm idle prompt, USB serial `8` starts at
load 2 and `9` starts at load 3. Both use the same safety checks as `W`.
They cannot skip an active load. Starting at load 3 still runs final ejection
and drying.

| Load | Mixer pattern | Press | Presser return |
| --- | --- | --- | --- |
| 1 | Forward 20s, OFF 2s, reverse 2s, OFF 2s, forward 10s, OFF 2s, reverse 2s | 18s | 19s |
| 2 | Same as load 1 | 16s | 19s |
| 3 | Same as load 1 | 10s | 19s |

The first 10s forward pass flows into the original pattern on the same load.
The mixer gate stays closed through both passes. The M fill test uses this
same mixing sequence and stops after transfer and gate closure.

Each load opens the mixer gate for the existing material-release stage, closes
it, presses and retracts. After loads 1 and 2, the molder stays closed and the
machine prints `NEXT LOAD: paper`. All motors, pump and heater remain off while
waiting. A clear-to-detected transition at the inlet requests the next load.
The normal 2.5s motor pause still applies. Start/W can also continue manually
from this wait. A Start during an active stage cannot skip it.
Opening the monitored door or any stop/fault cancels the session; closing the
door does not resume it. The absolute session deadline includes these waits.

After load 3 retracts, the molder opens, the pusher runs outward **12s** and
returns **12s**, then the molder closes and drying resumes. The requested
20s outward / 30s return is not configured pending travel/end-switch checking.
These are timed commands, not proof of full retraction or measured position.

G remains a single unloaded forming cycle without water/heat or IR waiting, using the first
mixer pattern and PROCESS_PRESSER_EXTEND_MS. The sensor-controlled P recipe is
separate. This update does not change the app or PaperBriqController sketch.

## Standalone batch with pump and thermostat-controlled heater

### Positioning just the presser, molder and pusher

Use **Z** for an operator-paced positioning sequence. It never runs the shredder,
mixer, mixer-gate servo, pump or heater. Start unloaded with the door closed,
the presser retracted and pusher at the intended starting park, clear of the
molder gate. Upload with actuator power off; open Serial Monitor at 9600 baud.
Send **R**, wait 3 seconds, then send **Z once per step**:

| Z step | Action | Current motor-on / gate travel time |
| --- | --- | --- |
| 1 | Close molder gate | 1.5s travel allowance |
| 2 | Extend presser | 19s |
| 3 | Retract presser; inspect its return | 19s |
| 4 | Open molder gate | 1.5s travel allowance |
| 5 | Extend pusher from park | 12s |
| 6 | Return pusher toward park; inspect it | 12s |
| 7 | Close molder gate; end sequence | 1.5s travel allowance |

Each motor request includes the normal 2.5s direction pause. Wait for movement
to finish and a further 2.5s settling interval before the next Z. An early Z is
rejected, not queued. Nothing automatically advances while you inspect a step.
After step 7, Reset is required for another run. X or any monitored safety fault
cancels positioning and requires Reset; cancellation does not issue a new gate
target, so it cannot newly close the molder onto an extended pusher. An already
moving servo may still complete its previous command. Inspect/reposition after
any interrupted step before restarting.

During positioning other actuator commands, W/physical Start and component tests
are blocked. H and I remain available. Timed completion is not position feedback;
use X/physical stop if a mechanism reaches an end and keeps straining. Do not
hold the motor stalled to establish its position.

Adjust `PROCESS_PRESSER_EXTEND_MS` in the sketch, and the three return/park
settings in `StandaloneCycle.h`. Z and G use PROCESS_PRESSER_EXTEND_MS; W uses the three BATCH_PRESS_*_MS settings,
so adjustments carry over without changing the unrelated manual E/T/F/B pulse
durations. Edit, save and re-upload to apply a changed duration.

**W** is the combined local batch command, separate from the unloaded **G** check.
It uses the operator-confirmed pump suppression, independent heater cutoff,
continuous heater fan, and automatic transfer of ejected briquettes into the chamber.
Keep those protections installed. For local tuning, power down the boards and
disconnect the UART signal wires to Uno A2/A3. W / physical Start runs on the
Uno with the external thermostat. G remains an unloaded check.

### Heating session: preheat, form, dry, bounded warm standby

With actuator starting positions checked, door closed and Stop/E-stop healthy:

1. Send **R** (physical Reset) to arm. Reset alone does not heat or move anything.
2. Send **W** (physical Start) to begin **PREHEAT**, with motors and pump off.
3. Check the external thermostat display. Once the chamber reaches the intended
   setpoint, send **W / Start again** to confirm and run the batch. The Uno cannot
   read the thermostat, so it never automatically declares that temperature reached.
   If not confirmed within 10 minutes, heating stops and Reset is required.
4. During forming, **heater D4 is LOW**. The heater, pump and motors share one supply;
   their combined load has not been verified. Heating resumes after ejection and
   molder closure for the existing **10-minute drying window**.
5. At the drying deadline, enter **WARM IDLE** for at most **2 minutes**. Motors and
   pump stay off, thermostat-controlled heat remains enabled. No next batch starts
   automatically. Check dryness and downstream space; elapsed time proves neither.
6. **X / physical Stop** ends the session. Opening the door also ends it and requires
   Reset after closing. For the present machine, use one chamber load at a time:
   finish drying, inspect/unload, close the door, then Reset and start a new session.
   W / Start during warm idle can begin another batch without Reset only if the
   door remained closed, no fault occurred, and the operator confirms capacity.

A **30-minute absolute session limit**, including forming time, never resets on
additional Start presses/batches. Reaching any timeout removes heat and motion,
latches Stop and requires Reset; a timeout during forming needs position inspection.
The thermostat switches the element within the enabled windows. D4 HIGH means
permission to heat, not confirmed element current or measured chamber temperature.

The heater's own fan is wired continuously to its supply. It is NOT controlled by
this sketch. Keep it powered for cooling after normal heat shutdown; there is no
temperature-verified cooldown, automatic unlock, or timed fan shutdown. Emergency
stop still takes precedence and cuts the machine's 12 V supply. Exhaust test
commands are not repurposed for cooldown.

The forming sequence follows the three-load table above. Water stops before
the mixer gate opens. Automatic
transfer after final ejection is assumed from the mechanical arrangement,
not confirmed by a chamber sensor.

**W and physical Start use the ESP32 inlet sensor to dose water.** Once
shredding starts, a detected inlet turns on the pump even if the sheet was
already there. Feeding water pauses as soon as the inlet reports clear and can
resume if paper returns within five seconds, up to 20000 ms from the first
detection. After five seconds clear and once mixing starts, one separate 40s
dose starts. Further detections do not restart it. If the mixer pattern ends
first, the mixer pauses before running forward until the dose finishes.
The mixer gate waits for it to finish before
transfer. Wait for `NEXT LOAD: paper` before feeding the next load.
`LOCAL_FEED_WATER_MS` and `LOCAL_MIX_WATER_MS` set the two timings.
Measure the output with the actual hose and paper batch before choosing a runtime.
Keep the pump supplied/primed and hose secured. X cancels the whole session.
Loss of fresh inlet reports also ends W water delivery. Door/expander faults
still cancel the session and cannot be corrected by increasing the water time.
To run **Y with the mixer gate open**, send R, wait 3 seconds, send O, wait
3 seconds for the gate, then send Y. Do not send X between O and Y: X commands
the gates closed when the door is healthy. Y now accepts an armed but idle
machine, keeps the existing gate target, and does not close the gate on normal
pump completion. Motors, new gate movements, heating and batch starts remain
blocked while Y is running; the drying-door interlock remains required closed.
The separate stopped-mode check still works: send X, wait for
2.5 seconds of settling (10 seconds between pump starts), then Y runs the pump
for `pumpDurationMs`. Your current sketch sets Y to **15000 ms / 15 seconds**,
separate from W's water settings. H displays the independent Y check time.
Collect and measure the discharge with the pump primed/supplied.
Determine the water required for your paper batch before setting a runtime.
Relevant settings:

- `LOCAL_FEED_WATER_MS` and `LOCAL_MIX_WATER_MS` set water timing in W. `BENCH_SHREDDER_RUN_MS`,
  `BATCH_MIX_FORWARD_MS`, `BATCH_MIX_REVERSE_MS`, `BATCH_MIX_PAUSE_MS`,
  `PROCESS_MATERIAL_RELEASE_MS`, and gate/pause times
  control their own stages, with a final pre-transfer water cutoff.
  The old `PAPERBRIQ_LOCAL_PUMP_MS` / `PAPERBRIQ_LOCAL_PUMP_MAX_MS` settings are removed.
- `SHREDDER_FORWARD_DUTY` in the sketch is now **153/255 (60% PWM)**, increased
  from 102/255 (40%). This changes forward process/manual operation; reverse
  remains 77/255. PWM percentage is not measured motor RPM. Stop on a jam/stall;
  increased PWM does not establish the motor/driver's load capability.
- Heating settings in `StandaloneCycle.h`:
  - `PAPERBRIQ_LOCAL_PREHEAT_MAX_MS`: **600000 / 10 minutes**, maximum wait for confirmation.
  - `PAPERBRIQ_LOCAL_HEAT_MS`: **600000 / 10 minutes** after forming. Temperature
    recovery may consume part of this window; it is not a proven drying recipe.
  - `PAPERBRIQ_LOCAL_WARM_IDLE_MS`: **120000 / 2 minutes**, bounded between-batch wait.
  - `PAPERBRIQ_LOCAL_SESSION_MAX_MS`: **1800000 / 30 minutes**, absolute session ceiling.
  These defaults are also their configuration ceilings, not proven process times.

The heater enable still travels **Uno D4 -> thermostat S1 -> S2 -> MOSFET TRIG**.
Set temperature on the thermostat itself (the current commissioning target is
45 C). The Uno supplies a bounded enable signal; it cannot read the thermostat,
confirm its setpoint, or measure temperature/airflow in this standalone mode.
Keep the independent thermal cutoff and continuously powered heater fan in service.
Unlike normal app heating, this explicitly selected local mode does not depend
on ESP32 DHT readings. It never fabricates a valid sensor reading.

Upload with actuator power off. Serial Monitor uses **9600 baud**. USB W and
physical Start follow the identical session sequence above. Watch `PREHEAT`,
`PUMP`, `DRYING`, `WARM IDLE`, and `HEAT TIMEOUT` messages. USB P and remote START
retain the original sensor-controlled process; the app and ESP32 are unchanged.
Boot and Reset alone never start heating or a batch.

Local W/Start uses the three-load mixing table at the top. G uses its first
pattern once; P/remote START keeps its separate sensor-controlled recipe.
Forward uses D9 HIGH/D10 LOW (`PROCESS_MIXER_USE_RPWM = true`, manual **3**);
reverse uses D9 LOW/D10 HIGH (manual **4**). Setting false swaps the directions.
Both directions use digital outputs because Servo occupies their PWM timer.
Manual **3/4** remain one-direction checks. A 2s pause does not measure zero
speed; verify the loaded mixer stops before reversal. Faults cancel the sequence.
The molder uses the same A1 servo and open/close angles as manual **5/6**, with
visible angle messages. A command message is not physical feedback: if manual 4
or 5 does not move its part, inspect the driver/servo supply, wiring and mechanism.
During each W load the first inlet detection starts the pump while the mixer gate is closed.
The molder is commanded open before pushing. This changes W and physical Start;
the separate ESP32-controlled P/app recipe is not used in this standalone mode.

The presser and pusher receive timed return commands after their respective
strokes and are then switched off. Standby is intended to be presser fully
retracted and pusher parked partway, using the requested timing approach.
In `StandaloneCycle.h`, the initial estimates are:

| Setting | Time | Intended movement |
| --- | --- | --- |
| `PAPERBRIQ_PRESSER_RETURN_MS` | 19000 ms | Presser return after pressing |
| `PAPERBRIQ_PUSHER_FROM_PARK_MS` | 12000 ms | Base outward time. W and physical Start use twice this value |
| `PAPERBRIQ_PUSHER_RETURN_TO_PARK_MS` | 12000 ms | Base return time for G, P, and Z. W uses three manual B pulse durations |

W presses 18/16/10s across its three loads, returning for 19s each time.
G/Z use the separate 19s extension setting. This is motor-on time, not confirmed
position: stop if an actuator reaches the end and strains. W uses 24s outward
and allows 69s to return, based on three 23s B pulses and the observed internal
retraction stop. The Uno waits for the full return timer before closing the molder.
Begin with the presser retracted and pusher at the chosen park; otherwise
these intervals will not establish the intended starting position. Adjust the
outward and return times separately from observed travel. An interrupted run
requires position checking again. Boot and Reset never home or move actuators.
The timing constants also apply to G's mechanical rehearsal in this sketch;
the separate `PaperBriqController` production sketch is unchanged.

X, physical Stop/E-stop monitoring, door opening and expander loss cancel motion,
pump and heater. Restoring contacts does not resume the cycle. Reset and a fresh W
are required, after inspecting any interrupted travel. Other local actuator
commands and arming are blocked during the session. W is accepted only during
preheat (temperature confirmation), warm idle (capacity confirmation), or the
explicit wait for load 2/3. During active stages it cannot skip or extend a batch. A held physical Start does not
repeat: it needs a new debounced press. Stopping or faults require a fresh Reset.
No firmware is uploaded as part of editing this sketch.

## One complete unloaded forming cycle without the ESP32

Upload `ArduinoUnoMotorTest.ino` to the Uno with actuator power off. Keep the
faulty ESP32 disconnected, including its UART wires. No changes to
`PAPERBRIQ_STANDALONE_BENCH` are needed: leave it at **0**.

1. Keep the machine unloaded, clear of people/tools, and supervise beside the
   physical emergency stop. Start with the presser and pusher retracted; this
   sketch cannot find their home positions. Close the door and release Stop/E-stop.
2. Open the **Uno** Serial Monitor at **9600 baud**. Enable actuator power when ready.
3. Send **R** (or press physical Reset) to arm. Reset does not start movement.
4. Send **G** to run one full timed forming pass. It closes both gates, runs the
   shredder alone for **18 seconds**, then the load-1 mixer pattern above,
   opens the mixer gate and allows **40 seconds** for release, then closes it.
5. The presser extends **19 seconds** and retracts **19 seconds**. The molder opens;
   the pusher extends **12 seconds** and retracts **12 seconds**; the molder closes.
   Existing 2.5-second motor/direction pauses and 1.5-second gate travel waits remain.
6. Watch the `EVENT` messages for progress and `PROCESS_COMPLETED` at the end.
   It runs once, with no automatic repeat. Timing completion is not position feedback.

**X**, physical Stop, emergency monitoring, an open door or lost expander cancels
the sequence. Use the physical emergency stop for an emergency. Restoring a contact
does not restart the cycle; Reset and a new G are required. A stopped sequence
may leave an actuator partway through travel: inspect/reposition it before retrying.

G is a **local USB-only mechanical rehearsal**. Pump, heater, exhaust and unlocking
are not part of this sequence; they retain their separate commands/requirements.
The ESP32 IR inputs and DHT are unavailable, so G uses a fixed shredder interval
and never claims paper was detected. Do not feed material for this unloaded check.
It refuses to interrupt another test or consume a prepared production timer.
Physical Start runs W after Reset. P and remote START retain their normal
production checks; none silently switches to G when the ESP32 is absent.

Timing adjustments are near the top of the sketch: `BENCH_SHREDDER_RUN_MS`,
`PROCESS_MIXER_FINISH_MS`, `PROCESS_MATERIAL_RELEASE_MS`, and the four
`PROCESS_PRESSER_*_MS` / `PROCESS_PUSHER_*_MS` durations. Values are milliseconds.
Keep the previously checked travel times initially; do not extend motor operation
against a mechanical stop to increase pressing force.

The app now supports production timer commands, server-saved settings and active
run adjustments. Both hardware protection flags are now confirmed by the operator.
Drying has an initial 10-minute ceiling; automatic dosing and full forming still
need a measured pump flow/dose limit. Inlet GPIO26 starts a prepared batch only
after Start, closed gates and a fresh clear-to-detected sensor transition.
Verbose USB copies are disabled by default for flash capacity; ACK/EVENT messages
remain visible. Diagnostic builds may enable `PAPERBRIQ_VERBOSE_USB_LOGS=1`.

With verbose USB logs enabled, send **S** at 9600 baud to request the drying-chamber DHT22
reading from ESP32 GPIO27. No arming or movement is required. See
[DHT chamber check](../DHT_CHAMBER_CHECK.md) for firmware updates and app setup.

## Current standalone function-test mode

`PAPERBRIQ_STANDALONE_BENCH` now defaults to **0**. Setting it to 1 restores the
legacy brief supervised checks with the ESP32 disconnected. The operator confirmed
digital D4 -> thermostat S1, S2 -> heater MOSFET TRIG, and digital D3 -> pump TRIG.
This enables local J/Y tests and bounded app runs, not automatic drying/pumping. The normal
hardware-verification flags are independent of this mode.

- **J:** ten-second heater enable, at least 60 seconds between starts.
- **Y:** three-second pump enable, at least 10 seconds between starts.
- **X:** stop the active test. Nothing starts at boot; repeated J/Y cannot extend it.

Upload with actuator power off. Keep the failed ESP32 disconnected. For a powered
test, keep the chamber empty, the heater fan continuously running and the thermostat
probe installed with heating control in circuit. Never bridge S1/S2. Supply/prime
the pump with water, secure its hoses/outlet and keep liquids away from electronics.
Stay beside the physical emergency disconnect. Close the door, clear Stop/Emergency
contacts and wait 2.5 seconds for settling before J or Y at 9600 baud. Do not arm
with R or start production. Do not touch the heater to judge operation.

This temporary mode does **not require an ESP32 temperature reading**; it relies
on the external thermostat. A known reading of 40 C or above still blocks heat.
Door/board loss, Stop/Emergency, X and UART frame faults still cancel outputs.
Tests exclude each other, motion, arming and unlocking. No airflow, water-flow or
physical success is measured. Software cannot stop a failed-short MOSFET.
The operator confirmed pump suppression and the independent heater cutoff on
2026-10-31. Short checks do not establish a water dose or drying recipe. Keep the
fan running while the heater cools afterward.

Restore `PAPERBRIQ_STANDALONE_BENCH` to **0** after the checks. The normal modes
below retain their hardware-verification gates and fresh-temperature requirement.
See [app timer controls](../TIMED_CONTROLS.md) for the implemented production
command path and its separate commissioning gates. Bench mode cannot bypass them.

## Normal heater-test mode (standalone mode off)

The sketch now initializes **digital D4 LOW** for heater enable. D4 is not A4:
**A4 is the I2C data connection for the expanders and must not feed thermostat S1.**
With all power off and the heater branch fuse removed, the intended control path is
`Uno D4 -> W3230 S1`, then `W3230 S2 -> heater XY-MOS TRIG`.
S1/S2 carry only the Uno enable signal in this arrangement; neither connects to 12 V.
The thermostat supply VCC/GND remains separate. MOSFET control GND shares Uno GND.
Provide a verified trigger pull-down (onboard or external) so an open thermostat
contact or resetting Uno cannot leave the MOSFET trigger floating.

`PAPERBRIQ_HEATER_VERIFIED` now defaults to **1**, based on the operator's wiring,
fan and independent cutoff confirmations. Keep those protections installed.
A 10 A or 15 A current fuse is **not** a thermal cutoff.

Once verified and enabled, the local USB-only `J` command requests a three-second
heater-enable test. It requires the machine stopped (not armed), closed door, healthy
expanders and Stop/Emergency contacts, no other test, 2.5 seconds of settling, and a
successful `S` temperature reply less than five seconds old and below 40 C.
Send `X`, wait for settling, then `S`; only after valid values appear may `J` run.
The 40 C guard is a bench-test limit, not a production drying temperature.
Repeated `J` does not extend a pulse; another pulse requires another successful `S`.
`X`, a UART frame fault, invalid/stale temperature, or lost/open interlocks remove
the enable signal. Arming, motion and unlocking are blocked during a heater test.
The firmware cannot measure airflow or prove that the thermostat or heater operates.
Software timing does not replace independent thermal protection.

The thermostat must also request heat for D4 to reach TRIG. A successful D4 pulse
does not prove heat production. Production drying is a separate app start with
its own total duration; it does not require standalone mode. Keep the heater fan running during cooling; the heater test does not
control that separately powered fan. No firmware has been uploaded by this change.

## Motor signal wiring

### Normal pump-test mode (standalone mode off)

The operator reports a **12 V / 6 A pump**, D3 signal wiring and suppression now
installed. Water dose and flow are not yet measured. Suppression must suit this
pump and driver, including its startup/stall current.
Do not assume the 3 A 1N5408 discussed for the lock is suitable for the 6 A pump.

With power off, the proposed control wiring is **Uno digital D3 -> pump XY-MOS
TRIG**, with the module's small GND connected to common Uno/supply ground.
Confirm a trigger pull-down, onboard or external, for resets. Use fused 12 V to
VIN+, supply negative to VIN-, and the pump across OUT+/OUT-. OUT- must not be
connected directly to supply negative, which would bypass the MOSFET. Load current
must use appropriate power wiring, not breadboard rails or Dupont jumpers. Verify
the module, fuse, wiring and power supply against startup/stall current as well as
the stated 6 A rating. No fuse or suppression component rating is selected here.

`PAPERBRIQ_PUMP_VERIFIED` now defaults to **1**. The pump retains its own hardware
flag, and a separate production maximum which remains 0 pending a water measurement.
Once the pump hardware is verified and enabled, **Y** in Uno USB Serial Monitor
at 9600 baud requests a three-second pump pulse. Use `X`, wait at least 2.5 seconds
for movement to settle, and ensure the pump is supplied/primed with water as its
manufacturer requires before `Y`. Never assume the firmware detects dry running.

Door closed, both expanders online and healthy Stop/Emergency contacts are required.
The machine must be stopped and no heater, fan, unlock, indicator, component test or
production cycle may be running. Repeating Y during a pulse cannot extend it;
starts are separated by at least 10 seconds. X, a UART frame fault, a door/board
fault or an opened Stop/Emergency contact cancels the output. Successful timing
only proves a command was issued: it does not prove water flow or pump health.
This short Y pulse is separate from automatic production dosing and the
all-components check. No firmware has been uploaded by this change.

| Motor | BTS7960 input | Arduino Uno |
| --- | --- | --- |
| Shredder | RPWM | D5 |
| Shredder | LPWM | D6 |
| Presser | RPWM | D7 |
| Presser | LPWM | D8 |
| Mixer | RPWM | D9 (digital full-speed pulse only) |
| Mixer | LPWM | D10 (digital full-speed pulse only) |
| Pusher L298N | IN3 | D11 |
| Pusher L298N | IN4 | D12 |
| Pusher L298N | ENB | Regulated 5 V rail (permanently enabled) |
| Unused | - | D13 |
| Mixer gate servo | Signal | A0 |
| Molder servo | Signal | A1 |
| ESP32 command link | Uno A2 RX from ESP32 GPIO17 |
| ESP32 event link | Uno A3 TX to ESP32 GPIO16 through 5 V-to-3.3 V level shifting |

The Uno only supports hardware PWM on D3, D5, D6, D9, D10, and D11. The standard Servo library uses Timer1, so the operator-selected mixer pins D9/D10 cannot provide reliable `analogWrite` PWM while either servo is active. This sketch keeps those pins and drives the mixer using full-speed digital HIGH/LOW pulses. The presser remains on D7/D8. The pusher direction remains on D11/D12, while L298N ENB is permanently enabled from regulated 5 V and D13 is unused. Adjustable mixer/presser/pusher speed with this exact allocation requires an external PWM controller, a second controller, or a larger board.

The mixer-gate servo uses A0 and the molder servo uses A1 as digital signal outputs. Power both servos from a properly rated regulated 5-6 V supply, not from 12 V and not from the Uno regulator under motor load. Size the supply for the combined stall current of both servos. Connect the servo-supply ground, Uno GND, BTS7960 logic GND, and 12 V supply negative to the same common reference without carrying motor or servo supply current through the breadboard.

A2 and A3 form the 9600-baud `SoftwareSerial` link to the ESP32. ESP32 GPIO17 (3.3 V TX) may connect directly to Uno A2. Uno A3 is a 5 V TX output, so route it through a proper logic-level shifter or resistor divider before ESP32 GPIO16. Connect ESP32 GND and Uno GND.

This remapping is for the shredder/mixer/servo bench test, not the final all-actuator machine. With `Servo.h` active, the Uno does not have enough unaffected PWM outputs for the shredder, mixer, press, and variable-speed fan simultaneously. The final system needs a revised allocation, another controller, or a suitable external PWM/servo controller.

## Safety setup

1. Disconnect blades, belts, gears, press linkages, and other loads where possible.
2. Clamp each motor securely and keep hands, clothing, and tools clear.
3. Use a separate correctly rated fuse close to the 12 V source for every motor branch.
4. Provide a reachable physical disconnect or emergency stop that removes dangerous motor power.
5. With 12 V OFF, verify B+, B-, M+, M-, and common-ground wiring.
6. Upload the sketch and confirm `motor test ready` in Serial Monitor before enabling the BTS7960 modules and applying 12 V.
7. Test only one motor at a time.
8. If a motor hums or does not turn, cut power immediately; a stalled motor can draw its full stall current.

Add external 10 kOhm pull-down resistors from every RPWM and LPWM line to GND so the drivers remain off while the Uno resets or boots. Tying R_EN and L_EN permanently to 5 V removes a software-controlled disable path, so the physical emergency disconnect is essential.

### Operator-button expander (`0x20`)

Wire each button contact to the shared controller ground. Start and Reset use normally-open contacts; Stop and Emergency use normally-closed contacts so a broken wire is treated as a fault.

| Control | PCF8574T pin | Contact |
| --- | --- | --- |
| Start | P0 | Normally open |
| Reset | P1 | Normally open |
| Stop | P2 | Normally closed |
| Emergency monitoring | P3 | Normally closed |

This table reflects the current sketch. Older notes listed Start=P3 and Emergency=P4;
confirm the installed connections before changing these pin assignments. Do not
move or bypass an emergency contact just to match a Start-button label.
Opening the Emergency contact cancels movement and latches the software stop.
After releasing Emergency, press Reset before Start can work again.

The PCF8574 Emergency input is monitoring and software shutdown only. A real emergency-stop contact must also interrupt hazardous actuator power or the enable/control circuit of a correctly rated contactor or safety relay. Do not route motor current through a contact block unless it is specifically rated for that load.

## Commands

Open Serial Monitor at 9600 baud:

- `1` pulses the shredder forward.
- `2` pulses the shredder in reverse.
- `3` pulses the mixer forward.
- `4` pulses the mixer in reverse.
- `E` pulses the presser in the extend/down direction.
- `T` pulses the presser in the retract/up direction.
- `F` pulses the pusher forward.
- `B` pulses the pusher backward.
- `G` runs one unloaded timed forming sequence without ESP32, pumping or heating (R first).
- `P` starts normal sensor-controlled forming with a prepared pump dose and ESP32 input; the opt-in legacy bench build retains its old demonstration behavior.
- `O` opens the mixer gate and keeps the servo holding that position.
- `C` closes the mixer gate and keeps the servo holding that position.
- `D` releases the servo signal so it no longer holds the gate.
- `5` opens the molder servo.
- `6` closes the molder servo.
- `7` releases the molder servo signal so it no longer holds position.
- `L` safely tests the status outputs in sequence: green, yellow, red, buzzer, then all indicators. Each step lasts 0.7 seconds, no motor is started, and the normal status display returns automatically.
- `X` forces every BTS7960 PWM input off, cancels pending movement, and latches the software stop.
- `R` arms the motor test initially or at least one second after a software stop.
- `H` prints the command list again.

The ESP32 can also send framed remote commands: `CMD|<id>|RESET`, `CMD|<id>|START`, and `CMD|<id>|STOP`. The Uno returns `ACK` frames and process/mixer/error `EVENT` frames. A repeated command ID is acknowledged without repeating the physical action. Remote Reset only arms/clears the software stop; it does not move anything. Remote Start is rejected until Reset has armed the controller.

Rejected Reset commands now return the specific blocked condition in the final
ACK (for example `door_open`, `emergency_open`, `stop_open`, `chamber_offline`,
or `reset_too_soon`) rather than only `reset_rejected`. Safety checks and the
requirement for an explicit Start are unchanged. See the ESP32 README's
acknowledgement troubleshooting section to trace both directions of the link.

## Operator status lights

The PCF8574T at `0x24` drives active-low inputs on the 5 V relay board. `P0` connects to relay `IN1` for the green Ready/Check lamp, `P1` connects to `IN2` for the yellow Preparing/Running lamp, `P3` connects to `IN4` for the red Stop/Fault lamp, and `P4` connects to `IN5` for the buzzer.

- Green steady: controller armed and ready.
- Yellow blinking: Start was accepted and the gates are moving to their safe starting positions.
- Yellow steady: the automatic process or a manual actuator command is running.
- Green blinking for five seconds: process completed with both gates commanded closed.
- Red steady: software stop is latched and Reset is required.
- Red light and buzzer blinking together: the physical Stop/Emergency contact is open or the `0x20` control expander is unavailable.

The PCF8574 does not power the 12 V lamps. It only controls the 5 V relay inputs; route fused 12 V lamp power through the relay contacts.

The `L` self-test is accepted only while the machine is stopped, the physical Stop contact is healthy, and both I/O expanders are online. A Stop fault or movement cancels the test immediately. The self-test may be used while the normal software stop is latched, so the lamps and buzzer can be checked without arming the motors.

The shredder/mixer speeds and the four actuator pulse durations are separate constants near the top of the sketch. Each direction request first forces every motor output LOW and waits 2.5 seconds. The manual presser pulse defaults to one second and the manual pusher pulse defaults to five seconds at full digital command. The timing is non-blocking, so the sketch continues checking for `X` while a motor is waiting or running.

The operator confirmed all four 5-second presser/pusher travels on 2026-10-31. The sequence still has no position or jam feedback: elapsed time does not prove a completed stroke. Independent travel protection and supervision remain necessary; the paper IR sensors do not replace actuator limit switches.

Preheating is now a separate bounded command (`HH:<run-id>:<seconds>`, maximum
600 seconds), available from the app while forming is stopped. The thermostat
must match `PAPERBRIQ_CHAMBER_TARGET_C` (initially 45 C). The heater fan was
confirmed continuously powered. Fresh DHT data qualifies Ready to load and starts
the drying countdown only after temperature is reached. Door opening, Stop,
emergency contact, stale/invalid temperature and target + 5 C cancel heating;
closing the door does not restart it. Forming/pumping and heating cannot overlap.
See [production timers](../TIMED_CONTROLS.md) for limits and deployment.

The production process uses inlet detection for feeding and timed movements for forming. It has no gate-position, motor-speed, or motor-current feedback, so it cannot detect a blocked gate or motor jam. It remains non-blocking and never starts at boot. After pump calibration, prepare one pump dose in the app, then Reset and Start forming (`R`, then `P` on USB). Its sequence is:

1. Stop and wait 2.5 seconds.
2. Close both the mixer gate and molder, then allow 1.5 seconds for travel.
3. Wait for a fresh clear-to-detected transition at shredder IN. Start shredding, mixing and the prepared pump dose together. While feeding, run the shredder whenever IN detects paper; stop it when IN clears. The mixer alternates directions with the 10s run / 2s OFF pattern above.
4. New paper resumes shredding without restarting the pump. Finish mixing only after IN stays clear and pumping has finished, followed by the 5-second finishing interval. New paper during that interval returns to shredding. Missing sensor heartbeats stop the process and require Reset.
5. Stop the mixer, wait 2.5 seconds, and open the mixer gate while the molder remains closed.
6. Allow 3 seconds for material release, then close the mixer gate.
7. Extend the presser for 5 seconds, pause, and retract it for 5 seconds.
8. Open the molder gate, extend the pusher for 5 seconds, pause, and retract it for 5 seconds.
9. Close the molder gate and keep its servo holding the closed position.

Change the `PROCESS_*_MS` timing constants near the top of the sketch only after an unloaded sequence completes safely. Manual motor and servo commands are rejected while the automatic sequence is running. `X` cancels the entire process at any stage.

The optional standalone bench build, with no production dose prepared, retains a fixed five-second shredder demonstration. This does not change the sensor-controlled production sequence. The inlet sensor does not prove that paper has cleared the blades; check its placement and actual feed behavior before production use. OUT currently reports detection only.

Opening or closing the gate is rejected until the motors have been stopped for 2.5 seconds. If `O` or `C` is sent while a motor is active or waiting, the sketch stops the motor request and asks the operator to wait and resend the gate command.

`X` is a software stop only. With BTS7960 enable inputs hardwired HIGH, it cannot isolate motor power and must never replace the physical 12 V emergency disconnect. Forward and reverse labels may be opposite on the physical motor depending on how M+ and M- are connected.

The current mixer-gate angles are 34 degrees closed and 130 degrees open. Adjust the two angle constants gradually to match the linkage. Never command an angle that makes the servo buzz, stall, or force the gate against a mechanical stop. A real loaded machine should use closed/open limit feedback and enter a latched fault if the gate does not reach its expected position.

The current molder angles are 120 degrees closed and 0 degrees open. Servo angle numbers do not guarantee a physical up/down direction; horn mounting and linkage geometry determine that. Calibrate unloaded in small steps before running the automatic process. The sketch assumes both servos complete movement within their configured 1.5-second travel time; it does not have position feedback.

If the molder servo is jammed against the ceiling, buzzing, hot, or no longer responds, remove servo power and repair or replace it before any automatic test. Do not use software commands to force a mechanically blocked servo.

`D` and `7` explicitly remove servo holding signals. Support the mechanisms before using them. `X` closes and holds the gates only when the door is known closed; an open or unknown door causes no new gate target and does not detach the servos. A servo already moving may still finish its previous command. Boot does not command gate movement.

The current faults cover software state, command protocol, and communication handling. Timer completion does not prove that a motor rotated or a gate reached position. Real jam, overload, and position error notifications require current/RPM sensing and closed/open limit switches wired into the safety logic.

## Session stop diagnosis and Uno memory (2026-11-05)

The generic session stop now reports its triggering condition as a FAULT event:
button board 0x20 lost, chamber board 0x24 lost, door P2 open, Stop/E-stop
contact open, chamber test active, latched Stop, or inconsistent session state.
Existing direct door/UART/button fault events remain in place. A reported input
change does not distinguish wiring noise from a real contact opening; inspect
the reported circuit instead of bypassing the check. Reset never resumes mid-cycle.

The verified build uses 32,078 / 32,256 bytes of flash and 1,398 / 2,048 bytes
of static RAM. Shared flash strings save space without removing commands or
safety checks. A full but successfully compiled flash image alone does not
explain a safety-stop event. Runtime stack space is separate and is not measured
by this build summary. Hardware operation has not been verified by simulation.

### Invalid UART command diagnosis

The Uno discards a single `IR2` report with damaged sensor values, or two
reports joined by one corrupted separator byte. Discarded reports do not
refresh the sensor heartbeat. Invalid command lines still latch Stop.

An `Invalid UART command` fault means the Uno received a completed line that
did not match its accepted command format. The log alone does not identify
whether this was a sender mismatch, lost bytes, or electrical interference.
The Uno now follows that fault with `UART_BAD|<rejected message>` on both USB
and its ESP32 link. The ESP32's existing monitor displays it as
`Uno RX: UART_BAD|...`; only the Uno needs this diagnostic update. Nonprinting
bytes appear as `?`. Stop remains latched; malformed frames are not ignored.

Native tests accept the current DHT/IR/health formats and check missing fields,
extra fields, damaged prefixes and nonprinting bytes. They verify that malformed
frames turn off an active local batch and cannot restart it. These tests do not
measure the physical UART signal or establish the cause of a particular fault.

### Partial UART timeout capture (2026-11-05)

UART command receive lines and `UART RX partial (N bytes): [...]` now print on
the Uno USB monitor even with PAPERBRIQ_VERBOSE_USB_LOGS disabled. Full verbose
logging compiles to 38,012 bytes and cannot fit this Uno sketch; UART-only logging
fits at 32,078 bytes. Leave the full verbose flag at its saved default.

Upload ArduinoUnoMotorTest to the Uno, then open its USB monitor at **9600 baud**.
The ESP32 USB monitor stays at 115200; the inter-board link remains 9600. No
ESP32 edit is needed for this diagnostic. The 750ms incomplete-frame timeout and
latched stop behavior remain unchanged. Capture the brackets from the Uno, since
partial-frame text is printed there. A completed ACK followed by a frame timeout
can be a successful command followed by a separate incomplete message.

For the idle check, isolate actuator power and power both controllers by USB
with the existing UART wiring retained. Do not send Start or Reset. Observe for
about one minute; absence of a fault during that interval is not proof of a
permanent fix. A native test covers completed STOP followed by a partial line,
exact timeout boundary, visible sanitized bytes, and no automatic restart.
