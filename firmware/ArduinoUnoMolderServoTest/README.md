# PaperBriq Molder Servo Test

This standalone Arduino Uno sketch calibrates only the 180-degree molder servo on A1. It does not control motors, the mixer gate, or the automatic process, and it never moves the servo at startup.

Use an external regulated 5-6 V servo supply and connect its ground to Arduino GND. Remove or loosen the mechanical linkage for the initial test.

Open Serial Monitor at 115200 baud, send `R`, then use `C` to center at 90 degrees. For this linkage, `D` decreases the angle to move downward in 5-degree steps and `U` increases the angle to move upward. `X` releases the servo signal immediately. The initial test range is restricted to 80-120 degrees to avoid the previously observed upper mechanical blockage.
