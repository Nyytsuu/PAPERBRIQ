#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "uno-test-stubs/Arduino.h"
#include "uno-test-stubs/Wire.h"
unsigned long testMillis = 0;
int testOutputs[32] = {};
Print Serial;
TwoWire Wire;
#include "../firmware/ArduinoUnoMixingTest/ArduinoUnoMixingTest.ino"

void checkOutputs() {
  for (int p : {4, 7, 8, 11, 12}) assert(testOutputs[p] == LOW);
  assert(!(testOutputs[5] && testOutputs[6]));
  assert(!(testOutputs[9] && testOutputs[10]));
  assert(Wire.outputs[0x20] == 0xFF && Wire.outputs[0x24] == 0xFF);
}
bool outputsIdle() {
  return !testOutputs[3] && !testOutputs[5] && !testOutputs[6] &&
         !testOutputs[9] && !testOutputs[10];
}
void advance(unsigned long duration) {
  for (unsigned long t = 0; t < duration; t += 10) {
    testMillis += 10; loop(); checkOutputs();
  }
}
void command(char c) { Serial.input.push_back(c); loop(); checkOutputs(); }

int main(int argc, char **argv) {
  const std::string mode = argc > 1 ? argv[1] : "combined";
  const std::string fault = argc > 2 ? argv[2] : "none";
  const int target = argc > 3 ? atoi(argv[3]) : COMBINED_RUN;
  Wire.inputs[0x24] = 0xFF; // Drying chamber door is irrelevant here.
  setup(); assert(!armed && state == IDLE && !mixerGate.attached());
  advance(100); command('G'); assert(state == IDLE && outputsIdle());
  command('R'); assert(armed && !mixerGate.attached());
  if (mode == "manual") {
    const char commands[] = {'1','2','3','4','Y','O','C'};
    const int pins[] = {5,6,9,10,3,-1,-1};
    for (int i = 0; i < 7; ++i) {
      advance(PUMP_REST_MS);
      command(commands[i]); assert(state == SETTLING && outputsIdle());
      advance(SETTLE_MS);
      if (pins[i] >= 0) {
        assert(state == MANUAL_RUN && testOutputs[pins[i]] != LOW);
        for (int p : {3,5,6,9,10}) if (p != pins[i]) assert(testOutputs[p] == LOW);
        command('G'); command('Y'); assert(action == commands[i]);
        advance(RUN_MS - 10); assert(state == MANUAL_RUN);
        advance(10);
      } else {
        assert(state == MANUAL_GATE && outputsIdle());
        assert(mixerGate.angle == (commands[i] == 'O' ? GATE_OPEN : GATE_CLOSED));
        advance(GATE_TRAVEL_MS);
      }
      assert(state == IDLE && outputsIdle());
    }
    // Pump rest prevents a new pump check even if R is resent.
    command('Y'); advance(SETTLE_MS + RUN_MS); command('R'); command('G');
    assert(state == IDLE && outputsIdle());
    puts("Individual checks: correct pins/angles, no overlap, bounded pulses, pump rest"); return 0;
  }
  command('G'); assert(state == SETTLING);
  bool visited[10] = {};
  unsigned long poweredMs = 0;
  for (int n = 0; n < 4000 && state != IDLE; ++n) {
    visited[state] = true;
    if (state == COMBINED_RUN) {
      assert(testOutputs[3] == HIGH && testOutputs[5] == SHRED_FORWARD_PWM);
      assert(testOutputs[6] == LOW && testOutputs[9] == LOW && testOutputs[10] == HIGH);
      assert(mixerGate.angle == GATE_CLOSED);
      poweredMs += 10;
    } else assert(outputsIdle());
    if (state == OPENING || state == OPEN_HOLD) assert(mixerGate.angle == GATE_OPEN);
    if (state == CLOSING) assert(mixerGate.angle == GATE_CLOSED);
    if (fault != "none" && state == target) {
      const auto writes = mixerGate.writes;
      if (fault == "x") command('X');
      else if (fault == "stop") Wire.inputs[0x20] |= STOP_MASK;
      else if (fault == "emergency") Wire.inputs[0x20] |= EMERGENCY_MASK;
      else if (fault == "buttons") Wire.missing[0x20] = true;
      else if (fault == "status") Wire.missing[0x24] = true;
      else if (fault == "timeout") testMillis = actionAt + ACTION_LIMIT_MS;
      else assert(false);
      advance(10);
      assert(!armed && state == IDLE && outputsIdle() && mixerGate.writes == writes);
      Wire.inputs[0x20] = 0xF3;
      Wire.missing[0x20] = Wire.missing[0x24] = false;
      advance(20000); command('G'); assert(state == IDLE && !armed && outputsIdle());
      puts("Combined fault: pump/motors off, no new gate target, Reset required"); return 0;
    }
    const auto before = state;
    for (char c : std::string("1234YOCGR")) command(c);
    assert(state == before); // No queued movement or reset of timer.
    advance(10);
  }
  assert(fault == "none" && state == IDLE && outputsIdle() && poweredMs == RUN_MS);
  for (int s : {SETTLING,CLOSE_BEFORE_RUN,COMBINED_RUN,SETTLE_BEFORE_OPEN,OPENING,OPEN_HOLD,CLOSING})
    assert(visited[s]);
  assert(mixerGate.angle == GATE_CLOSED);
  const auto writes = mixerGate.writes;
  advance(30000); assert(state == IDLE && outputsIdle() && mixerGate.writes == writes);
  command('X'); command('G'); assert(state == IDLE && !armed);
  puts("Combined check: close/run/stop/settle/open/hold/close, exact 5s, no auto repeat");
}
