#include <assert.h>
#include <stdio.h>
#include "uno-test-stubs/Arduino.h"
#include "uno-test-stubs/Wire.h"
unsigned long testMillis = 0;
int testOutputs[32] = {};
Print Serial;
TwoWire Wire;
#include "../firmware/ArduinoUnoPumpTest/ArduinoUnoPumpTest.ino"

void checkOutputs() {
  for (int p = 4; p <= 12; ++p) assert(testOutputs[p] == LOW);
  assert(Wire.outputs[0x20] == 0xFF && Wire.outputs[0x24] == 0xFF);
}
void advance(unsigned long duration) {
  for (unsigned long t = 0; t < duration; t += 10) {
    testMillis += 10; loop(); checkOutputs();
  }
}
void command(char c) { Serial.input.push_back(c); loop(); checkOutputs(); }

int main(int argc, char **argv) {
  const std::string fault = argc > 1 ? argv[1] : "none";
  const bool running = argc > 2 && std::string(argv[2]) == "running";
  const bool longRun = argc > 3 && std::string(argv[3]) == "long";
  const unsigned long expected = longRun ? LONG_RUN_MS : SHORT_RUN_MS;
  Wire.inputs[0x24] = 0xFF; // Drying door open; unrelated to this isolated test.
  setup(); checkOutputs();
  assert(!armed && pumpState == IDLE && testOutputs[3] == LOW);
  advance(100); command('Y'); assert(pumpState == IDLE);
  command('R'); assert(armed && testOutputs[3] == LOW);
  command(longRun ? '2' : 'Y'); assert(pumpState == WAITING && runDuration == expected);
  advance(SETTLE_MS - 10); assert(pumpState == WAITING && testOutputs[3] == LOW);
  if (running || fault == "none") {
    advance(10); assert(pumpState == RUNNING && testOutputs[3] == HIGH);
  }
  if (fault != "none") {
    if (fault == "x") command('X');
    else if (fault == "stop") Wire.inputs[0x20] |= STOP_MASK;
    else if (fault == "emergency") Wire.inputs[0x20] |= EMERGENCY_MASK;
    else if (fault == "buttons") Wire.missing[0x20] = true;
    else if (fault == "status") Wire.missing[0x24] = true;
    else assert(false);
    advance(10);
    assert(!armed && pumpState == IDLE && testOutputs[3] == LOW);
    Wire.inputs[0x20] = 0xF3;
    Wire.missing[0x20] = Wire.missing[0x24] = false;
    advance(REST_MS + 100); command('Y');
    assert(!armed && pumpState == IDLE && testOutputs[3] == LOW);
    puts("Pump fault cancels; no automatic resume"); return 0;
  }
  const auto start = runStarted;
  command('Y'); command('2'); command('R');
  assert(pumpState == RUNNING && runStarted == start && runDuration == expected);
  advance(expected - 10); assert(testOutputs[3] == HIGH);
  advance(10); assert(pumpState == IDLE && testOutputs[3] == LOW);
  command('Y'); assert(pumpState == IDLE); // No restart before rest deadline.
  advance(REST_MS - 10); command('Y'); assert(pumpState == IDLE);
  advance(10); command('1'); assert(pumpState == WAITING && runDuration == SHORT_RUN_MS);
  command('X'); assert(!armed && pumpState == IDLE);
  advance(20000); assert(testOutputs[3] == LOW); // Cancelled wait never starts.
  command('R');
  for (char c : std::string("WGPJUZ34567EFTBV")) command(c);
  Wire.inputs[0x20] &= (uint8_t)~1; advance(200); Wire.inputs[0x20] |= 1;
  assert(pumpState == IDLE && testOutputs[3] == LOW);
  puts("Pump-only: pulse deadline, no extension/queue, rest period, no unrelated actuators");
}
