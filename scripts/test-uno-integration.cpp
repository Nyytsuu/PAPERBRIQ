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
