# PaperBriq production controller — Arduino Uno

Open **PaperBriqController.ino** in this folder and select **Arduino Uno**.
Keep the `.ino` and all `.h` files together. This is the Uno sketch to upload
for the actual process. Use **PaperBriqMonitor** on the ESP32.

This production entry point replaces ArduinoUnoMotorTest for normal operation.
ArduinoUnoMotorTest remains a separate diagnostic/calibration sketch; it includes
individual motor, servo, fan and short pump/heater commands. Only one sketch can
be installed on the Uno at a time. Uploading one replaces the other.

## Included behavior

- Physical buttons and app commands use the same Reset, Start and Stop handlers.
- Prepared batch: gates close, wait for new paper at IN, then shred/mix and dose
  water once. Shredding follows IN; a five-second clear interval allows mixing
  to finish after pumping. Pressing/ejection use the checked five-second strokes.
- Separate preheat and drying controls, temperature supervision, door interlock,
  stop latching, local deadlines, and no automatic restart after a stop/reboot.
- Supervised unloaded component checks remain available as an explicit check.
- USB individual actuator jogs and short heater/pump tests are not available.
- Standalone bench mode cannot be compiled into this production entry point.

## Current settings and limits

`ProductionLimits.h` **in this folder** owns the production settings:

- Pump maximum remains **0** until water delivery and batch dose are measured.
  Therefore full automatic forming is still blocked. Use ArduinoUnoMotorTest's
  three-second `Y` pulse for that measurement, then return to this sketch.
- Preheat total ceiling: **600 seconds** including warming/holding.
- Drying ceiling: **600 seconds**, plus at most 600 seconds waiting for target.
- Initial target: **45 C**; set the physical thermostat to match.
- The heater fan was confirmed continuously powered. Heating and forming remain
  mutually exclusive pending a check of combined supply demand.

This separation does not certify machine performance or bypass any operating
limit. See [process and timer details](../TIMED_CONTROLS.md).

## USB monitor — 9600 baud

`R` Reset; `P` Start forming; `X` Stop; `S` request temperature;
`I` door/chamber status; `Q` supervised unloaded component check;
`U` unlock pulse; `K` end unlock; `H` help.

The startup banner identifies **PaperBriqController - PRODUCTION**. Reset never
starts the motors; Start requires the applied pump preparation. Power the
actuators off while uploading. Both boards, server and Android app need their
matching updates for the production/preheat controls.

## Maintenance and verification

Production changes belong in this folder. The diagnostic sketch is retained
separately; changes there do not automatically change this firmware. The timer
protocol header must remain compatible with PaperBriqMonitor. The native Uno
integration harness selects this exact sketch with `PAPERBRIQ_PRODUCTION_BUILD`;
its production and preheat cases exercise the real controller handlers.
