# Shredder-only Uno test

Open `ArduinoUnoShredderTest.ino` in Arduino IDE, select **Arduino Uno** and its
current port, then upload **with the machine's 12 V actuator power off**. Uploading
replaces the active Uno program; the original process files are not modified.
Disconnect the overheated/faulty ESP32 and its signal wires.

This test retains the existing wiring:

| Connection | Pin |
| --- | --- |
| Shredder BTS7960 RPWM | Uno D5 |
| Shredder BTS7960 LPWM | Uno D6 |
| Button PCF8574 | I2C 0x20; Stop P2 and E-stop P3, LOW when healthy |
| Status PCF8574 | I2C 0x24; relay outputs kept off; drying-door P2 ignored |
| I2C | Uno A4 SDA, A5 SCL |

Keep the driver's existing regulated 5 V logic supply, enable connections,
common ground, motor leads, and fused/emergency-disconnected 12 V supply.
This sketch does not control the hardwired R_EN/L_EN inputs. USB does not supply
the motor's 12 V power. Physical emergency stop must remain functional.

With the shredder unloaded, guards closed, and someone supervising beside the
physical emergency stop, enable its motor supply and open Serial Monitor at
**9600 baud**:

1. Send **I**: both boards must report online and Stop/E-stop closed. The drying-chamber door does not block this test.
2. Send **R**: arms only; nothing moves.
3. Send **1**: waits 2.5 seconds, then commands forward for **5 seconds** at 60% PWM.
4. Once finished, send **2** for reverse: the same pause and duration, at 30% PWM.
5. **X** immediately cancels pending/running motion and requires R to arm again.
6. **H** prints help. Repeated direction commands during a test are rejected,
   not queued. There is no automatic repeat and no motion at startup.

### Longer voltage-reading window

**R**, then **V** commands forward at the same 60% PWM for **15 seconds**, after
the usual 2.5-second settling pause. X/physical Stop/E-stop still cancels it.
There is no continuous-on mode; V cannot extend or restart a running pulse.
The next 1/2 check still uses five seconds. `VOLTAGE_CHECK_MS` is capped at 15000.

The 12 V input at driver B+/B- can be measured with the motor stopped; V is only
needed to compare voltage while an output is commanded. Use DC-voltage mode with
the leads in COM/V, not the meter's current terminals. Secure probes with power
off when possible and keep hands/leads clear of the mechanism. Do not move or
tighten wiring while powered. Stop immediately if the motor stalls or hums;
do not leave a non-rotating motor energized for the whole measurement window.
Motor terminals M+/M- are PWM-controlled, so their meter reading need not be 12 V.

Physical Stop/E-stop or either missing expander stops the test and
disarms it. Contact restoration never restarts it. Inputs must be healthy for
50 ms before arming. Expander failure handling uses bounded I2C transactions.
Physical Start/Reset are unused in this isolated test; use serial R/1/2.
The drying-chamber door is excluded only in this shredder-only sketch; the main
process sketch retains its door protection. This does not authorize opening or
removing the shredder's own guards. Both expanders remain required so the test
can keep unrelated relay outputs commanded off.
All status/relay outputs are commanded off. Uno D3/D4 and D7-D12 stay low;
neither servo is attached or commanded. Existing servo positions are not held
by this sketch. Keep the rest of the machine unloaded and supported as needed.

`RUN_MS`, `FORWARD_PWM`, and `REVERSE_PWM` are near the top. The default five-second
pulse is shorter than the process sketch's eleven-second manual check. PWM settings
match that sketch; they are commanded duty cycles, not measured RPM. The pulse
has an eleven-second compile-time ceiling. Do not increase speed to overcome a jam.

The messages report commands and elapsed time, **not measured motor movement**.
If it remains still, stop the check and investigate the supply, enables, driver,
motor wiring or mechanism. Do not keep a humming/stalled motor energized.

To return to the process, upload `ArduinoUnoMotorTest` (or the appropriate
production sketch) again with actuator power off, then inspect actuator positions
before starting. This test does not run pump, preheat, mixing, pressing, or ejection.
