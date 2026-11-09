#include <assert.h>
#include <stdio.h>
#include "uno-test-stubs/Arduino.h"
#include "uno-test-stubs/Wire.h"
unsigned long testMillis = 0;
int testOutputs[32] = {};
Print Serial;
TwoWire Wire;
#include "../firmware/ArduinoUnoShredderTest/ArduinoUnoShredderTest.ino"

void checkOutputs() {
  for (int p = 3; p <= 12; ++p)
    if (p != 5 && p != 6) assert(testOutputs[p] == LOW);
  assert(!(testOutputs[5] && testOutputs[6]));
  assert(Wire.outputs[0x24] == 0xFF && Wire.outputs[0x20] == 0xFF);
}
void advance(unsigned long duration) {
  for (unsigned long t = 0; t < duration; t += 10) {
    testMillis += 10; loop(); checkOutputs();
  }
}
void command(char c) { Serial.input.push_back(c); loop(); checkOutputs(); }

int main(int argc, char **argv) {
  const std::string fault = argc > 1 ? argv[1] : "none";
  const bool moving = argc > 2 && std::string(argv[2]) == "running";
  const bool voltage = argc > 3 && std::string(argv[3]) == "voltage";
  const unsigned long expectedRun = voltage ? VOLTAGE_CHECK_MS : RUN_MS;
  if (fault == "none") Wire.inputs[0x24] = 0xFF; // Drying door open at startup is allowed.
  setup(); checkOutputs();
  assert(!armed && runState == IDLE);
  advance(100); command('1'); assert(runState == IDLE);
  command('R'); assert(armed && runState == IDLE);
  command(voltage ? 'V' : '1'); assert(runState == WAITING && !testOutputs[5] && !testOutputs[6]);
  assert(activeRunMs == expectedRun);
  advance(SETTLE_MS - 10); assert(runState == WAITING);
  if (moving || fault == "none") {
    advance(10); assert(runState == RUNNING);
    assert(testOutputs[5] == FORWARD_PWM && testOutputs[6] == LOW);
  }
  if (fault != "none") {
    if (fault == "x") command('X');
    else if (fault == "door") Wire.inputs[0x24] |= 1 << 2;
    else if (fault == "stop") Wire.inputs[0x20] |= STOP_MASK;
    else if (fault == "emergency") Wire.inputs[0x20] |= EMERGENCY_MASK;
    else if (fault == "buttons") Wire.missing[0x20] = true;
    else if (fault == "status") Wire.missing[0x24] = true;
    else assert(false);
    advance(10);
    if (fault == "door") {
      assert(armed && runState == RUNNING && testOutputs[5] == FORWARD_PWM);
      advance(expectedRun);
      assert(runState == IDLE && !testOutputs[5] && !testOutputs[6]);
      puts("Drying door excluded; pulse still ends on time"); return 0;
    }
    assert(!armed && runState == IDLE && !testOutputs[5] && !testOutputs[6]);
    Wire.inputs[0x20] = 0xF3; Wire.inputs[0x24] = 0xFB;
    Wire.missing[0x20] = Wire.missing[0x24] = false;
    advance(100); command('2');
    assert(!armed && runState == IDLE);
    puts("Shredder test: fault cancels, no resume without R"); return 0;
  }
  command('2'); command('R'); command('V'); // No queue, reversal or timer extension.
  assert(runState == RUNNING && forwardRequested);
  assert(activeRunMs == expectedRun);
  advance(expectedRun - 10); assert(runState == RUNNING);
  advance(10); assert(runState == IDLE && !testOutputs[5] && !testOutputs[6]);
  advance(10000); assert(runState == IDLE);
  command('2'); advance(SETTLE_MS);
  assert(activeRunMs == RUN_MS); // V must not lengthen later normal checks.
  assert(runState == RUNNING && testOutputs[6] == REVERSE_PWM && !testOutputs[5]);
  advance(RUN_MS); assert(runState == IDLE && !testOutputs[5] && !testOutputs[6]);
  command('X'); command('1'); assert(!armed && runState == IDLE);
  // Other process commands and physical Start do not start any output.
  command('R');
  for (char c : std::string("WGPJYUZ34567EFTB")) command(c);
  Wire.inputs[0x20] &= (uint8_t)~1; advance(200); Wire.inputs[0x20] |= 1;
  assert(runState == IDLE && !testOutputs[5] && !testOutputs[6]);
  puts("Shredder-only: explicit arming, both bounded pulses, settling, no queue, unrelated outputs off");
}
