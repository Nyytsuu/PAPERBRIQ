#include <assert.h>
#include <stdio.h>
#include "uno-test-stubs/Arduino.h"
#include "uno-test-stubs/Wire.h"
unsigned long testMillis = 0;
int testOutputs[32] = {};
Print Serial;
TwoWire Wire;
#include "../firmware/ArduinoUnoMotorTest/ArduinoUnoMotorTest.ino"
unsigned long powered[32] = {};
unsigned int shredCount=0;
bool inletSignal=false, outletSignal=false, heartbeat=true;
unsigned long lastHeartbeat=0;
void advance(unsigned long ms) {
  for (unsigned long n = 0; n < ms; n += 10) {
    for (int p=0; p<32; ++p) if (testOutputs[p]) powered[p] += 10;
    testMillis += 10;
    if (PAPERBRIQ_ESP_LINK_ENABLED && heartbeat && millis()-lastHeartbeat>=250) {
      lastHeartbeat=millis();
      const std::string frame=std::string("IR2|")+(inletSignal ? "1" : "0")+"|"+(outletSignal ? "1" : "0")+"\n";
      for (char c : frame) espLink.input.push_back(c);
    }
    loop();
    for (int pin : {SHREDDER_RPWM,PRESSER_RPWM,MIXER_RPWM,PUSHER_IN3}) assert(!(testOutputs[pin] && testOutputs[pin+1]));
    assert(!(testOutputs[3] && testOutputs[4]));
  }
}
void command(char c) { Serial.input.push_back(c); loop(); }
bool motorsOff() {
  for (int pin : {5,6,7,8,11,12,int(MIXER_RPWM),int(MIXER_LPWM)}) if (testOutputs[pin]) return false;
  return true;
}
void awaitShredder() {
  for (int i=0;i<1000 && automaticProcessState!=PROCESS_SHREDDING_AND_MIXING && automaticProcessState!=PROCESS_MIXER_FINISHING;++i) advance(10);
  assert(automaticProcessState==PROCESS_SHREDDING_AND_MIXING || automaticProcessState==PROCESS_MIXER_FINISHING);
  assert(testOutputs[5]);
  assert(testOutputs[3] == (localCycle.fillOnly || localLoadPaper));
  ++shredCount;
}
void injectFault(const std::string &mode) {
  if (mode=="x") command('X');
  else if (mode=="door") Wire.inputs[0x24] |= ChamberControl::DOOR;
  else if (mode=="stop") Wire.inputs[0x20] |= STOP_BUTTON_MASK;
  else if (mode=="emergency") Wire.inputs[0x20] |= EMERGENCY_BUTTON_MASK;
  else if (mode=="buttons") Wire.missing[0x20]=true;
  else if (mode=="status") Wire.missing[0x24]=true;
  else if (mode=="timeout") testMillis=localCycle.sessionAt+PAPERBRIQ_LOCAL_SESSION_MAX_MS;
  else assert(false);
}

void checkLocalStop(const std::string &mode) {
  command('W');
  assert(localCycle.phase == StandaloneCycle::PREHEATING);
  const std::string stage = mode.substr(mode.find(':') + 1);
  if (stage != "preheat") {
    advance(3000);
    command('W');
    awaitShredder();
    const unsigned long deadline = millis() + PAPERBRIQ_LOCAL_SESSION_MAX_MS;
    while (millis() < deadline) {
      if ((stage == "shred" && testOutputs[SHREDDER_RPWM]) ||
          (stage == "mix" && (testOutputs[MIXER_RPWM] || testOutputs[MIXER_LPWM])) ||
          (stage == "transfer" && mixerGateServo.angle == MIXER_GATE_OPEN_ANGLE) ||
          (stage == "press" && testOutputs[PRESSER_RPWM]) ||
          (stage == "retract" && testOutputs[PRESSER_LPWM]) ||
          (stage == "wait" && automaticProcessState == PROCESS_WAITING_FOR_NEXT_BATCH) ||
          (stage == "eject" && testOutputs[PUSHER_IN4]) ||
          (stage == "dry" && localCycle.phase == StandaloneCycle::HEATING)) break;
      if (automaticProcessState == PROCESS_WAITING_FOR_NEXT_BATCH) command('W');
      advance(10);
    }
    assert(millis() < deadline);
  }

  const bool emergency = mode.find("emergency:") == 0;
  const auto contact = emergency ? EMERGENCY_BUTTON_MASK : STOP_BUTTON_MASK;
  if (stage == "wait" && PAPERBRIQ_ESP_LINK_ENABLED) {
    inletSignal = true;
    advance(250);
    assert(localLoadPaper && automaticProcessState == PROCESS_WAITING_FOR_NEXT_BATCH);
  }
  Wire.inputs[CONTROL_IO_ADDRESS] |= contact;
  advance(BUTTON_POLL_MS);
  assert(softwareStopLatched && !localCycle.active() && !automaticProcessIsActive());
  assert(motorsOff() && !testOutputs[PUMP_ENABLE_PIN] && !testOutputs[HEATER_ENABLE_PIN]);
  assert(Serial.output.find(emergency ? "E-stop contact open" : "Stop contact open") != std::string::npos);

  Wire.inputs[CONTROL_IO_ADDRESS] &= ~contact;
  advance(100);
  command('W');
  advance(3000);
  assert(softwareStopLatched && !localCycle.active() && motorsOff());
  assert(!testOutputs[PUMP_ENABLE_PIN] && !testOutputs[HEATER_ENABLE_PIN]);
  printf("%s: stop latched, outputs off, no restart on release or W\n", mode.c_str());
}

int main(int argc, char **argv) {
  const std::string mode = argc > 1 ? argv[1] : "normal";
  BatchMixer timer;
  const uint32_t wrap = UINT32_MAX - 5000U;
  timer.begin(wrap);
  assert(!timer.tick(uint32_t(wrap+9999U),10000,2000,2000));
  assert(timer.tick(uint32_t(wrap+10000U),10000,2000,2000) && timer.stage == 1);
  assert(!timer.tick(uint32_t(wrap+11999U),10000,2000,2000));
  timer.begin(0);
  assert(timer.tick(50000,10000,2000,2000) && timer.stage == 1);
  assert(!timer.tick(51999,10000,2000,2000));
  assert(timer.tick(52000,10000,2000,2000) && timer.stage == 2);
  // The 10s forward lead flows directly into the original pattern.
  timer.begin(60000, true);
  assert(!timer.tick(69999,10000,2000,2000) && timer.stage == 8);
  assert(timer.tick(70000,10000,2000,2000) && timer.stage == 0);
  assert(!timer.tick(79999,10000,2000,2000));
  assert(timer.tick(80000,10000,2000,2000) && timer.stage == 1);
  timer.begin(130000, false);
  assert(timer.tick(140000,10000,2000,2000) && timer.stage == 1);
  assert(timer.tick(142000,10000,2000,2000) && timer.stage == 2);
  setup(); advance(3000); command('R');
  if (mode == "load-2" || mode == "load-3" || mode == "skip-door" || mode == "stop-repeat") {
    const char shortcut = mode == "load-2" ? '8' : '9';
    command(shortcut);
    assert(!localCycle.active() && motorsOff());
    command('W');
    assert(localCycle.phase == StandaloneCycle::PREHEATING);
    if (mode == "skip-door") {
      injectFault("door");
      command(shortcut);
      assert(softwareStopLatched && !localCycle.active() && motorsOff() && !testOutputs[4]);
      return 0;
    }
    advance(3000);
    command(shortcut);
    assert(localCycle.batch == shortcut - '7');
    awaitShredder();
    command('9'); // The shortcut must not jump while a load is moving.
    assert(localCycle.batch == shortcut - '7');
    if (mode == "load-2" && PAPERBRIQ_ESP_LINK_ENABLED) {
      inletSignal = true;
      advance(250);
      assert(testOutputs[PUMP_ENABLE_PIN]);
      inletSignal = false;
      advance(250);
      assert(!testOutputs[PUMP_ENABLE_PIN]);
      advance(LOCAL_SHREDDER_LEAD_MS - (millis() - processStateStartedAtMs));
      assert(localMixWater && testOutputs[PUMP_ENABLE_PIN]);
      const auto waterBeforeMix = powered[PUMP_ENABLE_PIN];
      advance(39990);
      assert(testOutputs[PUMP_ENABLE_PIN] && mixerGateServo.angle == MIXER_GATE_CLOSED_ANGLE);
      advance(10);
      assert(!testOutputs[PUMP_ENABLE_PIN] && powered[PUMP_ENABLE_PIN] - waterBeforeMix == 40000);
    }
    const unsigned long deadline = millis() + 300000;
    bool checkedRepeatStart = false;
    while (automaticProcessState != PROCESS_WAITING_FOR_NEXT_BATCH &&
           localCycle.phase == StandaloneCycle::FORMING && millis() < deadline) {
      advance(10);
      if (localCycle.repressed && !checkedRepeatStart) {
        checkedRepeatStart = true;
        assert(powered[PRESSER_RPWM] == BATCH_PRESS_THIRD_MS);
        assert(powered[PRESSER_LPWM] == PROCESS_PRESSER_RETRACT_MS);
        assert(molderServo.angle == MOLDER_CLOSED_ANGLE && !powered[PUSHER_IN4]);
      }
      if (mode == "stop-repeat" && localCycle.repressed && testOutputs[PRESSER_RPWM]) {
        command('X');
        assert(softwareStopLatched && motorsOff() && !powered[PUSHER_IN4]);
        return 0;
      }
    }
    assert(millis() < deadline && !softwareStopLatched);
    if (mode == "load-2") {
      assert(automaticProcessState == PROCESS_WAITING_FOR_NEXT_BATCH);
      assert(powered[PRESSER_RPWM] == BATCH_PRESS_SECOND_MS);
      assert(!powered[PUSHER_IN4]);
    } else {
      assert(localCycle.phase == StandaloneCycle::HEATING);
      assert(checkedRepeatStart);
      assert(powered[PRESSER_RPWM] == BATCH_PRESS_THIRD_MS + 13000);
      assert(powered[PRESSER_LPWM] == 2 * PROCESS_PRESSER_RETRACT_MS);
      assert(powered[PUSHER_IN4] == LOCAL_PUSHER_EXTEND_MS);
    }
    return 0;
  }
  if (mode.find("stop:") == 0 || mode.find("emergency:") == 0) {
    checkLocalStop(mode);
    return 0;
  }
  if (mode=="dry") {
    command('G');
    assert(automaticProcessIsActive());
    advance(300000);
    assert(!automaticProcessIsActive() && !localCycle.active());
    assert(powered[3]==0 && powered[4]==0);
    assert(powered[5]==BENCH_SHREDDER_RUN_MS);
    assert(powered[7]==PROCESS_PRESSER_EXTEND_MS);
    assert(powered[PUSHER_IN4]==12000 && powered[PUSHER_IN3]==12000);
    puts("G remains one unloaded forming cycle without prefill, pump or heat");
    return 0;
  }
  const bool fillTest = mode=="single" || mode=="stuck";
  const bool sensorTest = fillTest && PAPERBRIQ_FILL_IR_ENABLED;
  if (fillTest) {
    command('M');
  }
  if (sensorTest) {
    advance(5000);
    assert(automaticProcessState==PROCESS_WAITING_FOR_PAPER && motorsOff() && !testOutputs[3]);
    testInput(FILL_IR_OUT_PIN)=LOW;
    advance(100);
    assert(motorsOff());
    testInput(FILL_IR_IN_PIN)=LOW;
  }
  else if (!fillTest) {
    command('W');
    assert(localCycle.phase == StandaloneCycle::PREHEATING && motorsOff());
    advance(3000);
    if (mode == "paper-held-at-start") {
      inletSignal = true;
      advance(250);
      assert(!testOutputs[3]);
    }
    command('W');
  }
  awaitShredder();
  if (mode == "paper-held-at-start") {
    advance(250);
    assert(testOutputs[3]);
    inletSignal = false;
    advance(250);
    assert(!testOutputs[3]);
    advance(5000);
    assert(!localMixWater && !testOutputs[3]);
    advance(LOCAL_SHREDDER_LEAD_MS);
    assert(localMixWater && testOutputs[3]);
    return 0;
  }
  if (mode=="stuck") {
    advance(FILL_SHREDDER_MAX_MS);
    assert(softwareStopLatched && motorsOff() && !testOutputs[3]);
    assert(Serial.output.find("M: shred limit")!=std::string::npos);
    return 0;
  }
  if (!PAPERBRIQ_ESP_LINK_ENABLED) {
    for (char c : std::string("garbled UART bytes\n")) espLink.input.push_back(c);
  }
  loop();
  assert(!softwareStopLatched && testOutputs[5] && testOutputs[3] == fillTest);
  if (mode == "paper-dose" || mode == "paper-dose-stop" || mode == "paper-dose-cap") {
    assert(PAPERBRIQ_ESP_LINK_ENABLED);
    if (mode == "paper-dose") advance(45000);
    inletSignal = true;
    advance(250);
    assert(testOutputs[3]);
    if (mode == "paper-dose-stop") {
      command('X');
      assert(softwareStopLatched && !testOutputs[3]);
      return 0;
    }
    if (mode == "paper-dose-cap") {
      advance(20000);
      assert(!testOutputs[3] && powered[3] == 20000);
      inletSignal = false;
      advance(5250);
      assert(localMixWater && testOutputs[3]);
      advance(40000);
      assert(!testOutputs[3] && powered[3] == 60000);
      assert(!softwareStopLatched);
      return 0;
    }

    for (unsigned batch = 0; batch < 3; ++batch) {
      const auto shredStartedAt = processStateStartedAtMs;
      advance(batch == 1 ? 1000 : 6000);
      inletSignal = false;
      advance(250);
      assert(!testOutputs[3] && localCycle.pumping());
      inletSignal = true;
      advance(250);
      assert(testOutputs[3]);
      inletSignal = false;
      advance(5000);
      assert(!testOutputs[3]);
      advance(250);
      if (batch == 1) {
        assert(!localMixWater && !testOutputs[3]);
        advance(LOCAL_SHREDDER_LEAD_MS - (millis() - shredStartedAt));
      }
      assert(localMixWater && testOutputs[3]);
      inletSignal = true;
      advance(250);
      assert(testOutputs[3]); // Another detection cannot restart the mix timer.
      inletSignal = false;
      advance(39000);
      assert(testOutputs[3] && mixerGateServo.angle == MIXER_GATE_CLOSED_ANGLE);
      const bool mixerPause = batchMixer.stage < 7 && (batchMixer.stage & 1);
      const bool extensionPause = paperExtensionActive &&
          millis() - paperExtensionAt < BATCH_MIX_PAUSE_MS;
      assert(testOutputs[MIXER_RPWM] || testOutputs[MIXER_LPWM] || mixerPause || extensionPause);
      advance(1000);
      assert(!testOutputs[3]);
      if (batch < 2) {
        while (automaticProcessIsActive() &&
               automaticProcessState != PROCESS_WAITING_FOR_NEXT_BATCH) advance(10);
        assert(automaticProcessState == PROCESS_WAITING_FOR_NEXT_BATCH);
        advance(1000);
        assert(localCycle.batch == batch && !testOutputs[3]); // Clear alone does not start.
        inletSignal = true;
        advance(250);
        awaitShredder();
        assert(localCycle.batch == batch + 1 && testOutputs[3]);
      }
    }
    assert(powered[3] > 133000 && powered[3] < 138000 && !softwareStopLatched);
    puts("W water pauses on clear, starts one mix dose after 5s, and resets per load");
    return 0;
  }
  const auto shredMs=(fillTest ? BENCH_SHREDDER_RUN_MS : LOCAL_SHREDDER_LEAD_MS) + (sensorTest ? 5000UL : 0UL);
  const auto mixDuration=BATCH_MIX_FORWARD_MS+BATCH_MIX_TOTAL_MS+FILL_MIX_EXTRA_MS;
  const auto firstReverseAt = 2 * BATCH_MIX_FORWARD_MS + BATCH_MIX_PAUSE_MS;
  const auto secondForwardAt = firstReverseAt + BATCH_MIX_REVERSE_MS + BATCH_MIX_PAUSE_MS;
  const auto lastReverseAt = secondForwardAt + BATCH_MIX_FORWARD_MS + BATCH_MIX_PAUSE_MS;
  const auto end=shredMs+mixDuration;
  if (mode.find("extension")==0) {
    assert(PAPERBRIQ_ESP_LINK_ENABLED);
    outletSignal=true;
    advance(end);
    assert(paperExtensionActive && testOutputs[5] && mixerGateServo.angle==MIXER_GATE_CLOSED_ANGLE);
    assert(!testOutputs[MIXER_RPWM] && !testOutputs[MIXER_LPWM]);
    advance(2000);
    assert(testOutputs[PROCESS_MIXER_USE_RPWM ? MIXER_RPWM : MIXER_LPWM]);
    if (mode=="extension-link") {
      heartbeat=false; advance(1600);
      assert(softwareStopLatched && motorsOff() && !testOutputs[3]);
    } else if (mode=="extension-stop" || mode=="extension-usb-flood") {
      if (mode=="extension-usb-flood") {
        for (int i=0;i<100;++i)
          for (char c : std::string("IR2|0|1\n")) espLink.input.push_back(c);
      }
      command('X');
      assert(softwareStopLatched && motorsOff() && !testOutputs[3]);
      if (mode=="extension-usb-flood") assert(espLink.available()>0);
    } else if (mode=="extension-panel-stop" || mode=="extension-emergency") {
      for (int i=0;i<100;++i)
        for (char c : std::string("IR2|0|1\n")) espLink.input.push_back(c);
      injectFault(mode=="extension-panel-stop" ? "stop" : "emergency");
      advance(100);
      assert(softwareStopLatched && motorsOff() && !testOutputs[3]);
      assert(mixerGateServo.angle==MIXER_GATE_CLOSED_ANGLE);
      Wire.inputs[0x20]=0xF3;
      advance(1000);
      assert(softwareStopLatched && motorsOff());
    } else {
      advance(1000);
      if (mode=="extension-long") {
        advance(17000);
        assert(!softwareStopLatched && testOutputs[5] && !testOutputs[3]);
        assert(mixerGateServo.angle==MIXER_GATE_CLOSED_ANGLE);
      }
      outletSignal=false;
      advance(4900);
      assert(automaticProcessState==PROCESS_MIXER_FINISHING);
      advance(400);
      assert(automaticProcessState==PROCESS_WAITING_TO_OPEN_GATE && motorsOff());
      advance(15000);
      assert(mixerGateServo.angle==MIXER_GATE_OPEN_ANGLE);
      assert(powered[3]==0); // Outlet alone never adds water.
    }
    puts("W extension: outlet hold, 5s clear, reversal pause, fixed water dose and stops verified");
    return 0;
  }
  for (unsigned long t=0;t<=end;t+=10) {
    if (sensorTest && t==1000) testInput(FILL_IR_IN_PIN)=HIGH;
    if (sensorTest && t==shredMs-FILL_IR_CLEAR_MS+10) testInput(FILL_IR_OUT_PIN)=HIGH;
    if (t) advance(10);
    assert(testOutputs[5] == (t < (!sensorTest ? end : shredMs) ? SHREDDER_FORWARD_DUTY : 0));
    assert(testOutputs[6] == LOW);
    const auto mixMs=t>=shredMs ? t-shredMs : 0;
    const bool forward = t>=shredMs && (mixMs < 2 * BATCH_MIX_FORWARD_MS ||
        (mixMs >= secondForwardAt && mixMs < secondForwardAt + BATCH_MIX_FORWARD_MS));
    const bool reverse = t>=shredMs && ((mixMs >= firstReverseAt && mixMs < firstReverseAt + BATCH_MIX_REVERSE_MS) ||
        (mixMs >= lastReverseAt && mixMs < mixDuration));
    assert(testOutputs[MIXER_RPWM] == (PROCESS_MIXER_USE_RPWM ? forward : reverse));
    assert(testOutputs[MIXER_LPWM] == (PROCESS_MIXER_USE_RPWM ? reverse : forward));
    assert(testOutputs[3] == fillTest);
    assert(testOutputs[4] == LOW);
    assert(mixerGateServo.angle == MIXER_GATE_CLOSED_ANGLE);
  }
  assert(automaticProcessState == PROCESS_WAITING_TO_OPEN_GATE);
  {
    advance((fillTest ? FILL_GATE_WAIT_MS : 10000UL)-10);
    assert(motorsOff() && testOutputs[3]==fillTest && mixerGateServo.angle==MIXER_GATE_CLOSED_ANGLE);
    advance(10);
  }
  assert(mixerGateServo.angle == MIXER_GATE_OPEN_ANGLE);
  assert(testOutputs[3] == LOW);
  if (mode == "press-joined-ir" || mode == "press-corrupt-ir") {
    for (int i = 0; i < 10000 && !testOutputs[PRESSER_RPWM]; ++i) advance(10);
    assert(testOutputs[PRESSER_RPWM]);
    advance(10);
    const auto lastGoodReport = dualPaperAt;
    const std::string report = mode == "press-corrupt-ir" ? "IR2|g|0\n" : "IR2|0|0?IR2|0|0\n";
    for (char c : report) espLink.input.push_back(c);
    loop();
    assert(dualPaperAt == lastGoodReport);
    assert(testOutputs[PRESSER_RPWM] && !softwareStopLatched);
    command('X');
    assert(softwareStopLatched && motorsOff());
    return 0;
  }
  const unsigned long session = localCycle.sessionAt;
  if (mode=="single") {
    for (int i=0;i<16000 && localCycle.phase==StandaloneCycle::FORMING;++i) {
      advance(10);
      assert(automaticProcessState!=PROCESS_WAITING_FOR_NEXT_BATCH);
      assert(testOutputs[5]==LOW);
    }
    assert(!localCycle.active() && softwareStopLatched && motorsOff());
    assert(powered[5]==(sensorTest ? shredMs : end) && powered[3]==end+FILL_GATE_WAIT_MS);
    assert(powered[4]==0 && powered[7]==0 && powered[8]==0);
    assert(powered[11]==0 && powered[12]==0);
    assert(mixerGateServo.angle==MIXER_GATE_CLOSED_ANGLE);
    assert(Serial.output.find("FILL DONE")!=std::string::npos);
    puts("M stops after filling the mold");
    return 0;
  }
  for (unsigned batch=0; batch<3; ++batch) {
    const unsigned long deadline = millis()+300000;
    while (automaticProcessState != PROCESS_WAITING_FOR_NEXT_BATCH &&
           localCycle.phase != StandaloneCycle::HEATING && millis()<deadline) {
      advance(10);
      if (batch<2) {
        assert(molderServo.angle == MOLDER_CLOSED_ANGLE);
        assert(!testOutputs[11] && !testOutputs[12]);
      }
    }
    assert(millis()<deadline && localCycle.active());
    assert(localCycle.sessionAt == session);
    assert(powered[3] == 0); // No inlet detections.
    // D7 extends, D8 retracts. Measure powered time, excluding settling.
    const unsigned long press[] = {BATCH_PRESS_FIRST_MS,
        BATCH_PRESS_FIRST_MS+BATCH_PRESS_SECOND_MS,
        BATCH_PRESS_FIRST_MS+BATCH_PRESS_SECOND_MS+BATCH_PRESS_THIRD_MS+BATCH_PRESS_THIRD_REPEAT_MS};
    assert(powered[7] == press[batch]);
    assert(powered[8] == PROCESS_PRESSER_RETRACT_MS*(batch == 2 ? 4 : batch + 1));
    if (batch==2) break;
    assert(automaticProcessState == PROCESS_WAITING_FOR_NEXT_BATCH);
    assert(motorsOff() && !testOutputs[3] && !testOutputs[4]);
    advance(20000);
    assert(localCycle.batch==batch && automaticProcessState==PROCESS_WAITING_FOR_NEXT_BATCH);
    if (mode != "normal") {
      injectFault(mode);
      command('W'); advance(1000);
      assert(!localCycle.active() && softwareStopLatched && motorsOff());
      assert(!testOutputs[3] && !testOutputs[4]);
      Wire.inputs[0x20]=0xF3; Wire.inputs[0x24]=0xFB;
      Wire.missing[0x20]=Wire.missing[0x24]=false;
      advance(20000); command('W');
      assert(!localCycle.active() && motorsOff());
      puts("Waiting batch: fault/timeout cancels; Start cannot bypass or restart");
      return 0;
    }
    if (batch==0) { // Physical Start must use the same continuation path.
      Wire.inputs[0x20] &= ~1; advance(200);
      Wire.inputs[0x20] |= 1; advance(200);
    } else command('W');
    assert(localCycle.batch==batch+1);
    command('W'); // Repeated Start cannot skip the new batch.
    assert(localCycle.batch==batch+1);
    awaitShredder();
  }
  assert(localCycle.phase==StandaloneCycle::HEATING);
  assert(powered[5]==3*(LOCAL_SHREDDER_LEAD_MS+mixDuration) && powered[6]==0);
  assert(shredCount==3);
  const auto mixerForward = 9 * BATCH_MIX_FORWARD_MS;
  const auto mixerReverse = 3 * (2 * BATCH_MIX_REVERSE_MS + FILL_MIX_EXTRA_MS);
  assert(powered[MIXER_RPWM]==(PROCESS_MIXER_USE_RPWM ? mixerForward : mixerReverse));
  assert(powered[MIXER_LPWM]==(PROCESS_MIXER_USE_RPWM ? mixerReverse : mixerForward));
  assert(powered[PUSHER_IN4]==24000 && powered[PUSHER_IN3]==69000);
  assert(molderServo.angle==MOLDER_CLOSED_ANGLE && motorsOff());
  puts("Three batches: local Start/W runs every batch without ESP32; exact mix/press/pump times; physical/serial continuation; final-only ejection; drying resumes");
}
