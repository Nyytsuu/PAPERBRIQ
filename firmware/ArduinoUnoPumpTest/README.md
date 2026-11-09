# Pump-only hose and shower test

Open **ArduinoUnoPumpTest.ino**, select **Arduino Uno** and its current port,
then upload with the machine's 12 V power off. The original process and shredder
sketches remain separate, but only the uploaded sketch runs on the Uno.

Keep the existing 12 V pump/MOSFET wiring and installed pump suppression diode:

| Connection | Destination |
| --- | --- |
| Pump MOSFET TRIG | Uno digital D3 |
| MOSFET signal GND | Uno common GND |
| MOSFET VIN+ / VIN- | Existing fused, emergency-disconnected 12 V supply |
| MOSFET OUT+ / OUT- | Pump positive / negative |
| Stop / E-stop contacts | PCF8574 0x20 P2 / P3, LOW when healthy |
| I2C | Uno A4 SDA / A5 SCL |

PCF8574 0x24 must remain reachable so unrelated relays can be commanded off.
The drying-chamber door is not used for this isolated hose check. The main
process's door protection is unchanged. Heater D4 and all motor inputs D5-D12
stay LOW. Neither servo is attached or moved; prior gate positions are not held.
Physical Start/Reset are unused. The faulty ESP32 is not needed; leave it disconnected.

Before powering the pump, provide water/prime it as its instructions require.
Mount the shower head facing downward and secure the hose with power off.
Aim into a suitable container or the intended receiving area, away from exposed
electronics. Check for leaks and stop if water runs out or the outlet is blocked.
There is no flow, water-level or pressure feedback in this sketch.

Open Serial Monitor at **9600 baud**, with a person supervising beside the
physical emergency stop:

1. **I** shows board and Stop/E-stop status.
2. **R** arms; nothing starts by itself.
3. **Y** or **1** runs the pump for **5 seconds**, after a 2.5-second start delay.
4. **2** runs it for **15 seconds**, with the same delay.
5. **X** cancels immediately and requires R before restarting.
6. Wait **10 seconds after the pump stops** before another run. H shows help.

Duplicate commands never extend or queue a run. Physical Stop/E-stop or an
expander failure stops and disarms the pump. Restoring contacts does not restart
it. The independent physical emergency stop still cuts 12 V power.

Use X, isolate power and let residual pressure release before repositioning or
disconnecting the hose/nozzle. The code cannot change the shower's direction;
that is a physical mounting adjustment. This is a timed check, not a measured
water dose. Collecting discharge for a known interval can help measure flow.

`SHORT_RUN_MS` and `LONG_RUN_MS` are near the top of the sketch; neither can exceed
15000 ms. There is no continuous-on mode or automatic repeat. Re-upload the main
process sketch with actuator power off when hose testing is finished.
