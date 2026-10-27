# PaperBriq chamber setup

The active `ArduinoUnoMotorTest/ArduinoUnoMotorTest.ino` includes
`ChamberControl.h` and implements the door input, exhaust fan and lock controls.
The operator confirmed that the emergency button cuts 12 V actuator power and
that component tests will be unloaded and supervised. Software builds do not
verify the physical wiring or stop behavior.

## Confirmed allocation

All three connections are on the LED/status PCF8574 at **0x24**:

| Pin | Connection | Behavior |
| --- | --- | --- |
| P2 | Door interlock contact; other side to shared Arduino GND | Released input; LOW means contact closed |
| P5 | Exhaust fan relay input (allocated IN3) | Active-low command |
| P6 | Lock relay input (allocated IN8) | Active-low, power-to-unlock pulse |
| P7 | Reserved | Held HIGH/off; heater disabled |

Button expander **0x20** remains Start P0, Reset P1, Stop P2, Emergency P3.
The relay board's 5 V coil supply is separate from its switched 12 V loads.
Confirm relay polarity and actual channel wiring before energizing loads.
P2 is always released HIGH in status-expander writes; output updates preserve
that input and the reserved pin.

## Uno Serial Monitor commands

| Command | Action |
| --- | --- |
| `I` | Report door contact, fan command and unlock command |
| `R` | Reset/arm after inspecting the machine; requires healthy inputs |
| `Q` | Run sequential component tests after `R` |
| `X` | Stop/cancel the tests and latch software Stop |
| `V` | Standalone fan test, at most 10 seconds; stopped and settled only |
| `N` | Cancel fan command |
| `U` | Standalone unlock pulse, 1 second; stopped and settled only |
| `K` | Cancel unlock power; does not prove the bolt is engaged |

Unlock pulses have a 5-second start-to-start cooldown. A closed door and healthy
controller inputs are required before motion. The firmware never starts tests
or commands gate movement at boot.

## Component sequence

`Q` and the app's `SELF_TEST` command use the same sequence:

1. Shredder: 1.2 seconds.
2. Mixer: 2 seconds in the installed working direction.
3. Mixer gate: open, then close; 1.5 seconds per target.
4. Molder gate: open, then close; 1.5 seconds per target.
5. Presser: extend, then retract; 1 second per direction.
6. Pusher: forward, then reverse; 1 second per direction.
7. Exhaust fan: 3 seconds.
8. Indicators and buzzer: existing sequential cycle.
9. Lock: 1-second unlock pulse after motion has stopped and settled.

Settling delays occur between stages. The run finishes software-stopped and
requires another Reset before normal motion. Each checklist tick means the
controller completed that timed sequence. There is no current, RPM, position
or lock-bolt feedback to prove physical operation or detect a jam.
The heater is excluded.

## Stop and door behavior

An open door, lost expander, Stop or Emergency cancels an active test and leaves
unfinished items unchecked. Door opening turns off motor commands and cancels
fan operation. Outside a component test, a bounded unlock pulse can remain
active when the door opens. Opening the door does not detach a holding servo or
command a new gate position. A servo already moving may finish its previous
target: the door input is not an independent hardware power disconnect.

With a known closed door, software Stop closes and holds the gates. Physical
emergency power removal is separate. Failed communications cannot force a
failed relay board off. Independent hardware protection and mechanical support
remain necessary; input closure does not prove lock engagement.

## Later drying stage

Adjustable time and temperature remain deferred until the MOSFET and heater
power circuit are verified. The supplied labels specify a 12 V / 100 W heater
(about 8.3 A at rated power), and a 12 V power-to-unlock LY-03B lock whose listing
limits energization to 10 seconds. Firmware uses a 1-second lock pulse.
Supply capacity, starting current, wiring, relay/module capacity, airflow,
over-temperature protection and inductive-load suppression need physical review.

## Validation

Native tests exercise the actual Uno sketch with simulated I/O: the complete
sequence plus interruption at every stage. They also check door debounce,
output masking, pulse limits, cooldown and timer rollover. These tests do not
operate the machine or validate mechanical travel. See [Check machine](MACHINE_CHECK.md)
for deployment and app behavior.
