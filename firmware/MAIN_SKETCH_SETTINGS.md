# PaperBriq settings reference

Current wiring update (2026-11-06): mixer RPWM/LPWM are A0/A1.
Mixer-gate and molder servo signals are D9/D10, using PWMServo.
UART on A2/A3 uses NeoSWSerial at 9600 baud.
Saved press durations are 15/13/11s. Older tables below are snapshots.

Read from the saved files on 2026-11-05. These are a snapshot, not measured or
validated travel times/water quantities. The compiled values in your sketch
take precedence over old comments, help messages or this snapshot.

Use Ctrl+F to find the exact setting name. Change one setting at a time, save,
compile, and upload with actuator power off. Only the uploaded sketch changes
machine operation. Changes to a separate test sketch do not change the main one.

Time conversion: 1000 = 1 second; 5000 = 5 seconds; 10000 = 10 seconds;
30000 = 30 seconds; 60000 = 1 minute. Keep the `UL` suffix when present.

## Current physical Start / W timing (2026-11-05)

The shredder, mixer and pump start together after gate closure. Mixing runs
45s, including reversal pauses. The shredder stops with the mixer. Water stays
on through a further 15s closed-gate wait and stops when the gate opens.
Total pump runtime is about 60s per load.
Three loads use press times 18/16/10s and 19s return each. After loads 1/2,
Start/W confirms the next load. Final ejection uses the existing 12s out/back.
M remains the shorter mold-fill check. Settings below include older snapshots.

## Main local sequence: ArduinoUnoMotorTest.ino

These are for W / physical Start in ArduinoUnoMotorTest, not the separate
PaperBriqController production sketch or app timer configuration. G in this main
sketch uses the forming times but no pump/heat. Z uses the presser/pusher timings.

| What you want to adjust | Search for | Saved value | Meaning |
| --- | --- | --- | --- |
| Pump duration during W | `LOCAL_PUMP_RUN_MS` | `65000UL` | 65s total per load; starts with shredder after gate closure, stops at deadline or before pressing. Ceiling 65s; water quantity not calibrated. |
| Shredder process duration | `BENCH_SHREDDER_RUN_MS` | `18000` | 18s shredding after gate closure, before mixing; repeats on all three loads. |
| Additional mixing after shredding | `PROCESS_MIXER_FINISH_MS` | `15000` | 15s for the separate sensor-controlled P recipe, not local W. |
| Material-release wait with mixer gate open | `PROCESS_MATERIAL_RELEASE_MS` | `40000` | 40s, separate from servo travel. This does not extend the independent pump timer. |
| Presser outward/press motor-on duration | `PROCESS_PRESSER_EXTEND_MS` | `19000` | 19s for G/Z; W uses BATCH_PRESS_*_MS below. |
| Mixer first/forward direction | `PROCESS_MIXER_USE_RPWM` | `true` | true starts with manual 3/D9; false starts with manual 4/D10. Reverse uses the opposite output. |
| Mixer forward leg | `BATCH_MIX_FORWARD_MS` | `10000UL` | 10s; two legs for loads 1/3, one for load 2. |
| Mixer reverse leg | `BATCH_MIX_REVERSE_MS` | `2000UL` | 2s; two legs for loads 1/3, none for load 2. |
| Mixer OFF interval | `BATCH_MIX_PAUSE_MS` | `2000UL` | 2s before every direction change. |
| First load press | `BATCH_PRESS_FIRST_MS` | `18000UL` | 18s forward command. |
| Second load press | `BATCH_PRESS_SECOND_MS` | `16000UL` | 16s forward command. |
| Third load press | `BATCH_PRESS_THIRD_MS` | `10000UL` | 10s forward command. |
| Shredder forward PWM | `SHREDDER_FORWARD_DUTY` | `153` | About 60% duty; affects process and manual forward. |
| Shredder reverse PWM | `SHREDDER_REVERSE_DUTY` | `77` | About 30% duty for reverse checks. Does not insert reverse intervals in the process. |

W / physical Start waits for another Start after loads 1 and 2 have pressed
and retracted. The pump and shredder start together after gate closure. The molder remains closed. Load 3 completes before ejection and
drying. Every press uses the existing 19s return; pusher remains 12s out/back.
Manual 3/4 retain individual one-direction pulses. See the sketch README for
preheat confirmation, the full pattern and stop behavior.

PWM values range from 0 to 255 (0 off, 255 continuous command); they do not
measure RPM or guarantee torque. Do not increase a command to force through a jam.
`MIXER_FORWARD_DUTY` and `MIXER_REVERSE_DUTY` are old unused constants in this
sketch. Changing them will NOT change mixer speed: D9/D10 are driven digitally
because the Servo library occupies their PWM timer.

The pump's single MOSFET provides ON/OFF control only. There is no pump-reverse
setting. Reversing pump flow cannot be added just by changing a time or boolean;
it depends on pump design and appropriate hardware.

## Retraction, pusher travel and heating: StandaloneCycle.h tab

This file is in the same ArduinoUnoMotorTest folder. The main .ino's
PROCESS_PRESSER_RETRACT_MS, PROCESS_PUSHER_EXTEND_MS and PROCESS_PUSHER_RETRACT_MS
refer to these settings. Edit the source values here, not the derived copies.

| What you want to adjust | Search for | Saved value |
| --- | --- | --- |
| Presser retract motor-on duration | `PAPERBRIQ_PRESSER_RETURN_MS` | `19000UL` = 19s |
| Pusher outward motor-on duration | `PAPERBRIQ_PUSHER_FROM_PARK_MS` | `12000UL` = 12s |
| Pusher return motor-on duration | `PAPERBRIQ_PUSHER_RETURN_TO_PARK_MS` | `12000UL` = 12s |
| Post-forming drying window | `PAPERBRIQ_LOCAL_HEAT_MS` | `600000UL` = 10min, maximum 10min |
| Preheat confirmation timeout | `PAPERBRIQ_LOCAL_PREHEAT_MAX_MS` | `600000UL` = 10min, maximum 10min |
| Warm standby timeout after drying | `PAPERBRIQ_LOCAL_WARM_IDLE_MS` | `120000UL` = 2min, maximum 2min |
| Absolute heating-session timeout | `PAPERBRIQ_LOCAL_SESSION_MAX_MS` | `1800000UL` = 30min, maximum 30min |

Travel is timed, not measured. Equal outward/return times do not guarantee the
same parked position. Check clearance and actual position before repeating.
Stop if an actuator reaches an end and strains; a longer time is not a substitute
for position feedback. Preheat time is a timeout waiting for operator confirmation,
not automatic proof that the setpoint has been reached. Drying time does not prove
dryness. Set standalone heating temperature on the external thermostat itself;
the Uno cannot read that controller's temperature/setpoint. Heater enable pauses
during forming with the current shared-supply arrangement.

## Gate positions and travel allowance: main .ino

| Search for | Saved value | Meaning |
| --- | --- | --- |
| `MIXER_GATE_OPEN_ANGLE` | `130` | Mixer gate open command, degrees |
| `MIXER_GATE_CLOSED_ANGLE` | `34` | Mixer gate closed command, degrees |
| `MOLDER_OPEN_ANGLE` | `0` | Molder gate open command, degrees |
| `MOLDER_CLOSED_ANGLE` | `120` | Molder gate closed command, degrees |
| `MIXER_GATE_TRAVEL_MS` | `1500` | 1.5s wait for commanded mixer-gate travel |
| `MOLDER_TRAVEL_MS` | `1500` | 1.5s wait for commanded molder-gate travel |

Angles set targets, not measured positions. Adjust in small steps within the
mechanism's actual free travel. Travel waits do not change servo speed and do not
confirm a gate reached its target. Keep the direction-change settling pause and
Stop/E-stop/door checks; do not change them to overcome a blocked mechanism.

## Individual commands in ArduinoUnoMotorTest

These do NOT set W's motor/pump durations.

| Command | Search for | Saved value |
| --- | --- | --- |
| 1 / 2 shredder forward/reverse | `SHREDDER_PULSE_MS` | `16000` = 16s |
| 3 / 4 mixer directions | `MIXER_PULSE_MS` | `15000` = 15s |
| E / T presser extend/retract | `PRESSER_PULSE_MS` | `16000` = 16s |
| F / B pusher outward/return | `PUSHER_PULSE_MS` | `23000` = 23s |
| Y pump check | `pumpDurationMs` | `15000UL` = 15s |

## Separate test sketches

These files have their own settings; editing them does not update ArduinoUnoMotorTest.

| Sketch | Search for | Saved value / scope |
| --- | --- | --- |
| ArduinoUnoPumpTest | `SHORT_RUN_MS` | 5000UL; Y/1 = 5s |
| ArduinoUnoPumpTest | `LONG_RUN_MS` | 30000UL; 2 = 30s (some old messages still say 15s) |
| ArduinoUnoShredderTest | `RUN_MS` | 5000UL; 1/2 = 5s |
| ArduinoUnoShredderTest | `VOLTAGE_CHECK_MS` | 30000UL; V = 30s (some old messages still say 15s) |
| ArduinoUnoShredderTest | `FORWARD_PWM` / `REVERSE_PWM` | 153 / 77 |
| ArduinoUnoMixingTest | `RUN_MS` | 5000UL; shared 5s motor/pump checks and combined powered stage, capped at 5s |
| ArduinoUnoMixingTest | `COMBINED_MIX_RPWM` | false; true = D9/manual 3, false = D10/manual 4 |
| ArduinoUnoMixingTest | `GATE_OPEN` / `GATE_CLOSED` | 130 / 34 degrees |
| ArduinoUnoMixingTest | `GATE_OPEN_HOLD_MS` | 3000UL; gate-open hold of 3s in G |

Keep guards in place and supervise beside the physical emergency stop. Water
tests need a supplied/primed pump, secure hose and a receiving container. Isolate
power before wiring or repositioning hoses/mechanisms. None of these sketches
confirms motion, position, flow, dryness or pressure merely from its command log.
