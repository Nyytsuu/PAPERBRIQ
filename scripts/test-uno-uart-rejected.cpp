#include <assert.h>
#include <stdio.h>
#include "uno-test-stubs/Arduino.h"
#include "uno-test-stubs/Wire.h"
unsigned long testMillis = 0;
int testOutputs[32] = {};
Print Serial;
TwoWire Wire;
#define PAPERBRIQ_ESP_LINK_ENABLED 1
#include "../firmware/ArduinoUnoMotorTest/ArduinoUnoMotorTest.ino"

void advance(unsigned long ms) {
  for (unsigned long n=0; n<ms; n+=10) { testMillis+=10; loop(); }
}
void frame(const std::string &s) {
  for (char c : s) espLink.input.push_back(c);
  serviceBridgeCommands();
}
void command(char c) { Serial.input.push_back(c); loop(); }
int main(int argc, char **argv) {
  const std::string mode=argc>1 ? argv[1] : "prefix";
  setup(); advance(3000); command('R');
  assert(!softwareStopLatched);
  requestDhtCheck();
  frame("DHT|"+std::to_string(dhtCheckSequence)+"|OK|26.3|86.3\r\n");
  assert(heaterReadingValid && !dhtCheckPending && !softwareStopLatched);
  if (mode=="health-overlap") {
    testMillis+=3000;
    espLink.input.push_back('C');
    serviceChamberTemperature();
    assert(!dhtCheckPending);
    frame("MD|health-adb1|");
    serviceChamberTemperature();
    assert(!dhtCheckPending);
    frame("CHECK\n");
    assert(espLink.output.find("HEALTH|health-adb1|")!=std::string::npos);
    serviceChamberTemperature();
    assert(!dhtCheckPending);
    testMillis+=20;
    serviceChamberTemperature();
    assert(dhtCheckPending && !softwareStopLatched);
    puts("Temperature polling waits for queued and partial health frames");
    return 0;
  }
  frame("IR|0\r\nIR|1\nCMD|health-adb1|CHECK\n");
  assert(!softwareStopLatched);
  assert(espLink.output.find("HEALTH|health-adb1|")!=std::string::npos);
  assert(Serial.output.find("UART_BAD|")==std::string::npos);
  if (mode=="ir-trigger" || mode=="ir-missing") {
    command('A');
    assert(irShredTest && automaticProcessIsActive());
    if (mode=="ir-missing") {
      advance(7000);
      assert(softwareStopLatched && !irShredTest && !testOutputs[SHREDDER_RPWM]);
      assert(Serial.output.find("Paper link lost")!=std::string::npos);
      puts("A still stops safely when its IR heartbeat is absent");
      return 0;
    }
    // A sensor already blocked during arming cannot start the shredder.
    for (int i=0;i<24;++i) { frame("IR|1\n"); advance(250); }
    assert(automaticProcessState==PROCESS_WAITING_FOR_PAPER && !testOutputs[SHREDDER_RPWM]);
    frame("IR|0\n"); frame("IR|1\n");
    assert(!irShredTest && runState==STATE_WAITING);
    advance(DIRECTION_CHANGE_PAUSE_MS);
    assert(testOutputs[SHREDDER_RPWM]==SHREDDER_FORWARD_DUTY);
    assert(!testOutputs[3] && !testOutputs[4] && !testOutputs[MIXER_RPWM] && !testOutputs[MIXER_LPWM]);
    const auto pulseAt=stateStartedAtMs;
    frame("IR|0\nIR|1\n");
    assert(stateStartedAtMs==pulseAt);
    advance(3000);
    assert(!testOutputs[SHREDDER_RPWM] && runState==STATE_IDLE);
    puts("A waits for fresh inlet edge, pulses shredder 3s once, with pump/mixer off");
    return 0;
  }
  // Start actual local timed batch; malformed frames must still stop water/motion.
  command('W'); advance(3000); command('W');
  for (int i=0;i<1000 && automaticProcessState!=PROCESS_SHREDDING_AND_MIXING;++i) {
    frame("IR2|0|0\n");
    advance(10);
  }
  frame("IR2|1|0\n");
  loop();
  assert(testOutputs[3] && localCycle.active());
  if (mode=="joined-ir" || mode=="corrupt-ir") {
    const auto lastGoodReport = dualPaperAt;
    for (int i = 0; i < 8; ++i) {
      frame(mode == "corrupt-ir" ? "IR2|g|0\n" : std::string("IR2|0|0") + char(1) + "IR2|0|0\n");
      assert(dualPaperAt == lastGoodReport);
      advance(250);
    }
    assert(softwareStopLatched && !testOutputs[3]);
    assert(Serial.output.find("Paper link lost") != std::string::npos);
    return 0;
  }
  if (mode=="timeout") {
    // Reproduce the screenshot: STOP completed, then an unrelated partial line.
    frame("CMD|stop-test|STOP\n");
    assert(Serial.output.find("ACK|stop-test|COMPLETED|stopped")!=std::string::npos);
    assert(softwareStopLatched && !testOutputs[3]);
    Serial.output.clear(); espLink.output.clear();
    frame(std::string("IR|")+char(1)+"0");
    testMillis+=749; serviceBridgeCommands();
    assert(Serial.output.find("UART RX partial")==std::string::npos);
    testMillis+=1; serviceBridgeCommands();
    assert(Serial.output.find("UART RX partial (5 bytes): [IR|?0]")!=std::string::npos);
    assert(Serial.output.find("UART frame timeout")!=std::string::npos);
    assert(bridgeLineLength==0 && softwareStopLatched && !localCycle.active());
    for (int p : {3,4,5,6,7,8,11,12,int(MIXER_RPWM),int(MIXER_LPWM)}) assert(!testOutputs[p]);
    frame("CMD|after-timeout|CHECK\n");
    assert(espLink.output.find("HEALTH|after-timeout|")!=std::string::npos);
    puts("Completed STOP followed by partial frame: 750ms fault, visible bytes, no restart");
    return 0;
  }
  std::string bad;
  if (mode=="prefix") bad="MD|health-adb1|CHECK";
  else if (mode=="missing") bad="CMD|health-adb1";
  else if (mode=="extra") bad="CMD|health-adb1|CHECK|oops";
  else if (mode=="ir") bad="IR|";
  else if (mode=="binary") bad=std::string("?")+char(1)+"|garbage";
  else assert(false);
  frame(bad.substr(0,2));
  assert(!softwareStopLatched); // Fragment isn't parsed until newline/timeout.
  frame(bad.substr(2)+"\n");
  assert(softwareStopLatched && !localCycle.active());
  for (int p : {3,4,5,6,7,8,11,12,int(MIXER_RPWM),int(MIXER_LPWM)}) assert(!testOutputs[p]);
  std::string shown=bad;
  for (char &c : shown) if (static_cast<unsigned char>(c)<32 || static_cast<unsigned char>(c)>126) c='?';
  const auto diagnostic="UART_BAD|"+shown+"\n";
  assert(Serial.output.find(diagnostic)!=std::string::npos);
  assert(espLink.output.find(diagnostic)!=std::string::npos);
  assert(Serial.output.find("Invalid UART command")!=std::string::npos);
  frame("CMD|health-after|CHECK\n");
  assert(softwareStopLatched && !localCycle.active() && !testOutputs[3]);
  puts("Valid ESP frames accepted; bad frame stops active batch and is logged on both serial links");
}
