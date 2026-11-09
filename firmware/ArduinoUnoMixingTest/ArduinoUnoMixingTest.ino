#include <Arduino.h>
#include <Wire.h>
#include <Servo.h>

// Isolated PaperBriq mixing-stage checks. No ESP32 or production process.
const uint8_t PUMP_PIN = 3;
const uint8_t SHRED_FORWARD_PIN = 5, SHRED_REVERSE_PIN = 6;
const uint8_t MIX_FORWARD_PIN = 9, MIX_REVERSE_PIN = 10;
const uint8_t GATE_PIN = A0;
const uint8_t GATE_CLOSED = 34, GATE_OPEN = 130;
const uint8_t SHRED_FORWARD_PWM = 153, SHRED_REVERSE_PWM = 77;
const bool COMBINED_MIX_RPWM = false; // Current process direction: D10, manual 4.
const unsigned long RUN_MS = 5000UL;
const unsigned long SETTLE_MS = 2500UL;
const unsigned long GATE_TRAVEL_MS = 1500UL;
const unsigned long GATE_OPEN_HOLD_MS = 3000UL;
const unsigned long PUMP_REST_MS = 10000UL;
const unsigned long ACTION_LIMIT_MS = 30000UL;
static_assert(RUN_MS > 0 && RUN_MS <= 5000UL, "Combined check limited to 5s powered run");

const uint8_t BUTTON_BOARD = 0x20, STATUS_BOARD = 0x24;
const uint8_t STOP_MASK = 1 << 2, EMERGENCY_MASK = 1 << 3;
enum TestState { IDLE, SETTLING, MANUAL_RUN, MANUAL_GATE,
                 CLOSE_BEFORE_RUN, COMBINED_RUN, SETTLE_BEFORE_OPEN,
                 OPENING, OPEN_HOLD, CLOSING };
TestState state = IDLE;
Servo mixerGate;
char action = 0;
bool armed = false, contactsHealthy = false, buttonsOnline = false, statusOnline = false;
bool pumpOn = false, pumpHasRun = false;
uint8_t buttonInputs = 0xFF;
unsigned long lastPoll = 0, healthySince = 0, stageAt = 0, actionAt = 0, pumpStoppedAt = 0;

void outputsOff() {
  digitalWrite(PUMP_PIN, LOW);
  analogWrite(SHRED_FORWARD_PIN, 0);
  analogWrite(SHRED_REVERSE_PIN, 0);
  digitalWrite(MIX_FORWARD_PIN, LOW);
  digitalWrite(MIX_REVERSE_PIN, LOW);
  if (pumpOn) pumpStoppedAt = millis();
  pumpOn = false;
}

void stopTest() {
  outputsOff();
  state = IDLE;
  armed = false;
  // No new gate target on stop/fault. It continues holding the last command.
}

bool releaseBoard(uint8_t address) {
  Wire.beginTransmission(address);
  Wire.write(0xFF); // Unrelated active-LOW relays off; all input pins released.
  return Wire.endTransmission() == 0;
}

void serviceSafety(bool force = false) {
  const unsigned long now = millis();
  if (!force && now - lastPoll < 10UL) return;
  lastPoll = now;
  buttonsOnline = releaseBoard(BUTTON_BOARD);
  if (buttonsOnline) {
    buttonsOnline = Wire.requestFrom(BUTTON_BOARD, static_cast<uint8_t>(1)) == 1;
    if (buttonsOnline) buttonInputs = Wire.read();
  }
  statusOnline = releaseBoard(STATUS_BOARD);
  // Drying-chamber door is not part of this isolated mixing-stage test.
  const bool healthy = buttonsOnline && statusOnline &&
      !(buttonInputs & (STOP_MASK | EMERGENCY_MASK));
  if (!healthy) {
    contactsHealthy = false;
    if (armed || state != IDLE) {
      stopTest();
      Serial.println(F("FAULT: Stop/E-stop/expander; motors/pump OFF. Inspect, then R."));
    }
  } else if (!contactsHealthy) {
    contactsHealthy = true;
    healthySince = now;
  }
}

bool ready() { return contactsHealthy && millis() - healthySince >= 50UL; }
bool pumpRested() { return !pumpHasRun || millis() - pumpStoppedAt >= PUMP_REST_MS; }

void commandGate(bool open) {
  mixerGate.write(open ? GATE_OPEN : GATE_CLOSED); // Set target before first attach.
  if (!mixerGate.attached()) mixerGate.attach(GATE_PIN);
  Serial.println(open ? F("MIXER GATE: OPEN commanded") : F("MIXER GATE: CLOSED commanded"));
}

void pumpStart() {
  pumpHasRun = pumpOn = true;
  digitalWrite(PUMP_PIN, HIGH);
}

void enterStage(TestState next) { state = next; stageAt = millis(); }

void finishTest() {
  outputsOff(); state = IDLE;
  Serial.println(F("CHECK ENDED: motors/pump OFF; inspect actual movement/flow."));
}

void serviceTest() {
  if (state == IDLE || !armed || !ready()) return;
  const unsigned long now = millis();
  if (now - actionAt >= ACTION_LIMIT_MS) {
    stopTest(); Serial.println(F("ACTION TIMEOUT; R required.")); return;
  }
  const unsigned long elapsed = now - stageAt;
  switch (state) {
    case SETTLING:
      if (elapsed < SETTLE_MS) break;
      if (action == 'G') {
        commandGate(false); enterStage(CLOSE_BEFORE_RUN);
      } else if (action == 'O' || action == 'C') {
        commandGate(action == 'O'); enterStage(MANUAL_GATE);
      } else {
        if (action == '1') analogWrite(SHRED_FORWARD_PIN, SHRED_FORWARD_PWM);
        if (action == '2') analogWrite(SHRED_REVERSE_PIN, SHRED_REVERSE_PWM);
        // Servo uses Timer1; D9/D10 must be digital, not analogWrite PWM.
        if (action == '3') digitalWrite(MIX_FORWARD_PIN, HIGH);
        if (action == '4') digitalWrite(MIX_REVERSE_PIN, HIGH);
        if (action == 'Y') pumpStart();
        enterStage(MANUAL_RUN);
        Serial.print(F("OUTPUT ON for 5s; command=")); Serial.println(action);
      }
      break;
    case MANUAL_RUN: if (elapsed >= RUN_MS) finishTest(); break;
    case MANUAL_GATE: if (elapsed >= GATE_TRAVEL_MS) finishTest(); break;
    case CLOSE_BEFORE_RUN:
      if (elapsed < GATE_TRAVEL_MS) break;
      analogWrite(SHRED_FORWARD_PIN, SHRED_FORWARD_PWM);
      digitalWrite(COMBINED_MIX_RPWM ? MIX_FORWARD_PIN : MIX_REVERSE_PIN, HIGH);
      pumpStart();
      enterStage(COMBINED_RUN);
      Serial.println(F("SHREDDER + MIXER + PUMP commanded for 5s"));
      break;
    case COMBINED_RUN:
      if (elapsed >= RUN_MS) {
        outputsOff(); enterStage(SETTLE_BEFORE_OPEN);
        Serial.println(F("MOTORS/PUMP OFF; settling before gate opens"));
      }
      break;
    case SETTLE_BEFORE_OPEN:
      if (elapsed >= SETTLE_MS) { commandGate(true); enterStage(OPENING); }
      break;
    case OPENING: if (elapsed >= GATE_TRAVEL_MS) enterStage(OPEN_HOLD); break;
    case OPEN_HOLD:
      if (elapsed >= GATE_OPEN_HOLD_MS) { commandGate(false); enterStage(CLOSING); }
      break;
    case CLOSING: if (elapsed >= GATE_TRAVEL_MS) finishTest(); break;
    default: break;
  }
}

void printStatus() {
  Serial.print(F("0x20 online=")); Serial.print(buttonsOnline);
  Serial.print(F(" Stop closed=")); Serial.print(buttonsOnline && !(buttonInputs & STOP_MASK));
  Serial.print(F(" E-stop closed=")); Serial.println(buttonsOnline && !(buttonInputs & EMERGENCY_MASK));
  Serial.print(F("0x24 online=")); Serial.println(statusOnline);
  Serial.print(F("Armed=")); Serial.print(armed);
  Serial.print(F(" State=")); Serial.print(static_cast<int>(state));
  Serial.print(F(" Pump rested=")); Serial.println(pumpRested());
}

void printHelp() {
  Serial.println(F("MIXING-STAGE TEST | USB 9600 | no ESP32"));
  Serial.println(F("R=arm X=stop I=status H=help"));
  Serial.println(F("1/2=shred forward/reverse; 3/4=mix D9/D10; Y=pump (5s each)"));
  Serial.println(F("O=open mixer gate C=close mixer gate"));
  Serial.println(F("G=close gate, shred+mix+pump 5s, stop/settle, open/hold/close gate"));
  Serial.println(F("Stop/E-stop monitored; drying door and physical Start/Reset unused."));
  Serial.println(F("Guarded/unloaded check; supply water and secure hose. X before adjustments."));
}

void setup() {
  for (uint8_t pin = 3; pin <= 12; ++pin) {
    digitalWrite(pin, LOW); pinMode(pin, OUTPUT);
  }
  stopTest(); // No servo attachment/movement at boot.
  Serial.begin(9600);
  Wire.begin(); Wire.setWireTimeout(25000, true);
  serviceSafety(true); printHelp(); printStatus();
}

void loop() {
  serviceSafety(); serviceTest();
  for (uint8_t count = 0; count < 8 && Serial.available(); ++count) {
    char command = Serial.read();
    if (command >= 'a' && command <= 'z') command -= 'a' - 'A';
    if (command == 'X') {
      stopTest(); Serial.println(F("STOPPED: no new gate target; R required."));
    } else if (command == 'R') {
      serviceSafety(true);
      if (state == IDLE && ready()) {
        armed = true; Serial.println(F("ARMED; choose a check."));
      } else Serial.println(F("R blocked: busy/contacts; send I."));
    } else if ((command >= '1' && command <= '4') || command == 'Y' ||
               command == 'O' || command == 'C' || command == 'G') {
      serviceSafety(true);
      if (!armed || !ready() || state != IDLE ||
          ((command == 'Y' || command == 'G') && !pumpRested())) {
        Serial.println(F("BLOCKED: R/contacts/busy/pump rest; send I."));
      } else {
        outputsOff(); action = command; actionAt = millis(); enterStage(SETTLING);
        Serial.println(F("Accepted; 2.5s settling. X cancels."));
      }
    } else if (command == 'I') { serviceSafety(true); printStatus(); }
    else if (command == 'H') printHelp();
    serviceSafety(); serviceTest();
  }
}
