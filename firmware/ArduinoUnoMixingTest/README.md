# Shredder, mixer, pump and mixer-gate test

Open **ArduinoUnoMixingTest.ino**, select **Arduino Uno** and the current port,
and upload with 12 V actuator power off. This is a separate diagnostic sketch;
existing process and individual test files are unchanged. Only the sketch most
recently uploaded runs on the Uno.

| Part | Existing Uno connection |
| --- | --- |
| Shredder RPWM / LPWM | D5 / D6 |
| Mixer RPWM / LPWM | D9 / D10 (digital output, because Servo uses Timer1) |
| Pump MOSFET TRIG | D3 |
| Mixer-gate servo signal | A0 |
| Button expander | 0x20: Stop P2, emergency P3, both LOW when healthy |
| Status/relay expander | 0x24, kept off; drying-door input unused |
| I2C | A4 SDA / A5 SCL |

Keep the existing driver supplies/enables, common ground, pump suppression and
physical emergency power disconnect. Servo power remains on its appropriately
rated separate buck supply, not the Uno's 5 V pin. No ESP32 is required. The
heater D4, presser D7/D8 and pusher D11/D12 stay LOW. The molder servo on A1 is
never attached. Neither servo is moved at boot; the mixer gate attaches only
after a requested gate check. Uncommanded servos are not held by this sketch.

Test with no paper/material loaded, guards in place, a clear receiving area,
water available to the primed pump, hose secured away from electronics, and a
person supervising beside the physical emergency stop. Combined G runs the
shredder, mixer and pump simultaneously. Stop if the supply sags, a motor stalls,
water escapes, or a gate hits an obstruction. Turning outputs on is not proof
that a motor moves or water flows. Do not change wiring/hose mountings while powered.

Serial Monitor: **9600 baud**. Send **R** to arm, then choose:

| Command | Check |
| --- | --- |
| 1 | Shredder forward, 60% PWM, 5 seconds |
| 2 | Shredder reverse, 30% PWM, 5 seconds |
| 3 | Mixer D9 HIGH / D10 LOW, 5 seconds |
| 4 | Mixer D9 LOW / D10 HIGH, 5 seconds (current process direction) |
| Y | Pump on, 5 seconds |
| O | Mixer gate open, 130 degrees |
| C | Mixer gate closed, 34 degrees |
| G | One short combined sequence described below |
| X | Cancel, turn motors/pump off, require R before restarting |
| I / H | Input status / help |

Every check begins with 2.5 seconds of settling. Commands during another check
are rejected, not queued; no automatic repeat. Pump checks require 10 seconds
of rest after the previous pump stop. Physical Start/Reset are unused.

**G sequence:** close mixer gate -> allow 1.5 seconds of gate travel -> run
shredder forward + mixer in direction 4 + pump together for 5 seconds -> turn
all three off -> settle 2.5 seconds -> open mixer gate -> allow 1.5 seconds of
travel -> hold open 3 seconds -> close gate -> allow 1.5 seconds of travel -> end.
It uses the current process's gate angles and mixer direction, but shorter test
times. It does not execute the full production recipe or measure a water dose.

Stop/E-stop or loss of either expander cancels the test and disarms it. Restoring
inputs never resumes it. The drying-chamber door is excluded from this separate
test only; main process protection is unchanged. Keep the shredder/mixer guards
in place. **X/fault does not command the mixer gate to another position**; an
attached servo continues holding its last target and may finish an existing
movement. Inspect position after interruption. The physical emergency stop
remains the emergency means of disconnecting actuator power.

Settings are at the top of the sketch. Motor/pump test duration is capped at
5 seconds. A separate 30-second action watchdog cancels any overlong sequence;
there is no position, flow, or motor-speed feedback. Do not extend travel time
to force a stalled mechanism. Re-upload your main sketch with actuator power
off after testing, and check all starting positions before resuming production.
