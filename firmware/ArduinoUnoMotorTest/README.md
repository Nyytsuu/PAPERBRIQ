# PaperBriq Arduino Uno Motor Test

This bench test matches the current split-controller design: the ESP32 handles sensors, TFT, Wi-Fi, and monitoring; the Arduino Uno handles motors, pump, fan, buttons, limits, and safety logic.

## Motor signal wiring

| Motor | BTS7960 input | Arduino Uno |
| --- | --- | --- |
| Shredder | RPWM | D5 |
| Shredder | LPWM | D6 |
| Mixer | RPWM | D3 |
| Mixer | LPWM | D11 |
| Mixer gate servo | Signal | A0 |
| Molder servo | Signal | A1 |

The Uno only supports hardware PWM on D3, D5, D6, D9, D10, and D11. The standard Servo library uses Timer1, so D9 and D10 cannot provide motor PWM while the mixer-gate servo is active. For this test, move mixer RPWM from D8 to D3 and mixer LPWM from D9 to D11. Disconnect the fan ENB from D3 and disconnect both press-driver PWM inputs, including the old D11 connection, before applying motor power.

The mixer-gate servo uses A0 and the molder servo uses A1 as digital signal outputs. Power both servos from a properly rated regulated 5-6 V supply, not from 12 V and not from the Uno regulator under motor load. Size the supply for the combined stall current of both servos. Connect the servo-supply ground, Uno GND, BTS7960 logic GND, and 12 V supply negative to the same common reference without carrying motor or servo supply current through the breadboard.

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
- `P` runs the automatic demonstration process: close gate, shred, mix, open gate.
- `O` opens the mixer gate and keeps the servo holding that position.
- `C` closes the mixer gate and keeps the servo holding that position.
- `D` releases the servo signal so it no longer holds the gate.
- `5` opens the molder servo.
- `6` closes the molder servo.
- `7` releases the molder servo signal so it no longer holds position.
- `X` forces every BTS7960 PWM input off, cancels pending movement, and latches the software stop.
- `R` arms the motor test initially or at least one second after a software stop.
- `H` prints the command list again.

The four speeds and two pulse durations are separate constants near the top of the sketch. Each direction request first forces all PWM inputs LOW and waits 2.5 seconds. Every pulse stops automatically after 1.2 seconds. The timing is non-blocking, so the sketch continues checking for `X` while a motor is waiting or running.

The automatic process is a timer-based bench demonstration. It has no gate-position, motor-speed, or motor-current feedback, so it cannot detect a blocked gate or motor jam. It remains non-blocking and never starts at boot. Send `R`, then `P`. Its default sequence is:

1. Stop and wait 2.5 seconds.
2. Close both the mixer gate and molder, then allow 1.5 seconds for travel.
3. Run the shredder forward for 5 seconds.
4. Stop, wait 2.5 seconds, then run the mixer forward for 10 seconds.
5. Stop, wait 2.5 seconds, open the mixer gate, and leave it holding open. The molder remains closed until command `5` is deliberately sent after the process.

Change `PROCESS_SHREDDER_RUN_MS`, `PROCESS_MIXER_RUN_MS`, and `MIXER_GATE_TRAVEL_MS` near the top of the sketch only after an unloaded sequence completes safely. Manual motor and servo commands are rejected while the automatic sequence is running. `X` cancels the entire process at any stage.

Opening or closing the gate is rejected until the motors have been stopped for 2.5 seconds. If `O` or `C` is sent while a motor is active or waiting, the sketch stops the motor request and asks the operator to wait and resend the gate command.

`X` is a software stop only. With BTS7960 enable inputs hardwired HIGH, it cannot isolate motor power and must never replace the physical 12 V emergency disconnect. Forward and reverse labels may be opposite on the physical motor depending on how M+ and M- are connected.

The current mixer-gate angles are 52 degrees closed and 130 degrees open. Adjust the two angle constants gradually to match the linkage. Never command an angle that makes the servo buzz, stall, or force the gate against a mechanical stop. A real loaded machine should use closed/open limit feedback and enter a latched fault if the gate does not reach its expected position.

The current molder angles are 90 degrees closed and 120 degrees open. The higher opening angle moves this linkage downward. Calibrate them gradually before running the automatic process. The sketch assumes both servos complete movement within their configured 1.5-second travel time; it does not yet have position feedback.

`D`, `7`, and `X` remove servo holding signals. Make sure neither mechanism can fall or open dangerously under gravity when holding torque is removed.
