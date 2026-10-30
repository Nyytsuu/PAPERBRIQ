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
  for(unsigned long n=0;n<ms;n+=10) { testMillis+=10; loop(); }
}
bool motorsOff() {
  for(int p=5;p<=12;p++) if(testOutputs[p]) return false;
  return true;
}
int main(int argc, char **argv) {
  const std::string mode=argc>1?argv[1]:"full";
  setup();
  assert(motorsOff());
  assert(!mixerGateServo.attached() && !molderServo.attached());
  assert(!diagnosticActive && !chamber.fanOn && !chamber.unlockOn);
  assert(testOutputs[HEATER_ENABLE_PIN] == LOW);
  assert(testOutputs[PUMP_ENABLE_PIN] == LOW && !pumpTestActive);
  if (mode == "production_disabled") {
    advance(3000);
    char p[]="CMD|disabled-pump|PP:1234ABCD:60";
    char d[]="CMD|disabled-dry|DD:2345ABCD:1800";
    handleRemoteCommandFrame(p); handleRemoteCommandFrame(d);
    handlePaperSignal('0'); advance(250); handlePaperSignal('1');
    assert(!production.active() && testOutputs[3]==LOW && testOutputs[4]==LOW);
    assert(espLink.output.find("production_blocked")!=std::string::npos);
    puts("Default build: uncommissioned production requests cannot enable outputs"); return 0;
  }
  if (mode == "production") {
    const auto cmd = [](const char *s) { std::string line(s); handleRemoteCommandFrame(&line[0]); };
    advance(3000);
    cmd("CMD|dry-start|DD:1234ABCD:60");
    assert(production.on() && testOutputs[4] == HIGH && motorsOff());
    assert(!armMotorTest() && !startAutomaticProcess() && !startComponentTest());
    advance(2000);
    cmd("CMD|dry-edit|DT:1234ABCD:90"); assert(production.total == 90 && production.elapsed(millis()) == 2);
    cmd("CMD|dry-stale|DT:1234ABCE:100"); assert(production.total == 90);
    cmd("CMD|dry-over|DT:1234ABCD:3601"); assert(production.total == 90);
    cmd("CMD|dry-shorten|DT:1234ABCD:1"); assert(!production.on() && testOutputs[4] == LOW);
    cmd("CMD|dry-replay|DD:1234ABCD:60"); assert(!production.active());
    cmd("CMD|pump-arm|PP:2345ABCD:20"); assert(production.phase == ProductionTimer::WAITING && testOutputs[3] == LOW);
    handlePaperSignal('0'); advance(250); handlePaperSignal('1'); assert(!production.on());
    assert(armMotorTest()); assert(startAutomaticProcess());
    for (int i=0;i<2000 && automaticProcessState!=PROCESS_SHREDDING_AND_MIXING;++i) advance(10);
    assert(automaticProcessState==PROCESS_SHREDDING_AND_MIXING);
    handlePaperSignal('1'); assert(!production.on());
    handlePaperSignal('0'); advance(1600); handlePaperSignal('1'); assert(!production.on());
    handlePaperSignal('0'); advance(250); handlePaperSignal('1');
    assert(production.on() && testOutputs[3] == HIGH);
    const auto startAt = production.startedAt;
    advance(11000); assert(automaticProcessState==PROCESS_MIXER_FINISHING && production.on());
    handlePaperSignal('0'); advance(250); handlePaperSignal('1'); assert(production.startedAt == startAt);
    cmd("CMD|pump-edit|PT:2345ABCD:12");
    advance(1000); assert(!production.on() && testOutputs[3] == LOW);
    cmd("CMD|prod-stop|STOP"); assert(motorsOff() && !production.active());
    for (int fault=0;fault<6;++fault) {
      advance(3000);
      std::string line = "CMD|fault-" + std::to_string(fault) + "|DD:3456ABC" + std::to_string(fault) + ":60";
      handleRemoteCommandFrame(&line[0]); assert(testOutputs[4] == HIGH);
      if(fault==0) Wire.inputs[0x24] |= ChamberControl::DOOR;
      if(fault==1) Wire.inputs[0x20] |= STOP_BUTTON_MASK;
      if(fault==2) Wire.inputs[0x20] |= EMERGENCY_BUTTON_MASK;
      if(fault==3) Wire.missing[0x24] = true;
      if(fault==4) Wire.missing[0x20] = true;
      if(fault==5) { espLink.input.push_back('?'); serviceBridgeCommands(); advance(750); }
      advance(150); assert(testOutputs[4] == LOW && !production.active());
      Wire.inputs[0x24] &= ~ChamberControl::DOOR;
      Wire.inputs[0x20] &= ~(STOP_BUTTON_MASK|EMERGENCY_BUTTON_MASK);
      Wire.missing[0x24] = Wire.missing[0x20] = false;
    }
    advance(3000); cmd("CMD|no-paper|PP:4567ABCD:20");
    assert(production.phase == ProductionTimer::WAITING);
    assert(armMotorTest()); assert(startAutomaticProcess());
    advance(20000);
    assert(softwareStopLatched && !production.active() && motorsOff() && testOutputs[3] == LOW);
    puts("Production integration: actual sketch handles IR batch, mixer hold, live edits, replay, limits and hardware stop paths");
    return 0;
  }
  if (mode == "timed_remote") {
    assert(PAPERBRIQ_STANDALONE_BENCH);
    advance(3000);
    for (const char *invalid : {"HEATER:0", "HEATER:11", "HEATER:01", "HEATER:1.0", "PUMP:4", "PUMP:-1", "PUMP:1x", "PUMP:"}) {
      assert(timedCommandSeconds(invalid) == 0);
      std::string frame = "CMD|invalid-" + std::string(invalid).substr(0, 4) + "|" + invalid;
      handleRemoteCommandFrame(&frame[0]);
      assert(!heaterTestActive && !pumpTestActive && testOutputs[3] == LOW && testOutputs[4] == LOW);
    }
    for (int n = 1; n <= 10; ++n) {
      advance(60000);
      std::string frame = "CMD|heat-" + std::to_string(n) + "|HEATER:" + std::to_string(n);
      std::string replay = frame;
      handleRemoteCommandFrame(&frame[0]);
      assert(heaterTestActive && heaterDurationMs == (unsigned)n * 1000);
      advance(n * 1000 - 10); assert(heaterTestActive);
      handleRemoteCommandFrame(&replay[0]); // Same ID cannot extend the run.
      advance(10); assert(!heaterTestActive && testOutputs[4] == LOW);
    }
    for (int n = 1; n <= 3; ++n) {
      advance(10000);
      std::string frame = "CMD|pump-" + std::to_string(n) + "|PUMP:" + std::to_string(n);
      handleRemoteCommandFrame(&frame[0]); assert(pumpTestActive);
      char overlapping[] = "CMD|overlap|HEATER:10";
      handleRemoteCommandFrame(overlapping); assert(!heaterTestActive);
      advance(n * 1000 - 10); assert(pumpTestActive);
      advance(10); assert(!pumpTestActive && testOutputs[3] == LOW);
    }
    advance(60000);
    char start[] = "CMD|final-heat|HEATER:10"; handleRemoteCommandFrame(start);
    char stop[] = "CMD|cancel|STOP"; handleRemoteCommandFrame(stop);
    assert(!heaterTestActive && testOutputs[4] == LOW);
    puts("Timed UART: strict limits, every duration, duplicate replay, mutual exclusion and STOP passed");
    return 0;
  }
  if (mode == "standalone_bench") {
    assert(PAPERBRIQ_STANDALONE_BENCH && !PAPERBRIQ_HEATER_VERIFIED && !PAPERBRIQ_PUMP_VERIFIED);
    advance(3000);
    assert(!heaterReadingValid && espLink.input.empty());
    Serial.input.push_back('J'); serviceSerialCommands();
    assert(heaterTestActive && testOutputs[4] == HIGH && motorsOff());
    startPumpTest(); assert(!pumpTestActive);
    assert(!armMotorTest() && !startAutomaticProcess() && !startComponentTest());
    advance(9900);
    assert(heaterTestActive && testOutputs[4] == HIGH);
    startHeaterTest(); advance(110);
    assert(!heaterTestActive && testOutputs[4] == LOW);
    startHeaterTest(); assert(!heaterTestActive); // 60s between starts.
    advance(49980); startHeaterTest(); assert(!heaterTestActive);
    advance(20); startHeaterTest(); assert(heaterTestActive);
    Serial.input.push_back('X'); serviceSerialCommands();
    assert(!heaterTestActive && testOutputs[4] == LOW);
    advance(3000);
    Serial.input.push_back('Y'); serviceSerialCommands();
    assert(pumpTestActive && testOutputs[3] == HIGH);
    startHeaterTest(); assert(!heaterTestActive);
    advance(2900); startPumpTest(); advance(110);
    assert(!pumpTestActive && testOutputs[3] == LOW);
    startPumpTest(); assert(!pumpTestActive);
    for (int output = 0; output < 2; ++output) {
      for (int fault = 0; fault < 6; ++fault) {
        advance(60000);
        if (output == 0) startHeaterTest(); else startPumpTest();
        assert(testOutputs[output == 0 ? 4 : 3] == HIGH);
        if (fault == 0) Wire.inputs[0x24] |= ChamberControl::DOOR;
        if (fault == 1) Wire.inputs[0x20] |= STOP_BUTTON_MASK;
        if (fault == 2) Wire.inputs[0x20] |= EMERGENCY_BUTTON_MASK;
        if (fault == 3) Wire.missing[0x20] = true;
        if (fault == 4) Wire.missing[0x24] = true;
        if (fault == 5) { espLink.input.push_back('?'); serviceBridgeCommands(); advance(750); }
        advance(10);
        assert(testOutputs[3] == LOW && testOutputs[4] == LOW && motorsOff());
        Wire.inputs[0x24] &= (uint8_t)~ChamberControl::DOOR;
        Wire.inputs[0x20] &= (uint8_t)~(STOP_BUTTON_MASK | EMERGENCY_BUTTON_MASK);
        Wire.missing[0x20] = false; Wire.missing[0x24] = false;
        initializeControlIo(); initializeStatusIo(); advance(100);
      }
    }
    puts("Standalone bench: no ESP32 required; pulses/cooldowns, exclusions and all stop paths passed");
    return 0;
  }
  if (mode == "pump_blocked") {
    advance(3000);
    Serial.input.push_back('Y'); serviceSerialCommands();
    assert(!pumpTestActive && testOutputs[3] == LOW && motorsOff());
    assert(Serial.output.find("wiring/suppression verification pending") != std::string::npos);
    puts("Pump default build: unverified hardware cannot energize D3");
    return 0;
  }
  if (mode == "pump") {
    assert(PAPERBRIQ_PUMP_VERIFIED && PAPERBRIQ_HEATER_VERIFIED);
    startPumpTest(); assert(!pumpTestActive); // Mechanical settling.
    advance(3000);
    Serial.input.push_back('Y'); serviceSerialCommands();
    assert(pumpTestActive && testOutputs[3] == HIGH && motorsOff() && testOutputs[4] == LOW);
    assert(!armMotorTest() && strcmp(lastArmRejectReason, "pump_active") == 0);
    assert(!startAutomaticProcess() && !startComponentTest());
    assert(!runChamberCommand("UNLOCK") && !runChamberCommand("FAN_TEST"));
    heaterReadingValid = true; heaterReadingAt = millis(); heaterTemperature = 25;
    startHeaterTest(); assert(!heaterTestActive && testOutputs[4] == LOW);
    advance(2900); startPumpTest(); advance(110);
    assert(!pumpTestActive && testOutputs[3] == LOW);
    startPumpTest(); assert(!pumpTestActive); // Cooldown.
    advance(7000); startPumpTest(); assert(pumpTestActive);
    Serial.input.push_back('X'); serviceSerialCommands();
    assert(!pumpTestActive && testOutputs[3] == LOW && softwareStopLatched);
    advance(10000);
    heaterReadingValid = true; heaterReadingAt = millis(); heaterTemperature = 25;
    startHeaterTest(); assert(heaterTestActive);
    startPumpTest(); assert(!pumpTestActive && testOutputs[3] == LOW);
    stopHeaterTest();
    for (int fault = 0; fault < 6; ++fault) {
      advance(10000); startPumpTest(); assert(pumpTestActive);
      if (fault == 0) Wire.inputs[0x24] |= ChamberControl::DOOR;
      if (fault == 1) Wire.inputs[0x20] |= STOP_BUTTON_MASK;
      if (fault == 2) Wire.inputs[0x20] |= EMERGENCY_BUTTON_MASK;
      if (fault == 3) Wire.missing[0x20] = true;
      if (fault == 4) Wire.missing[0x24] = true;
      if (fault == 5) { espLink.input.push_back('?'); serviceBridgeCommands(); advance(750); }
      advance(10);
      assert(!pumpTestActive && testOutputs[3] == LOW && motorsOff());
      Wire.inputs[0x24] &= (uint8_t)~ChamberControl::DOOR;
      Wire.inputs[0x20] &= (uint8_t)~(STOP_BUTTON_MASK | EMERGENCY_BUTTON_MASK);
      Wire.missing[0x20] = false; Wire.missing[0x24] = false;
      initializeControlIo(); initializeStatusIo(); advance(100);
    }
    puts("Pump: timed pulse, repeat/cooldown, mutual exclusion, X, door, contacts, I2C loss and UART fault passed");
    return 0;
  }
  if (mode == "heater_blocked") {
    advance(3000);
    heaterReadingValid = true; heaterReadingAt = millis(); heaterTemperature = 25;
    startHeaterTest();
    assert(!heaterTestActive && testOutputs[HEATER_ENABLE_PIN] == LOW);
    assert(Serial.output.find("verification pending") != std::string::npos);
    puts("Heater default build: hardware verification gate keeps D4 LOW");
    return 0;
  }
  if (mode == "heater") {
    assert(PAPERBRIQ_HEATER_VERIFIED);
    advance(3000);
    startHeaterTest(); assert(!heaterTestActive); // No sensor reading.
    const auto fresh = []() {
      requestDhtCheck();
      std::string frame = "DHT|" + std::to_string(dhtCheckSequence) + "|OK|25.0|60.0\n";
      for (char c : frame) espLink.input.push_back(c);
      serviceBridgeCommands();
    };
    fresh(); heaterTemperature = 40; startHeaterTest(); assert(!heaterTestActive);
    fresh(); startHeaterTest();
    assert(heaterTestActive && testOutputs[4] == HIGH && motorsOff());
    assert(!armMotorTest() && !startAutomaticProcess() && !startComponentTest());
    assert(!runChamberCommand("UNLOCK"));
    advance(2900); startHeaterTest(); advance(110);
    assert(!heaterTestActive && testOutputs[4] == LOW && !heaterReadingValid);
    startHeaterTest(); assert(!heaterTestActive);
    fresh(); startHeaterTest();
    Wire.inputs[0x24] |= ChamberControl::DOOR; advance(10);
    assert(!heaterTestActive && testOutputs[4] == LOW);
    Wire.inputs[0x24] &= (uint8_t)~ChamberControl::DOOR; advance(100);
    for (uint8_t mask : {STOP_BUTTON_MASK, EMERGENCY_BUTTON_MASK}) {
      fresh(); startHeaterTest(); assert(heaterTestActive);
      Wire.inputs[0x20] |= mask; advance(10);
      assert(!heaterTestActive && testOutputs[4] == LOW);
      Wire.inputs[0x20] &= (uint8_t)~mask; advance(100);
    }
    for (uint8_t address : {CONTROL_IO_ADDRESS, STATUS_IO_ADDRESS}) {
      fresh(); startHeaterTest(); assert(heaterTestActive);
      Wire.missing[address] = true; advance(10);
      assert(!heaterTestActive && testOutputs[4] == LOW);
      Wire.missing[address] = false;
      initializeControlIo(); initializeStatusIo(); advance(100);
    }
    fresh(); heaterReadingAt = millis() - 4900UL; startHeaterTest();
    assert(heaterTestActive); advance(100);
    assert(!heaterTestActive && testOutputs[4] == LOW);
    fresh(); startHeaterTest(); Serial.input.push_back('X'); serviceSerialCommands();
    assert(!heaterTestActive && testOutputs[4] == LOW && softwareStopLatched);
    advance(3000); fresh(); startHeaterTest(); assert(heaterTestActive);
    espLink.input.push_back('?'); serviceBridgeCommands(); advance(750);
    assert(!heaterTestActive && testOutputs[4] == LOW && softwareStopLatched);
    puts("Heater: bounded pulse, repeat rejection, temperature freshness, interlocks, I2C loss, X/UART stop passed");
    return 0;
  }
  if (mode == "reset_ack") {
    advance(1100);
    Wire.inputs[0x24] |= ChamberControl::DOOR;
    char rejected[] = "CMD|reset-door|RESET";
    handleRemoteCommandFrame(rejected);
    assert(espLink.output.find("ACK|reset-door|DELIVERED|received") != std::string::npos);
    assert(espLink.output.find("ACK|reset-door|FAILED|door_open") != std::string::npos);
    assert(softwareStopLatched && motorsOff());
    Wire.inputs[0x24] &= (uint8_t)~ChamberControl::DOOR;
    advance(1100);
    char replay[] = "CMD|reset-door|RESET";
    handleRemoteCommandFrame(replay);
    assert(softwareStopLatched && motorsOff());
    char accepted[] = "CMD|reset-good|RESET";
    handleRemoteCommandFrame(accepted);
    assert(espLink.output.find("ACK|reset-good|COMPLETED|armed") != std::string::npos);
    assert(!softwareStopLatched && motorsOff());
    const auto rejectedFor = [](const char *reason) {
      assert(!armMotorTest());
      assert(strcmp(lastArmRejectReason, reason) == 0);
      assert(motorsOff());
    };
    diagnosticActive=true; rejectedFor("test_running"); diagnosticActive=false;
    controlIoOnline=false; rejectedFor("buttons_offline"); controlIoOnline=true;
    emergencyButton.stableActive=true; rejectedFor("emergency_open"); emergencyButton.stableActive=false;
    stopButton.stableActive=true; rejectedFor("stop_open"); stopButton.stableActive=false;
    chamber.unlockOn=true; chamber.unlockAt=millis(); rejectedFor("unlock_active"); chamber.unlockOn=false;
    chamber.fanOn=true; chamber.fanAt=millis(); rejectedFor("fan_active"); chamber.fanOn=false;
    softwareStopLatchedAtMs=millis(); rejectedFor("reset_too_soon");
    Wire.missing[0x24]=true; rejectedFor("chamber_offline");
    puts("Reset ACK: specific rejection reasons, safe arming, and duplicate rejection replay passed");
    return 0;
  }
  if (mode == "uart_timeout") {
    advance(1100);
    assert(armMotorTest());
    const std::string partial = "CMD|health-test|CHE";
    for (char c : partial) espLink.input.push_back(c);
    serviceBridgeCommands();
    advance(740);
    assert(!softwareStopLatched);
    advance(20);
    assert(softwareStopLatched && motorsOff() && bridgeLineLength == 0);
    assert(Serial.output.find("UART RX partial (19 bytes): [CMD|health-test|CHE]") != std::string::npos);
    assert(espLink.output.find("UART frame timeout") != std::string::npos);
    const std::string next = "CMD|health-next|CHECK\n";
    for (char c : next) espLink.input.push_back(c);
    serviceBridgeCommands();
    assert(espLink.output.find("HEALTH|health-next|") != std::string::npos);
    assert(softwareStopLatched && motorsOff());
    puts("UART timeout: partial frame logged, stop retained, next health frame recovered");
    return 0;
  }
  if (mode == "chamber_pulses") {
    advance(3000);
    assert(runChamberCommand("UNLOCK"));
    assert(!(Wire.outputs[0x24] & ChamberControl::UNLOCK));
    Serial.output.clear();
    advance(990);
    assert(chamber.unlockOn);
    advance(20);
    assert(!chamber.unlockOn && (Wire.outputs[0x24] & ChamberControl::UNLOCK));
    assert(Serial.output.find("UNLOCK: OFF command sent") != std::string::npos);
    assert(Serial.output.find("ACTION BLOCKED") == std::string::npos);
    assert(runChamberCommand("FAN_TEST"));
    assert(!(Wire.outputs[0x24] & ChamberControl::FAN));
    Serial.output.clear();
    advance(9990);
    assert(chamber.fanOn);
    advance(20);
    assert(!chamber.fanOn && (Wire.outputs[0x24] & ChamberControl::FAN));
    assert(Serial.output.find("FAN: OFF command sent") != std::string::npos);
    assert(Serial.output.find("ACTION BLOCKED") == std::string::npos);
    assert(motorsOff() && softwareStopLatched && !automaticProcessIsActive());
    puts("Standalone chamber pulses: 1s unlock, 10s fan, no idle log flood passed");
    return 0;
  }
  if (mode == "buttons") {
    const auto press = [](uint8_t mask) {
      Wire.inputs[0x20] &= (uint8_t)~mask;
      advance(150);
      Wire.inputs[0x20] |= mask;
      advance(150);
    };
    advance(1100);
    Wire.inputs[0x24] |= ChamberControl::DOOR;
    advance(100);
    press(RESET_BUTTON_MASK);
    press(START_BUTTON_MASK);
    assert(softwareStopLatched && !automaticProcessIsActive() && motorsOff());
    assert(Serial.output.find("RESET BUTTON: pressed") != std::string::npos);
    assert(Serial.output.find("START BUTTON: pressed") != std::string::npos);
    assert(Serial.output.find("door interlock P2 is OPEN") != std::string::npos);
    assert(Serial.output.find("raw P2=HIGH") != std::string::npos);
    Wire.inputs[0x24] &= (uint8_t)~ChamberControl::DOOR;
    advance(100);
    press(RESET_BUTTON_MASK);
    assert(Serial.output.find("raw P2=LOW") != std::string::npos);
    assert(!softwareStopLatched && motorsOff());
    press(START_BUTTON_MASK);
    assert(automaticProcessIsActive());
    Wire.inputs[0x20] |= STOP_BUTTON_MASK;
    advance(150);
    assert(softwareStopLatched && !automaticProcessIsActive() && motorsOff());
    puts("Physical button simulation: door-open rejection, closed-door Reset/Start, Stop passed");
    return 0;
  }
  if (mode == "dht") {
    const auto reply = [](const std::string &frame) {
      for (char c : frame) espLink.input.push_back(c);
      serviceBridgeCommands();
    };
    strcpy(lastRemoteCommandId, "keep-actuator-replay-id");
    Serial.input.push_back('S'); serviceSerialCommands();
    assert(dhtCheckPending && espLink.output.find("DHT_CHECK|1") != std::string::npos);
    reply("DHT|99|OK|25.0|60.0\n");
    assert(dhtCheckPending); // Replies must match the current request.
    reply("DHT|1|OK|nan|60.0\n"); assert(dhtCheckPending);
    reply("DHT|1|OK|25.0|101\n"); assert(dhtCheckPending);
    reply("DHT|1|OK|25.0|60.0\n");
    assert(!dhtCheckPending && Serial.output.find("25.0 C, 60.0 %RH") != std::string::npos);
    requestDhtCheck(); reply("DHT|2|ERROR\n");
    assert(!dhtCheckPending && Serial.output.find("no fresh valid reading") != std::string::npos);
    requestDhtCheck(); testMillis += 10000;
    reply("DHT|3|OK|29.0|55.0\n");
    assert(!dhtCheckPending && Serial.output.find("reply timed out") != std::string::npos);
    assert(Serial.output.find("29.0 C") == std::string::npos);
    assert(strcmp(lastRemoteCommandId, "keep-actuator-replay-id") == 0);
    assert(motorsOff() && softwareStopLatched && !diagnosticActive);
    assert(!mixerGateServo.attached() && !molderServo.attached());
    assert(!chamber.fanOn && !chamber.unlockOn);
    puts("Uno DHT bridge: valid/error/stale/invalid replies and no actuation passed");
    return 0;
  }
  advance(1100);
  assert(chamber.ready());
  assert(armMotorTest());
  assert(startComponentTest());
  assert(!startComponentTest());
  assert(!startAutomaticProcess());
  assert(!armMotorTest());
  const int abortStep=mode=="full"?-1:atoi(argv[2]);
  bool injected=false, fanSeen=false, unlockSeen=false;
  for(int i=0;i<18000 && diagnosticActive;i++) {
    advance(10);
    fanSeen |= !(Wire.outputs[0x24] & ChamberControl::FAN);
    unlockSeen |= !(Wire.outputs[0x24] & ChamberControl::UNLOCK);
    if(chamber.unlockOn) assert(motorsOff());
    if(!injected && diagnosticActive && diagnosticStepRunning && diagnosticStep==abortStep) {
      injected=true;
      const int gateWrites=mixerGateServo.writes + molderServo.writes;
      const int detachCount=mixerGateServo.detaches + molderServo.detaches;
      if(mode=="door") Wire.inputs[0x24] |= ChamberControl::DOOR;
      else if(mode=="lost") Wire.missing[0x24]=true;
      else if(mode=="stop") Wire.inputs[0x20] |= STOP_BUTTON_MASK;
      else if(mode=="emergency") Wire.inputs[0x20] |= EMERGENCY_BUTTON_MASK;
      else if(mode=="software") Serial.input.push_back('X');
      advance(100);
      assert(!diagnosticActive && softwareStopLatched && motorsOff());
      assert(!chamber.fanOn && !chamber.unlockOn);
      if(mode=="door" || mode=="lost") {
        assert(gateWrites==mixerGateServo.writes + molderServo.writes);
        assert(detachCount==mixerGateServo.detaches + molderServo.detaches);
      }
      assert(espLink.output.find("TEST_ABORTED")!=std::string::npos);
      assert(espLink.output.find("TEST_COMPLETED")==std::string::npos);
      assert(!startComponentTest());
    }
  }
  assert(!diagnosticActive);
  if(mode=="full") {
    assert(fanSeen && unlockSeen && motorsOff());
    assert(!chamber.fanOn && !chamber.unlockOn && softwareStopLatched);
    assert(espLink.output.find("TEST_COMPLETED")!=std::string::npos);
    size_t at=0; int count=0;
    while((at=espLink.output.find("TEST_ITEM_DONE",at))!=std::string::npos) { count++; at++; }
    assert(count==9);
    assert(mixerGateServo.angle==MIXER_GATE_CLOSED_ANGLE);
    assert(molderServo.angle==MOLDER_CLOSED_ANGLE);
  } else assert(injected);
  printf("Uno integration %s %d passed\n",mode.c_str(),abortStep);
}
