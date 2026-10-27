# PaperBriq Arduino Uno Motor Test

This bench test uses the ESP32 for sensors, TFT, Wi-Fi, and monitoring and the Uno for motor, gate, button, indicator, door-interlock, fan and lock controls. The active sketch includes `ChamberControl.h`. See [chamber setup](../CHAMBER_SETUP.md) and [Check machine](../MACHINE_CHECK.md) for wiring, commands and the supervised component-test sequence.

Send **S** in Uno Serial Monitor (9600 baud) to request the drying-chamber DHT22
reading from ESP32 GPIO27. No arming or movement is required. See
[DHT chamber check](../DHT_CHAMBER_CHECK.md) for firmware updates and app setup.

## Motor signal wiring

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
| Start | P3 | Normally open |
| Reset | P1 | Normally open |
| Stop | P2 | Normally closed |
| Emergency monitoring | P4 | Normally closed |

P0 is intentionally unused because it did not respond on the installed control expander. Opening the Emergency contact immediately cancels movement, latches the software stop, and commands the gates closed. After releasing the Emergency button, press Reset before Start can work again.

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
- `P` runs the complete timed demonstration process: shred, mix, release into the closed molder, press, open the molder, eject, retract, and close both gates.
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

The presser and pusher are now included in `P` only as an unloaded timed simulation. The sequence has no position or jam feedback. Do not use it with material or production loads until upper/lower presser limit switches and forward/home pusher limit switches are installed and handled as latched safety inputs. Test `E/T`, `5/6`, and `F/B` manually and unloaded before running `P`, and cut physical motor power immediately if a mechanism approaches an end stop.

The automatic process is a timer-based bench demonstration. It has no gate-position, motor-speed, or motor-current feedback, so it cannot detect a blocked gate or motor jam. It remains non-blocking and never starts at boot. Send `R`, then `P`. Its default sequence is:

1. Stop and wait 2.5 seconds.
2. Close both the mixer gate and molder, then allow 1.5 seconds for travel.
3. Run the shredder and mixer together for 5 seconds. The automatic mixer run uses the installed motor's verified D10/LPWM direction (the same direction as manual command `4`).
4. Stop the shredder while the mixer continues without interruption for another 5 seconds.
5. Stop the mixer, wait 2.5 seconds, and open the mixer gate while the molder remains closed.
6. Allow 3 seconds for material release, then close the mixer gate.
7. Extend the presser for 5 seconds, pause, and retract it for 5 seconds.
8. Open the molder gate, extend the pusher for 5 seconds, pause, and retract it for 5 seconds.
9. Close the molder gate and keep its servo holding the closed position.

Change the `PROCESS_*_MS` timing constants near the top of the sketch only after an unloaded sequence completes safely. Manual motor and servo commands are rejected while the automatic sequence is running. `X` cancels the entire process at any stage.

Opening or closing the gate is rejected until the motors have been stopped for 2.5 seconds. If `O` or `C` is sent while a motor is active or waiting, the sketch stops the motor request and asks the operator to wait and resend the gate command.

`X` is a software stop only. With BTS7960 enable inputs hardwired HIGH, it cannot isolate motor power and must never replace the physical 12 V emergency disconnect. Forward and reverse labels may be opposite on the physical motor depending on how M+ and M- are connected.

The current mixer-gate angles are 34 degrees closed and 130 degrees open. Adjust the two angle constants gradually to match the linkage. Never command an angle that makes the servo buzz, stall, or force the gate against a mechanical stop. A real loaded machine should use closed/open limit feedback and enter a latched fault if the gate does not reach its expected position.

The current molder angles are 120 degrees closed and 0 degrees open. Servo angle numbers do not guarantee a physical up/down direction; horn mounting and linkage geometry determine that. Calibrate unloaded in small steps before running the automatic process. The sketch assumes both servos complete movement within their configured 1.5-second travel time; it does not have position feedback.

If the molder servo is jammed against the ceiling, buzzing, hot, or no longer responds, remove servo power and repair or replace it before any automatic test. Do not use software commands to force a mechanically blocked servo.

`D` and `7` explicitly remove servo holding signals. Support the mechanisms before using them. `X` closes and holds the gates only when the door is known closed; an open or unknown door causes no new gate target and does not detach the servos. A servo already moving may still finish its previous command. Boot does not command gate movement.

The current faults cover software state, command protocol, and communication handling. Timer completion does not prove that a motor rotated or a gate reached position. Real jam, overload, and position error notifications require current/RPM sensing and closed/open limit switches wired into the safety logic.
