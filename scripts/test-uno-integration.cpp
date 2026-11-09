#include <assert.h>
#include <stdio.h>
#include "uno-test-stubs/Arduino.h"
#include "uno-test-stubs/Wire.h"
unsigned long testMillis = 0;
int testOutputs[32] = {};
Print Serial;
TwoWire Wire;
#ifdef PAPERBRIQ_PRODUCTION_BUILD
#include "../firmware/PaperBriqController/PaperBriqController.ino"
// The shared harness also exercises the separately retained diagnostic sketch.
#define armMotorTest armController
#else
#include "../firmware/ArduinoUnoMotorTest/ArduinoUnoMotorTest.ino"
#endif

bool simulateThermalSensor = false;
float simulatedTemperature = 45;
void advance(unsigned long ms) {
  for(unsigned long n=0;n<ms;n+=10) {
    testMillis+=10; loop();
    assert(!(testOutputs[9] && testOutputs[10]));
    if (simulateThermalSensor && dhtCheckPending) {
      std::string reply = "DHT|" + std::to_string(dhtCheckSequence) + "|OK|" +
          std::to_string(simulatedTemperature) + "|60.0";
      handleDhtReply(&reply[0]);
    }
  }
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
#ifndef PAPERBRIQ_PRODUCTION_BUILD
  if (mode == "mixer_alternation") {
    // Wraparound and late servicing must preserve a full pause, not skip it.
    MixerAlternation timer;
    const uint32_t origin = UINT32_MAX - 5000U;
    timer.begin(origin);
    assert(!timer.tick(uint32_t(origin + 9999U), 10000, 2000));
    assert(timer.tick(uint32_t(origin + 10000U), 10000, 2000) && timer.stage == 1);
    assert(!timer.tick(uint32_t(origin + 11999U), 10000, 2000));
    assert(timer.tick(uint32_t(origin + 12000U), 10000, 2000) && timer.stage == 2);
    timer.begin(0);
    assert(timer.tick(50000, 10000, 2000) && timer.stage == 1);
    assert(!timer.tick(51999, 10000, 2000));
    assert(timer.tick(52000, 10000, 2000) && timer.stage == 2);

    const auto usb = [](char c) { Serial.input.push_back(c); serviceSerialCommands(); };
    advance(3000); usb('R'); usb('G');
    for (int i = 0; i < 1000 && automaticProcessState != PROCESS_SHREDDING_AND_MIXING; ++i)
      advance(10);
    assert(automaticProcessState == PROCESS_SHREDDING_AND_MIXING);
    const unsigned long started = millis();
    const std::string fault = argc > 2 ? argv[2] : "none";
    const int phase = argc > 3 ? atoi(argv[3]) : 0;
    if (fault != "none") {
      const unsigned long offsets[] = {100, 10100, 12100, 22100};
      assert(phase >= 0 && phase < 4);
      advance(offsets[phase]);
      assert(processMixer.stage == phase);
      if (fault == "x") usb('X');
      else if (fault == "door") Wire.inputs[0x24] |= ChamberControl::DOOR;
      else if (fault == "stop") Wire.inputs[0x20] |= STOP_BUTTON_MASK;
      else if (fault == "emergency") Wire.inputs[0x20] |= EMERGENCY_BUTTON_MASK;
      else if (fault == "buttons") Wire.missing[0x20] = true;
      else if (fault == "status") Wire.missing[0x24] = true;
      else assert(false);
      advance(300);
      assert(softwareStopLatched && motorsOff() && !automaticProcessIsActive());
      Wire.inputs[0x20] = 0xF3; Wire.inputs[0x24] = 0xFB;
      Wire.missing[0x20] = Wire.missing[0x24] = false;
      advance(25000);
      assert(motorsOff() && softwareStopLatched);
      usb('R'); usb('G');
      for (int i = 0; i < 1000 && automaticProcessState != PROCESS_SHREDDING_AND_MIXING; ++i)
        advance(10);
      assert(processMixer.stage == 0 && testOutputs[9] == HIGH && !testOutputs[10]);
      puts("Mixer fault: stops in each leg/pause, no auto-resume, new batch starts forward");
      return 0;
    }
    // Independent expected timeline spans the shredder -> finishing transition.
    const unsigned long times[] = {0,9990,10000,11990,12000,19990,20000,21990,
                                  22000,23990,24000,33990,34000,34990,35000};
    for (unsigned long t : times) {
      advance(t - (millis() - started));
      const bool forward = t < 10000 || (t >= 24000 && t < 34000);
      const bool reverse = t >= 12000 && t < 22000;
      assert(testOutputs[9] == forward && testOutputs[10] == reverse);
      assert(testOutputs[3] == LOW && testOutputs[4] == LOW);
    }
    assert(automaticProcessState == PROCESS_WAITING_TO_OPEN_GATE);
    advance(3000);
    assert(!testOutputs[9] && !testOutputs[10]);
    puts("Mixer: exact 10/2/10/2 timing; mixing deadline preserved; wrap/late tick checked");
    return 0;
  }
  if (mode == "positioning") {
    const auto usb = [](char c) { Serial.input.push_back(c); serviceSerialCommands(); };
    advance(3000);
    usb('Z'); assert(positioningStep == 0 && motorsOff());
    usb('R'); advance(3000);
    const std::string fault = argc > 2 ? argv[2] : "none";
    const int target = argc > 3 ? atoi(argv[3]) : 5;
    const unsigned long times[] = {0, MOLDER_TRAVEL_MS, PROCESS_PRESSER_EXTEND_MS,
      PROCESS_PRESSER_RETRACT_MS, MOLDER_TRAVEL_MS, PROCESS_PUSHER_EXTEND_MS,
      PROCESS_PUSHER_RETRACT_MS, MOLDER_TRAVEL_MS};
    for (int step = 1; step <= 7; ++step) {
      usb('Z'); assert(positioningStep == step);
      usb('Z'); assert(positioningStep == step); // Repeated key doesn't skip or restart.
      const auto gateWrites = mixerGateServo.writes + molderServo.writes;
      for (char c : std::string("RWPQJY1234567EFTB")) usb(c);
      assert(positioningStep == step && !localCycle.active() && !production.active());
      assert(!armMotorTest() && !startStandaloneCycle() && !startComponentTest());
      assert(mixerGateServo.writes + molderServo.writes == gateWrites);
      assert(testOutputs[3] == LOW && testOutputs[4] == LOW);
      for (int p = 5; p <= 6; ++p) assert(testOutputs[p] == LOW);
      assert(testOutputs[9] == LOW && testOutputs[10] == LOW);
      const bool motor = step == 2 || step == 3 || step == 5 || step == 6;
      if (motor) {
        advance(DIRECTION_CHANGE_PAUSE_MS + 10);
        assert(runState == STATE_RUNNING);
        const int activePin = step == 2 ? PRESSER_RPWM : step == 3 ? PRESSER_LPWM :
                              step == 5 ? PUSHER_IN4 : PUSHER_IN3;
        assert(testOutputs[activePin] == HIGH);
      }
      if (fault != "none" && step == target) {
        if (fault == "x") usb('X');
        else if (fault == "door") Wire.inputs[0x24] |= ChamberControl::DOOR;
        else if (fault == "stop") Wire.inputs[0x20] |= STOP_BUTTON_MASK;
        else if (fault == "emergency") Wire.inputs[0x20] |= EMERGENCY_BUTTON_MASK;
        else if (fault == "buttons") Wire.missing[0x20] = true;
        else if (fault == "status") Wire.missing[0x24] = true;
        else assert(false);
        advance(300);
        assert(positioningStep == 0 && motorsOff() && softwareStopLatched);
        assert(mixerGateServo.writes + molderServo.writes == gateWrites);
        assert(testOutputs[3] == LOW && testOutputs[4] == LOW);
        Wire.inputs[0x20] = 0xF3; Wire.inputs[0x24] = 0xFB;
        Wire.missing[0x20] = Wire.missing[0x24] = false;
        advance(3000); usb('Z'); assert(positioningStep == 0);
        puts("Positioning fault: motors off, no new gate movement, Reset required"); return 0;
      }
      advance(times[step] + DIRECTION_CHANGE_PAUSE_MS);
      assert(motorsOff());
      if (step != 7) { assert(positioningStep == step); advance(10000); assert(positioningStep == step); }
      else assert(positioningStep == 0 && softwareStopLatched);
    }
    assert(fault == "none");
    assert(molderServo.angle == MOLDER_CLOSED_ANGLE);
    assert(!mixerGateServo.attached());
    puts("Positioning: 7 operator-paced steps, actual process times, no unrelated outputs or automatic next step");
    return 0;
  }
  if (mode == "local_session") {
    // Clock rollover must not leave a heating window enabled indefinitely.
    StandaloneCycle clockCheck;
    const uint32_t nearWrap = UINT32_MAX - 1000U;
    clockCheck.preheat(nearWrap);
    assert(!clockCheck.expired((uint32_t)(nearWrap + PAPERBRIQ_LOCAL_PREHEAT_MAX_MS - 1)));
    assert(clockCheck.expired((uint32_t)(nearWrap + PAPERBRIQ_LOCAL_PREHEAT_MAX_MS)));
    clockCheck.phase = StandaloneCycle::READY;
    assert(!clockCheck.expired((uint32_t)(nearWrap + PAPERBRIQ_LOCAL_WARM_IDLE_MS - 1)));
    assert(clockCheck.expired((uint32_t)(nearWrap + PAPERBRIQ_LOCAL_WARM_IDLE_MS)));
    clockCheck.begin();
    assert(clockCheck.sessionAt == nearWrap);
    assert(clockCheck.expired((uint32_t)(nearWrap + PAPERBRIQ_LOCAL_SESSION_MAX_MS)));
    const auto usb = [](char c) { Serial.input.push_back(c); serviceSerialCommands(); };
    const std::string fault = argc > 2 ? argv[2] : "none";
    const bool readyStage = argc > 3 && std::string(argv[3]) == "ready";
    advance(3000); usb('R'); usb('W');
    assert(localCycle.phase == StandaloneCycle::PREHEATING);
    assert(!automaticProcessIsActive() && motorsOff() && testOutputs[4] == HIGH);
    assert(testOutputs[3] == LOW);
    const uint32_t sessionAt = localCycle.sessionAt;
    for (char c : std::string("RGPZ1234567EFTBYJUVLQ")) usb(c);
    assert(!startAutomaticProcess(true) && !startComponentTest() && !armMotorTest());
    assert(!runChamberCommand("UNLOCK") && !chamber.unlockOn);
    assert(localCycle.phase == StandaloneCycle::PREHEATING && motorsOff());
    // Waiting is not temperature confirmation: time alone never starts forming.
    advance(5000);
    assert(localCycle.phase == StandaloneCycle::PREHEATING && motorsOff());
    if (readyStage || fault == "none") {
      advance(3000); usb('W');
      assert(localCycle.phase == StandaloneCycle::FORMING && testOutputs[4] == LOW);
      for (unsigned n = 0; n < 100000 && localCycle.phase != StandaloneCycle::READY; ++n) advance(10);
      assert(localCycle.phase == StandaloneCycle::READY && testOutputs[4] == HIGH);
      assert(motorsOff() && testOutputs[3] == LOW && !softwareStopLatched);
      // Ready does not start the next batch by itself or allow manual motion/unlock.
      advance(10000);
      for (char c : std::string("RGPZ1234567EFTBYJUVLQ")) usb(c);
      assert(localCycle.phase == StandaloneCycle::READY && motorsOff());
      assert(!chamber.unlockOn && !pumpTestActive && !heaterTestActive);
    }
    if (fault != "none") {
      if (fault == "x") usb('X');
      else if (fault == "door") Wire.inputs[0x24] |= ChamberControl::DOOR;
      else if (fault == "stop") Wire.inputs[0x20] |= STOP_BUTTON_MASK;
      else if (fault == "emergency") Wire.inputs[0x20] |= EMERGENCY_BUTTON_MASK;
      else if (fault == "buttons") Wire.missing[0x20] = true;
      else if (fault == "status") Wire.missing[0x24] = true;
      else if (fault == "timeout") {
        testMillis = localCycle.heatAt + (readyStage ? PAPERBRIQ_LOCAL_WARM_IDLE_MS : PAPERBRIQ_LOCAL_PREHEAT_MAX_MS);
      } else assert(false);
      advance(300);
      assert(!localCycle.active() && softwareStopLatched && motorsOff());
      assert(testOutputs[3] == LOW && testOutputs[4] == LOW);
      Wire.inputs[0x20] = 0xF3; Wire.inputs[0x24] = 0xFB;
      Wire.missing[0x20] = Wire.missing[0x24] = false;
      advance(5000); usb('W'); assert(!localCycle.active());
      puts("Heating session: fault/deadline shuts down; no resume without Reset"); return 0;
    }
    // A new physical Start is allowed without Reset only after the drying window.
    Wire.inputs[0x20] &= (uint8_t)~START_BUTTON_MASK; advance(150);
    Wire.inputs[0x20] |= START_BUTTON_MASK; advance(150);
    assert(localCycle.phase == StandaloneCycle::FORMING && testOutputs[4] == LOW);
    assert(localCycle.sessionAt == sessionAt); // New batches cannot extend session deadline.
    for (unsigned n = 0; n < 100000 && localCycle.phase != StandaloneCycle::READY; ++n) advance(10);
    assert(localCycle.phase == StandaloneCycle::READY);
    usb('W'); assert(localCycle.phase == StandaloneCycle::FORMING);
    testMillis = sessionAt + PAPERBRIQ_LOCAL_SESSION_MAX_MS;
    serviceStandaloneCycle();
    assert(!localCycle.active() && softwareStopLatched && motorsOff());
    assert(testOutputs[3] == LOW && testOutputs[4] == LOW);
    puts("Heating session: two batches, button/USB parity, no deadline extension, session cutoff during forming");
    return 0;
  }
  if (mode == "local_batch" || mode == "local_batch_buttons" || mode == "local_water_timeout") {
    const auto usb = [](char c) { Serial.input.push_back(c); serviceSerialCommands(); };
    const auto press = [](uint8_t mask) {
      Wire.inputs[0x20] &= (uint8_t)~mask;
      advance(150);
      Wire.inputs[0x20] |= mask;
      advance(150);
    };
    const bool buttons = mode == "local_batch_buttons";
    advance(3000);
    if (buttons) press(START_BUTTON_MASK); else usb('W');
    assert(!localCycle.active() && motorsOff());
    if (buttons) { press(RESET_BUTTON_MASK); press(START_BUTTON_MASK); }
    else { usb('R'); usb('W'); }
    assert(localCycle.phase == StandaloneCycle::PREHEATING);
    assert(motorsOff() && testOutputs[3] == LOW && testOutputs[4] == HIGH);
    advance(3000); // Operator checks the external thermostat before confirming.
    if (buttons) press(START_BUTTON_MASK); else usb('W');
    assert(localCycle.phase == StandaloneCycle::FORMING);
    if (mode == "local_water_timeout") {
      advance(DIRECTION_CHANGE_PAUSE_MS + PROCESS_CLOSE_TRAVEL_MS + 100);
      assert(localCycle.pumping() && testOutputs[3] == HIGH);
      // Stop advancing the process states, but let the output guard run.
      testMillis = localCycle.pumpAt + LOCAL_WATER_TIMEOUT_MS;
      serviceStandaloneCycle();
      assert(!localCycle.active() && softwareStopLatched && motorsOff());
      assert(testOutputs[3] == LOW && testOutputs[4] == LOW);
      advance(5000); usb('W'); assert(!localCycle.active());
      puts("Stage-synchronized water: stalled process times out and cannot auto-resume");
      return 0;
    }
    bool pumpSeen = false, heatSeen = false;
    bool molderOpenSeen = false, pusherSeen = false;
    uint32_t mixerStoppedAt = 0;
    unsigned long presserReturnOnMs = 0, pusherOutOnMs = 0, pusherBackOnMs = 0;
    const std::string fault = argc > 2 ? argv[2] : "none";
    const bool duringHeat = argc > 3 && std::string(argv[3]) == "heat";
    uint32_t pumpFirstAt = 0, pumpEndedAt = 0;
    for (int n = 0; n < 80000 && localCycle.active() &&
         localCycle.phase != StandaloneCycle::READY; ++n) {
      const bool pumping = testOutputs[3] == HIGH, heating = testOutputs[4] == HIGH;
      if (testOutputs[PRESSER_LPWM]) {
        assert(!testOutputs[PRESSER_RPWM]);
        presserReturnOnMs += 10;
      }
      if (testOutputs[PUSHER_IN4]) {
        assert(!testOutputs[PUSHER_IN3]);
        pusherOutOnMs += 10;
      }
      if (testOutputs[PUSHER_IN3]) {
        assert(!testOutputs[PUSHER_IN4]);
        pusherBackOnMs += 10;
      }
      assert(!(pumping && heating));
      if (pumping) {
        if (!pumpSeen) pumpFirstAt = millis();
        pumpSeen = true;
        assert(automaticProcessState >= PROCESS_SHREDDING_AND_MIXING &&
               automaticProcessState <= PROCESS_WAITING_FOR_MIXER_GATE_TO_CLOSE);
        assert(molderServo.angle == MOLDER_CLOSED_ANGLE);
        if (automaticProcessState >= PROCESS_WAITING_TO_OPEN_GATE) assert(motorsOff());
        if (automaticProcessState == PROCESS_RELEASING_MATERIAL)
          assert(mixerGateServo.angle == MIXER_GATE_OPEN_ANGLE);
      }
      const bool waterStage = automaticProcessState >= PROCESS_SHREDDING_AND_MIXING &&
          automaticProcessState <= PROCESS_WAITING_FOR_MIXER_GATE_TO_CLOSE;
      const bool beforeWaterDeadline = !pumpSeen || millis() - pumpFirstAt < LOCAL_PUMP_RUN_MS;
      assert(pumping == (waterStage && beforeWaterDeadline));
      if (automaticProcessState == PROCESS_SHREDDING_AND_MIXING ||
          automaticProcessState == PROCESS_MIXER_FINISHING) {
        assert(pumping == beforeWaterDeadline); // Mixer continues after the shorter water dose.
        const unsigned long phase = (millis() - pumpFirstAt) % 24000UL;
        const bool forward = phase < 10000UL;
        const bool reverse = phase >= 12000UL && phase < 22000UL;
        assert(testOutputs[MIXER_RPWM] ==
               (PROCESS_MIXER_USE_RPWM ? forward : reverse));
        assert(testOutputs[MIXER_LPWM] ==
               (PROCESS_MIXER_USE_RPWM ? reverse : forward));
      }
      if (automaticProcessState == PROCESS_WAITING_TO_OPEN_GATE && !mixerStoppedAt)
        mixerStoppedAt = millis();
      if (automaticProcessState == PROCESS_WAITING_FOR_MOLDER_TO_OPEN ||
          automaticProcessState == PROCESS_PUSHER_EXTENDING ||
          automaticProcessState == PROCESS_PUSHER_RETRACTING) {
        assert(molderServo.attached() && molderServo.angle == MOLDER_OPEN_ANGLE);
        molderOpenSeen = true;
      }
      if (testOutputs[PUSHER_IN3] || testOutputs[PUSHER_IN4]) {
        assert(molderOpenSeen && molderServo.angle == MOLDER_OPEN_ANGLE);
        pusherSeen = true;
      }
      if (pumpSeen && !pumping && !pumpEndedAt) pumpEndedAt = millis();
      if (heating) {
        assert(pumpSeen && motorsOff() && !automaticProcessIsActive());
        assert(!heaterReadingValid); // External thermostat, not invented DHT readings.
        assert(mixerGateServo.angle == MIXER_GATE_CLOSED_ANGLE);
        assert(molderServo.angle == MOLDER_CLOSED_ANGLE);
        const auto heatAt = localCycle.heatAt;
        if (!heatSeen) {
          const auto writes = mixerGateServo.writes + molderServo.writes;
          for (char c : std::string("WRPG1234567DOTFYJU")) usb(c);
          assert(localCycle.heatAt == heatAt && motorsOff());
          assert(writes == mixerGateServo.writes + molderServo.writes);
          assert(!chamber.unlockOn && !pumpTestActive && !heaterTestActive);
        }
        heatSeen = true;
        assert(millis() - heatAt < PAPERBRIQ_LOCAL_HEAT_MS);
      }
      if (fault != "none" && (duringHeat ? heating : pumping)) {
        if (fault == "x") usb('X');
        else if (fault == "door") Wire.inputs[0x24] |= ChamberControl::DOOR;
        else if (fault == "stop") Wire.inputs[0x20] |= STOP_BUTTON_MASK;
        else if (fault == "emergency") Wire.inputs[0x20] |= EMERGENCY_BUTTON_MASK;
        else if (fault == "buttons") Wire.missing[0x20] = true;
        else if (fault == "status") Wire.missing[0x24] = true;
        else assert(false);
        advance(150);
        assert(!localCycle.active() && !automaticProcessIsActive() && motorsOff());
        assert(testOutputs[3] == LOW && testOutputs[4] == LOW && softwareStopLatched);
        Wire.inputs[0x20] = 0xF3; Wire.inputs[0x24] = 0xFB;
        Wire.missing[0x20] = Wire.missing[0x24] = false;
        advance(5000); usb('W');
        assert(!localCycle.active() && motorsOff() && testOutputs[3] == LOW && testOutputs[4] == LOW);
        puts("Local batch: fault removes pump/heat/motion; no automatic restart"); return 0;
      }
      advance(10);
    }
    assert(fault == "none" && pumpSeen && heatSeen && molderOpenSeen && pusherSeen);
    assert(localCycle.phase == StandaloneCycle::READY && !softwareStopLatched);
    assert(pumpEndedAt - pumpFirstAt == LOCAL_WATER_EXPECTED_MS);
    assert(presserReturnOnMs == PAPERBRIQ_PRESSER_RETURN_MS);
    assert(pusherOutOnMs == PAPERBRIQ_PUSHER_FROM_PARK_MS);
    assert(pusherBackOnMs == PAPERBRIQ_PUSHER_RETURN_TO_PARK_MS);
    assert(mixerStoppedAt - pumpFirstAt == BENCH_SHREDDER_RUN_MS + PROCESS_MIXER_FINISH_MS);
    assert(motorsOff() && testOutputs[3] == LOW && testOutputs[4] == HIGH);
    advance(PAPERBRIQ_LOCAL_WARM_IDLE_MS + 20);
    assert(!localCycle.active() && softwareStopLatched && testOutputs[4] == LOW);
    assert(Serial.output.find("HEAT TIMEOUT") != std::string::npos);
    advance(10000); usb('W'); assert(!localCycle.active());
    puts("Local session: preheat, forming without heat, drying, bounded warm idle; water/return times preserved");
    return 0;
  }
  if (mode == "local_cycle") {
    const auto usb = [](char c) { Serial.input.push_back(c); serviceSerialCommands(); };
    advance(3000);
    usb('G'); // A local cycle still needs an explicit Reset.
    assert(!automaticProcessIsActive() && motorsOff());
    usb('R'); assert(!softwareStopLatched);
    usb('P'); assert(!automaticProcessIsActive()); // Normal Start still needs a dose/ESP.
    usb('G');
    assert(automaticProcessIsActive() && !sensorControlledProcess);
    const auto began = processStateStartedAtMs;
    usb('G'); assert(processStateStartedAtMs == began); // No restart/extension.
    const std::string fault = argc > 2 ? argv[2] : "none";
    const int target = argc > 3 ? atoi(argv[3]) : PROCESS_SHREDDING_AND_MIXING;
    bool seen[PROCESS_WAITING_FOR_MOLDER_TO_CLOSE + 1] = {};
    // Current user-selected release/travel times exceed the old 150s harness cap.
    for (int n = 0; n < 30000 && automaticProcessIsActive(); ++n) {
      seen[automaticProcessState] = true;
      assert(testOutputs[3] == LOW && testOutputs[4] == LOW);
      assert(!production.active() && !chamber.unlockOn && !chamber.fanOn);
      if (fault != "none" && automaticProcessState == target) {
        if (fault == "stop") Wire.inputs[0x20] |= STOP_BUTTON_MASK;
        else if (fault == "emergency") Wire.inputs[0x20] |= EMERGENCY_BUTTON_MASK;
        else if (fault == "door") Wire.inputs[0x24] |= ChamberControl::DOOR;
        else if (fault == "buttons") Wire.missing[0x20] = true;
        else if (fault == "status") Wire.missing[0x24] = true;
        else if (fault == "x") usb('X');
        else assert(false);
        advance(150);
        assert(softwareStopLatched && !automaticProcessIsActive() && motorsOff());
        assert(testOutputs[3] == LOW && testOutputs[4] == LOW);
        Wire.inputs[0x20] = 0xF3; Wire.inputs[0x24] = 0xFB;
        Wire.missing[0x20] = Wire.missing[0x24] = false;
        advance(5000);
        assert(softwareStopLatched && !automaticProcessIsActive() && motorsOff());
        usb('G'); assert(!automaticProcessIsActive());
        puts("Local unloaded cycle: fault stops sequence; no automatic restart"); return 0;
      }
      advance(10);
    }
    assert(fault == "none"); // Requested fault stage must actually be reached.
    assert(!automaticProcessIsActive() && motorsOff());
    for (int state = PROCESS_WAITING_TO_CLOSE_GATE; state <= PROCESS_WAITING_FOR_MOLDER_TO_CLOSE; ++state)
      if (state != PROCESS_WAITING_FOR_PAPER) assert(seen[state]);
    assert(mixerGateServo.angle == MIXER_GATE_CLOSED_ANGLE);
    assert(molderServo.angle == MOLDER_CLOSED_ANGLE);
    assert(Serial.output.find("Unloaded cycle ended") != std::string::npos);
    advance(10000); assert(!automaticProcessIsActive() && motorsOff());
    usb('P'); assert(!automaticProcessIsActive());
    // A prepared production dose cannot be silently used by a local dry run.
    production.kind = 'P'; production.phase = ProductionTimer::WAITING;
    usb('G'); assert(!automaticProcessIsActive() && motorsOff());
    puts("Local unloaded cycle: all forming stages, one pass, no pump/heat/ESP; production gates retained");
    return 0;
  }
#endif
#ifdef PAPERBRIQ_PRODUCTION_BUILD
  assert(Serial.output.find("PaperBriqController - PRODUCTION") != std::string::npos);
  for (char c : std::string("jJyY1234567eEtTfFbBoOcCdDvVlL")) Serial.input.push_back(c);
  serviceSerialCommands();
  assert(motorsOff() && testOutputs[3] == LOW && testOutputs[4] == LOW);
  assert(!mixerGateServo.attached() && !molderServo.attached() && !chamber.fanOn);
#endif
  if (mode == "preheat") {
    const auto cmd = [](const char *s) { std::string line(s); handleRemoteCommandFrame(&line[0]); };
    advance(3000);
    cmd("CMD|missing-temperature|HH:1234ABCD:600");
    assert(!production.active() && testOutputs[4] == LOW);
    simulateThermalSensor = true; simulatedTemperature = 25;
    advance(10000);
    cmd("CMD|warm|HH:1234ABCE:600");
    assert(production.active() && thermal.stage == ThermalCycle::WARMING && testOutputs[4] == HIGH);
    const auto preheatStarted = production.startedAt;
    requestDhtCheck(); serviceProduction();
    assert(testOutputs[4] == HIGH); // Read-only S does not invalidate a fresh heating sample.
    advance(10);
    assert(!armMotorTest() && !startAutomaticProcess());
    cmd("CMD|pump-conflict|PP:2345ABCD:20");
    assert(production.kind == 'H' && testOutputs[3] == LOW && motorsOff());
    advance(5000);
    cmd("CMD|warm|HH:1234ABCE:600"); assert(production.startedAt == preheatStarted);
    simulatedTemperature = PAPERBRIQ_CHAMBER_TARGET_C;
    advance(15000); assert(thermal.stage == ThermalCycle::READY && testOutputs[4] == HIGH);
    Wire.inputs[0x24] |= ChamberControl::DOOR;
    advance(100); assert(!production.active() && testOutputs[4] == LOW);
    Wire.inputs[0x24] &= ~ChamberControl::DOOR;
    advance(3000); assert(testOutputs[4] == LOW); // Closing never resumes heating.
    // Drying waits for stable target temperature, preserving all selected time.
    simulatedTemperature = 25; advance(3000);
    cmd("CMD|dry|DD:2345ABCE:60");
    advance(15000);
    assert(production.phase == ProductionTimer::WAITING && production.remaining(millis()) == 60);
    simulatedTemperature = PAPERBRIQ_CHAMBER_TARGET_C;
    advance(15000); assert(thermal.stage == ThermalCycle::DRYING && production.on());
    assert(production.elapsed(millis()) < 6);
    advance(60000); assert(!production.active() && testOutputs[4] == LOW);
    // The preheat ceiling includes both warm-up and ready/holding time.
    cmd("CMD|bounded-preheat|HH:3456ABCD:30");
    advance(30000); assert(!production.active() && testOutputs[4] == LOW);
    for (int fault=0; fault<6; ++fault) {
      simulatedTemperature = PAPERBRIQ_CHAMBER_TARGET_C; simulateThermalSensor = true;
      advance(12000);
      std::string line = "CMD|thermal-fault-" + std::to_string(fault) + "|HH:4567ABC" + std::to_string(fault) + ":60";
      cmd(line.c_str()); assert(testOutputs[4] == HIGH);
      if (fault == 0) { simulateThermalSensor = false; advance(5100); }
      if (fault == 1) { simulatedTemperature = PAPERBRIQ_CHAMBER_TARGET_C + 5; advance(3000); }
      if (fault == 2) Wire.inputs[0x20] |= STOP_BUTTON_MASK;
      if (fault == 3) Wire.inputs[0x20] |= EMERGENCY_BUTTON_MASK;
      if (fault == 4) Wire.missing[0x24] = true;
      if (fault == 5) cmd("CMD|thermal-stop|STOP");
      advance(100); assert(testOutputs[4] == LOW && !production.active());
      Wire.inputs[0x20] &= ~(STOP_BUTTON_MASK|EMERGENCY_BUTTON_MASK);
      Wire.missing[0x24] = false;
      advance(3000); assert(testOutputs[4] == LOW);
    }
    simulatedTemperature = 25; advance(3000);
    cmd("CMD|never-warm|DD:5678ABCD:60");
    advance(600000);
    assert(production.phase == ProductionTimer::FAULT && testOutputs[4] == LOW);
    puts("Preheat: temperature qualification, bounded hold, dry countdown, no concurrent motors, door/Stop/emergency/temperature faults and no restart passed");
    return 0;
  }
  if (mode == "production_disabled") {
    simulateThermalSensor = true;
    advance(3000);
    char p[]="CMD|disabled-pump|PP:1234ABCD:60";
    char d[]="CMD|disabled-dry|DD:2345ABCD:1800";
    handleRemoteCommandFrame(p); handleRemoteCommandFrame(d);
    handlePaperSignal('0'); advance(250); handlePaperSignal('1');
    assert(!production.active() && testOutputs[3]==LOW && testOutputs[4]==LOW);
    assert(espLink.output.find("production_blocked")!=std::string::npos);
    assert(armMotorTest() && !startAutomaticProcess());
    latchSoftwareStop(); advance(3000);
    char bounded[]="CMD|bounded-dry|DD:3456ABCD:600";
    handleRemoteCommandFrame(bounded); assert(production.active() && testOutputs[4] == HIGH);
    advance(610020); assert(!production.active() && testOutputs[4] == LOW && motorsOff());
    puts("Default build: pump/over-limit drying blocked; explicit 10-minute drying stops locally"); return 0;
  }
  if (mode == "production") {
    simulateThermalSensor = true;
    const auto cmd = [](const char *s) { std::string line(s); handleRemoteCommandFrame(&line[0]); };
    const auto feedFor = [](char value, unsigned long duration) {
      for (unsigned long n=0; n<duration; n+=250) { handlePaperSignal(value); advance(250); }
    };
    advance(3000);
    cmd("CMD|dry-start|DD:1234ABCD:60");
    assert(production.active() && testOutputs[4] == HIGH && motorsOff());
    assert(!armMotorTest() && !startAutomaticProcess() && !startComponentTest());
    advance(10000); assert(production.on());
    advance(2000);
    cmd("CMD|dry-edit|DT:1234ABCD:90"); assert(production.total == 90 && production.elapsed(millis()) == 2);
    cmd("CMD|dry-stale|DT:1234ABCE:100"); assert(production.total == 90);
    cmd("CMD|dry-over|DT:1234ABCD:3601"); assert(production.total == 90);
    cmd("CMD|dry-shorten|DT:1234ABCD:1"); assert(!production.on() && testOutputs[4] == LOW);
    cmd("CMD|dry-replay|DD:1234ABCD:60"); assert(!production.active());
    cmd("CMD|pump-arm|PP:2345ABCD:20"); assert(production.phase == ProductionTimer::WAITING && testOutputs[3] == LOW);
    handlePaperSignal('0'); advance(250); handlePaperSignal('1'); assert(!production.on());
    assert(armMotorTest()); assert(startAutomaticProcess());
    for (int i=0;i<2000 && automaticProcessState!=PROCESS_WAITING_FOR_PAPER;++i) advance(10);
    assert(automaticProcessState==PROCESS_WAITING_FOR_PAPER && motorsOff());
    handlePaperSignal('1'); assert(!production.on());
    handlePaperSignal('0'); advance(250); handlePaperSignal('1');
    assert(production.on() && testOutputs[3] == HIGH && automaticProcessState==PROCESS_SHREDDING_AND_MIXING);
    const auto startAt = production.startedAt;
    feedFor('1', 11000);
    assert(automaticProcessState==PROCESS_SHREDDING_AND_MIXING && production.on());
    assert(testOutputs[SHREDDER_RPWM] == SHREDDER_FORWARD_DUTY);
    feedFor('0', 1000);
    assert(automaticProcessState==PROCESS_MIXER_FINISHING && testOutputs[SHREDDER_RPWM] == LOW);
    assert(testOutputs[MIXER_LPWM] == HIGH);
    feedFor('1', 250); assert(production.startedAt == startAt);
    assert(automaticProcessState==PROCESS_SHREDDING_AND_MIXING && testOutputs[SHREDDER_RPWM] != LOW);
    cmd("CMD|pump-edit|PT:2345ABCD:12");
    feedFor('1', 1000); assert(!production.on() && testOutputs[3] == LOW);
    // A completed water dose never repeats, even when paper arrives again.
    feedFor('0', 4500); assert(automaticProcessState==PROCESS_MIXER_FINISHING);
    feedFor('1', 250); assert(automaticProcessState==PROCESS_SHREDDING_AND_MIXING);
    assert(!production.on() && production.startedAt == startAt && testOutputs[3] == LOW);
    feedFor('0', 5000); assert(automaticProcessState==PROCESS_WAITING_TO_OPEN_GATE);
    // Paper after feeding ends must not restart the shredder during transfer.
    feedFor('1', 250); assert(testOutputs[SHREDDER_RPWM] == LOW);
    advance(90000);
    assert(automaticProcessState == PROCESS_IDLE && motorsOff());
    assert(espLink.output.find("PROCESS_COMPLETED") != std::string::npos);
    assert(!startAutomaticProcess()); // Completed water dose cannot start another batch.
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
    for (int i=0;i<2000 && automaticProcessState!=PROCESS_WAITING_FOR_PAPER;++i) advance(10);
    // Repeated clear heartbeats leave the machine waiting without motion.
    for (int i=0;i<80;++i) { handlePaperSignal('0'); advance(250); }
    assert(motorsOff() && !production.on() && !softwareStopLatched);
    // Losing those heartbeats must cancel the armed batch, not restart later.
    advance(2000);
    assert(softwareStopLatched && !production.active() && motorsOff() && testOutputs[3] == LOW);
    handlePaperSignal('0'); advance(250); handlePaperSignal('1');
    assert(motorsOff() && !production.on());
    // Lost sensor heartbeats stop both active shredding and the feeding gap.
    // Test a late frame arriving before the main loop can service the timeout.
    for (int gap=0;gap<2;++gap) {
      advance(3000);
      cmd(gap ? "CMD|gap-loss|PP:5678ABCE:20" : "CMD|shred-loss|PP:5678ABCD:20");
      assert(armMotorTest() && startAutomaticProcess());
      for (int i=0;i<2000 && automaticProcessState!=PROCESS_WAITING_FOR_PAPER;++i) advance(10);
      handlePaperSignal('0'); advance(250); handlePaperSignal('1'); advance(10);
      assert(testOutputs[SHREDDER_RPWM] != LOW);
      if (gap) { handlePaperSignal('0'); advance(10); }
      testMillis += 1500;
      handlePaperSignal('1');
      assert(softwareStopLatched && motorsOff() && !production.active() && testOutputs[3] == LOW);
      handlePaperSignal('0'); advance(250); handlePaperSignal('1');
      assert(motorsOff() && !production.on());
    }
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
    assert(!chamber.doorClosed && !localCycle.active());
    Wire.inputs[0x24] &= (uint8_t)~ChamberControl::DOOR;
    advance(100);
    press(RESET_BUTTON_MASK);
    assert(chamber.doorClosed);
    assert(!softwareStopLatched && motorsOff());
    press(START_BUTTON_MASK);
    assert(localCycle.phase == StandaloneCycle::PREHEATING && motorsOff());
    press(START_BUTTON_MASK); // Operator confirms preheat in the current W/Start flow.
    assert(automaticProcessIsActive());
    Wire.inputs[0x20] |= STOP_BUTTON_MASK;
    advance(150);
    assert(softwareStopLatched && !automaticProcessIsActive() && motorsOff());
    assert(!localCycle.active() && testOutputs[3] == LOW && testOutputs[4] == LOW);
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
