#include <Arduino.h>
#include <Wire.h>

// PaperBriq hose/spray check: ONLY the pump MOSFET on Uno D3 is enabled.
// No ESP32, heating, forming, servo movement or automatic repeat.
const uint8_t PUMP_PIN = 3;
const unsigned long SHORT_RUN_MS = 5000UL;
const unsigned long LONG_RUN_MS = 30000UL;
const unsigned long SETTLE_MS = 2500UL;
const unsigned long REST_MS = 10000UL; // Rest after every pump stop.
static_assert(SHORT_RUN_MS > 0 && SHORT_RUN_MS <=30000UL, "Pump check: 1..15000 ms");
static_assert(LONG_RUN_MS > 0 && LONG_RUN_MS <= 30000UL, "Pump check: 1..15000 ms");

const uint8_t BUTTON_BOARD = 0x20, STATUS_BOARD = 0x24;
const uint8_t STOP_MASK = 1 << 2, EMERGENCY_MASK = 1 << 3;
enum PumpState { IDLE, WAITING, RUNNING };
PumpState pumpState = IDLE;
bool armed = false, contactsHealthy = false, buttonsOnline = false, statusOnline = false;
bool hasRun = false;
uint8_t buttonInputs = 0xFF;
unsigned long healthySince = 0, lastPoll = 0, requestedAt = 0;
unsigned long runStarted = 0, stoppedAt = 0, runDuration = SHORT_RUN_MS;

void pumpOff() {
  digitalWrite(PUMP_PIN, LOW);
  if (pumpState == RUNNING) stoppedAt = millis();
  pumpState = IDLE;
}

void stopTest() {
  pumpOff();
  armed = false;
}

bool releaseBoard(uint8_t address) {
  Wire.beginTransmission(address);
  Wire.write(0xFF); // Release inputs, keep active-LOW relays/indicators off.
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
  // Drying-chamber door is unrelated to this isolated pump/hose check.
  // Both boards remain required to keep unrelated outputs commanded off.
  const bool healthy = buttonsOnline && statusOnline &&
      !(buttonInputs & (STOP_MASK | EMERGENCY_MASK));
  if (!healthy) {
    contactsHealthy = false;
    if (armed || pumpState != IDLE) {
      stopTest();
      Serial.println(F("FAULT: Stop/E-stop/expander. Pump OFF; inspect, then R."));
    }
  } else if (!contactsHealthy) {
    contactsHealthy = true;
    healthySince = now;
  }
}

bool ready() { return contactsHealthy && millis() - healthySince >= 50UL; }
bool rested() { return !hasRun || millis() - stoppedAt >= REST_MS; }

void printStatus() {
  Serial.print(F("0x20 online=")); Serial.print(buttonsOnline);
  Serial.print(F(" Stop closed=")); Serial.print(buttonsOnline && !(buttonInputs & STOP_MASK));
  Serial.print(F(" E-stop closed=")); Serial.println(buttonsOnline && !(buttonInputs & EMERGENCY_MASK));
  Serial.print(F("0x24 online=")); Serial.println(statusOnline);
  Serial.print(F("Armed=")); Serial.print(armed);
  Serial.print(F(" State (0 idle/1 waiting/2 on)=")); Serial.println(static_cast<int>(pumpState));
  Serial.print(F("Rest complete=")); Serial.println(rested());
}

void printHelp() {
  Serial.println(F("PUMP ONLY | Uno D3 -> MOSFET TRIG | USB 9600"));
  Serial.println(F("R=arm Y or 1=5s 2=15s X=stop I=status H=help"));
  Serial.println(F("2.5s start delay; 10s rest after stopping; no automatic repeat."));
  Serial.println(F("Stop/E-stop monitored. Drying door and physical Start/Reset unused."));
  Serial.println(F("Supply water, secure hose, keep spray away from electronics. X before adjustments."));
}

void setup() {
  for (uint8_t pin = 3; pin <= 12; ++pin) {
    digitalWrite(pin, LOW);
    pinMode(pin, OUTPUT);
  }
  stopTest(); // Never attach either servo or issue gate commands.
  Serial.begin(9600);
  Wire.begin();
  Wire.setWireTimeout(25000, true);
  serviceSafety(true);
  printHelp();
  printStatus();
}

void servicePump() {
  if (!armed || !ready()) return;
  const unsigned long now = millis();
  if (pumpState == WAITING && now - requestedAt >= SETTLE_MS) {
    pumpState = RUNNING;
    runStarted = now;
    hasRun = true;
    digitalWrite(PUMP_PIN, HIGH);
    Serial.print(F("PUMP ON commanded; auto-stop ms=")); Serial.println(runDuration);
  } else if (pumpState == RUNNING && now - runStarted >= runDuration) {
    pumpOff();
    Serial.println(F("PUMP OFF; wait 10s before another check."));
  }
}

void loop() {
  serviceSafety();
  servicePump();
  for (uint8_t count = 0; count < 8 && Serial.available(); ++count) {
    const char command = Serial.read();
    if (command == 'X' || command == 'x') {
      stopTest(); Serial.println(F("STOPPED; R required."));
    } else if (command == 'R' || command == 'r') {
      serviceSafety(true);
      if (pumpState == IDLE && ready()) {
        armed = true; Serial.println(F("ARMED; Y/1=5s, 2=15s."));
      } else Serial.println(F("R blocked: busy/contacts; send I."));
    } else if (command == 'Y' || command == 'y' || command == '1' || command == '2') {
      serviceSafety(true);
      if (!armed || !ready() || pumpState != IDLE || !rested()) {
        Serial.println(F("BLOCKED: R/contacts/busy/10s rest; send I."));
      } else {
        runDuration = command == '2' ? LONG_RUN_MS : SHORT_RUN_MS;
        requestedAt = millis();
        pumpState = WAITING;
        Serial.println(F("Accepted; starts in 2.5s. X cancels."));
      }
    } else if (command == 'I' || command == 'i') {
      serviceSafety(true); printStatus();
    } else if (command == 'H' || command == 'h') printHelp();
    serviceSafety();
    servicePump();
  }
}
