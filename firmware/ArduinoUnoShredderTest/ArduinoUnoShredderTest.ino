#include <Arduino.h>
#include <Wire.h>

// Standalone diagnostic for the installed PaperBriq Uno wiring.
// No automatic motion, Wi-Fi, ESP32, heater, pump, or servo commands.
const uint8_t SHREDDER_RPWM = 5;
const uint8_t SHREDDER_LPWM = 6;
const uint8_t FORWARD_PWM = 153; // Same 60% command as the process sketch.
const uint8_t REVERSE_PWM = 77;  // Same 30% command as the process sketch.
const unsigned long RUN_MS = 5000UL;
const unsigned long VOLTAGE_CHECK_MS = 30000UL;
const unsigned long SETTLE_MS = 2500UL;
static_assert(RUN_MS > 0 && RUN_MS <= 11000UL, "Test pulse must be 1..11000 ms");
static_assert(VOLTAGE_CHECK_MS > 0 && VOLTAGE_CHECK_MS <= 30000UL,
              "Voltage check must be 1..30000 ms");

const uint8_t BUTTON_BOARD = 0x20;
const uint8_t STATUS_BOARD = 0x24;
const uint8_t STOP_MASK = 1 << 2;
const uint8_t EMERGENCY_MASK = 1 << 3;

enum RunState { IDLE, WAITING, RUNNING };
RunState runState = IDLE;
bool armed = false;
bool forwardRequested = true;
bool contactsHealthy = false;
bool buttonsOnline = false, statusOnline = false;
uint8_t buttons = 0xFF, statusPins = 0xFF;
unsigned long healthySince = 0, lastPoll = 0, lastStopped = 0, runStarted = 0;
unsigned long activeRunMs = RUN_MS;

void stopOutputs() {
  analogWrite(SHREDDER_RPWM, 0);
  analogWrite(SHREDDER_LPWM, 0);
  runState = IDLE;
  lastStopped = millis();
}

void stopTest() {
  stopOutputs();
  armed = false;
}

bool sampleBoard(uint8_t address, uint8_t &value) {
  // Release all inputs; active-LOW status/relay outputs remain OFF.
  // Keep door P2 released even though it shares the status-output board.
  Wire.beginTransmission(address);
  Wire.write(0xFF);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(address, static_cast<uint8_t>(1)) != 1) return false;
  value = Wire.read();
  return true;
}

void serviceSafety(bool force = false) {
  const unsigned long now = millis();
  if (!force && now - lastPoll < 10UL) return;
  lastPoll = now;
  buttonsOnline = sampleBoard(BUTTON_BOARD, buttons);
  statusOnline = sampleBoard(STATUS_BOARD, statusPins);
  // The drying-chamber door does not gate this shredder-only diagnostic.
  // Keep both expanders online so unrelated relay outputs stay commanded OFF.
  const bool healthy = buttonsOnline && statusOnline &&
      !(buttons & (STOP_MASK | EMERGENCY_MASK));
  if (!healthy) {
    contactsHealthy = false;
    if (armed || runState != IDLE) {
      stopTest();
      Serial.println(F("FAULT: Stop/E-stop/expander. Outputs OFF; inspect, then R."));
    }
  } else if (!contactsHealthy) {
    contactsHealthy = true;
    healthySince = now;
  }
}

bool ready() {
  return contactsHealthy && millis() - healthySince >= 50UL;
}

void printStatus() {
  Serial.print(F("0x20 online=")); Serial.print(buttonsOnline);
  Serial.print(F(" Stop closed=")); Serial.print(buttonsOnline && !(buttons & STOP_MASK));
  Serial.print(F(" E-stop closed=")); Serial.println(buttonsOnline && !(buttons & EMERGENCY_MASK));
  Serial.print(F("0x24 online=")); Serial.println(statusOnline);
  Serial.print(F("Armed=")); Serial.print(armed);
  Serial.print(F(" State=")); Serial.println(static_cast<int>(runState));
  Serial.println(F("State 0=idle, 1=settling, 2=output commanded; no motion feedback."));
}

void printHelp() {
  Serial.println(F("SHREDDER ONLY | USB 9600 baud | no ESP32 needed"));
  Serial.println(F("R=arm  1=forward  2=reverse  X=stop  I=status  H=help"));
  Serial.println(F("V=15s forward voltage check; X stops. Stop if stalled/humming."));
  Serial.print(F("Pulse milliseconds=")); Serial.println(RUN_MS);
  Serial.println(F("D5=RPWM, D6=LPWM; forward 60%, reverse 30%; 2.5s settling."));
  Serial.println(F("Physical Stop/E-stop monitored. Drying door not used in this test."));
  Serial.println(F("Keep shredder guards in place. Start/Reset buttons unused."));
}

void setup() {
  // Keep all installed motor/enable outputs OFF; never attach either servo.
  for (uint8_t pin = 3; pin <= 12; ++pin) {
    digitalWrite(pin, LOW);
    pinMode(pin, OUTPUT);
  }
  stopTest();
  Serial.begin(9600);
  Wire.begin();
  Wire.setWireTimeout(25000, true);
  serviceSafety(true);
  printHelp();
  printStatus();
}

void loop() {
  serviceSafety();
  while (Serial.available()) {
    const char command = Serial.read();
    if (command == 'x' || command == 'X') {
      stopTest(); Serial.println(F("STOPPED; R required."));
    } else if (command == 'r' || command == 'R') {
      serviceSafety(true);
      if (runState == IDLE && ready()) {
        armed = true; Serial.println(F("ARMED; 1/2=5s, V=15s forward."));
      } else {
        Serial.println(F("R blocked: busy or unsafe; send I."));
      }
    } else if (command == '1' || command == '2' || command == 'v' || command == 'V') {
      serviceSafety(true);
      if (!armed || !ready() || runState != IDLE) {
        Serial.println(F("BLOCKED: R/contacts/busy; send I."));
      } else {
        stopOutputs();
        forwardRequested = command != '2';
        activeRunMs = (command == 'v' || command == 'V') ? VOLTAGE_CHECK_MS : RUN_MS;
        runState = WAITING;
        Serial.println(F("Accepted; waiting 2.5s. X cancels."));
      }
    } else if (command == 'i' || command == 'I') {
      serviceSafety(true); printStatus();
    } else if (command == 'h' || command == 'H') {
      printHelp();
    }
  }
  serviceSafety();
  if (!armed || !ready()) return;
  const unsigned long now = millis();
  if (runState == WAITING && now - lastStopped >= SETTLE_MS) {
    // Opposite output stays LOW. Never reverse a running test on a new key.
    analogWrite(forwardRequested ? SHREDDER_LPWM : SHREDDER_RPWM, 0);
    analogWrite(forwardRequested ? SHREDDER_RPWM : SHREDDER_LPWM,
                forwardRequested ? FORWARD_PWM : REVERSE_PWM);
    runState = RUNNING;
    runStarted = now;
    Serial.print(F("Auto-stop after ms=")); Serial.println(activeRunMs);
    Serial.println(forwardRequested ? F("FORWARD commanded: D5 PWM, D6 LOW") :
                                     F("REVERSE commanded: D6 PWM, D5 LOW"));
  } else if (runState == RUNNING && now - runStarted >= activeRunMs) {
    stopOutputs();
    Serial.println(F("Pulse ended; outputs OFF. Check actual movement."));
  }
}
