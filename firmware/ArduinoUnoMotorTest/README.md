# PaperBriq Arduino Uno Motor Test

This bench test matches the current split-controller design: the ESP32 handles sensors, TFT, Wi-Fi, and monitoring; the Arduino Uno handles motors, pump, fan, buttons, limits, and safety logic.

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

## Commands

Open Serial Monitor at 115200 baud:

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
- `X` forces every BTS7960 PWM input off, cancels pending movement, and latches the software stop.
- `R` arms the motor test initially or at least one second after a software stop.
- `H` prints the command list again.

The ESP32 can also send framed remote commands: `CMD|<id>|RESET`, `CMD|<id>|START`, and `CMD|<id>|STOP`. The Uno returns `ACK` frames and process/mixer/error `EVENT` frames. A repeated command ID is acknowledged without repeating the physical action. Remote Reset only arms/clears the software stop; it does not move anything. Remote Start is rejected until Reset has armed the controller.

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

The current mixer-gate angles are 52 degrees closed and 130 degrees open. Adjust the two angle constants gradually to match the linkage. Never command an angle that makes the servo buzz, stall, or force the gate against a mechanical stop. A real loaded machine should use closed/open limit feedback and enter a latched fault if the gate does not reach its expected position.

The current molder angles are 120 degrees closed and 0 degrees open. Servo angle numbers do not guarantee a physical up/down direction; horn mounting and linkage geometry determine that. Calibrate unloaded in small steps before running the automatic process. The sketch assumes both servos complete movement within their configured 1.5-second travel time; it does not have position feedback.

If the molder servo is jammed against the ceiling, buzzing, hot, or no longer responds, remove servo power and repair or replace it before any automatic test. Do not use software commands to force a mechanically blocked servo.

`D`, `7`, and `X` remove servo holding signals. Make sure neither mechanism can fall or open dangerously under gravity when holding torque is removed.

The current faults cover software state, command protocol, and communication handling. Timer completion does not prove that a motor rotated or a gate reached position. Real jam, overload, and position error notifications require current/RPM sensing and closed/open limit switches wired into the safety logic.
