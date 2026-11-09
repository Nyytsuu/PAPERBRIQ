#include <assert.h>
#include <stdio.h>
#include "uno-test-stubs/Arduino.h"
#include "uno-test-stubs/Wire.h"
unsigned long testMillis = 0;
int testOutputs[32] = {};
Print Serial;
TwoWire Wire;
#include "../firmware/ArduinoUnoMotorTest/ArduinoUnoMotorTest.ino"

void advance(unsigned long ms) {
  for (unsigned long n = 0; n < ms; n += 10) { testMillis += 10; loop(); }
}
void command(char c) { Serial.input.push_back(c); loop(); }
bool motorsOff() {
  for (int p : {5,6,7,8,11,12,int(MIXER_RPWM),int(MIXER_LPWM)}) if (testOutputs[p]) return false;
  return true;
}
int main(int argc, char **argv) {
  const std::string mode = argc > 1 ? argv[1] : "normal";
  setup(); advance(3000);
  command('R'); assert(!softwareStopLatched);
  advance(3000); command('O');
  assert(mixerGateServo.angle == MIXER_GATE_OPEN_ANGLE);
  command('Y'); assert(!pumpTestActive); // Gate settling still enforced.
  advance(2500); command('Y');
  assert(pumpTestActive && testOutputs[3] == HIGH && motorsOff());
  const auto writes = mixerGateServo.writes;
  const auto started = pumpTestAt;
  if (mode != "normal") {
    if (mode == "x") command('X');
    else if (mode == "door") Wire.inputs[0x24] |= ChamberControl::DOOR;
    else if (mode == "stop") Wire.inputs[0x20] |= STOP_BUTTON_MASK;
    else if (mode == "emergency") Wire.inputs[0x20] |= EMERGENCY_BUTTON_MASK;
    else if (mode == "buttons") Wire.missing[0x20] = true;
    else if (mode == "status") Wire.missing[0x24] = true;
    else if (mode == "uart") espLink.input.push_back('?');
    else assert(false);
    advance(1000);
    assert(!pumpTestActive && !testOutputs[3] && motorsOff());
    Wire.inputs[0x20] = 0xF3; Wire.inputs[0x24] = 0xFB;
    Wire.missing[0x20] = Wire.missing[0x24] = false;
    advance(20000);
    assert(!pumpTestActive && !testOutputs[3] && motorsOff());
    puts("Open-gate pump: fault stops water; restoring inputs does not restart it");
    return 0;
  }
  // No other motion, heat, gate target, rearm, or batch may start during Y.
  for (char c : std::string("1234EFTBOC567RQWGJP")) command(c);
  assert(pumpTestActive && pumpTestAt == started && motorsOff());
  assert(!testOutputs[4] && !automaticProcessIsActive() && !localCycle.active());
  assert(mixerGateServo.writes == writes && mixerGateServo.angle == MIXER_GATE_OPEN_ANGLE);
  assert(!heaterTestActive && !diagnosticActive);
  advance(pumpDurationMs - 10); command('Y');
  assert(pumpTestActive && pumpTestAt == started);
  advance(10);
  assert(!pumpTestActive && !testOutputs[3] && motorsOff());
  assert(mixerGateServo.writes == writes && mixerGateServo.angle == MIXER_GATE_OPEN_ANGLE);
  // Short interrupted check retains existing 10s-between-starts restriction.
  command('Y'); assert(pumpTestActive);
  command('X'); advance(3000); command('Y'); assert(!pumpTestActive);
  advance(7000); command('Y'); assert(pumpTestActive && softwareStopLatched);
  command('X');
  puts("R -> O -> settle -> Y: water runs 15s with gate held open; timer, exclusions, cooldown retained");
}
