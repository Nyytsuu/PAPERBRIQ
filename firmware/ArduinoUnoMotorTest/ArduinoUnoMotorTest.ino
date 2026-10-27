#include <Arduino.h>
#include <Servo.h>
#include <SoftwareSerial.h>
#include <Wire.h>
#include "ChamberControl.h"
#include <stdlib.h>
#include <math.h>

void serviceChamber(bool force = false);
bool chamberMotionReady();
bool runChamberCommand(const char *action);
void printChamberStatus();
bool startComponentTest();
void serviceComponentTest();

// PaperBriq Arduino Uno shredder, mixer, and mixer-gate bench test.
// Test only unloaded, securely clamped motors with a fused 12 V supply and
// a reachable physical emergency disconnect.

// BTS7960 signal pins used by this test.
const uint8_t SHREDDER_RPWM = 5;  // Hardware PWM
const uint8_t SHREDDER_LPWM = 6;  // Hardware PWM
const uint8_t PRESSER_RPWM = 7;
const uint8_t PRESSER_LPWM = 8;

// Kept on the operator-selected pins. Servo uses Timer1, so D9/D10 are used
// as reliable full-speed digital direction outputs, not analogWrite PWM.
const uint8_t MIXER_RPWM = 9;
const uint8_t MIXER_LPWM = 10;

// Pusher L298N channel B, kept on the operator-selected direction pins.
// ENB is permanently connected to a regulated 5 V rail, so D13 is unused.
const uint8_t PUSHER_IN3 = 11;
const uint8_t PUSHER_IN4 = 12;

// Private 9600-baud command/event link to ESP32 UART2. A2/A3 work as Uno
// digital pins 16/17. A2 receives the ESP32's 3.3 V TX directly. A3
// transmits 5 V and MUST pass through a level shifter or resistor divider.
const uint8_t ESP_LINK_RX = A2;
const uint8_t ESP_LINK_TX = A3;
const unsigned long ESP_LINK_BAUD = 9600;
SoftwareSerial espLink(ESP_LINK_RX, ESP_LINK_TX);

// PCF8574T operator controls on the Uno I2C bus (A4 SDA, A5 SCL).
// Start and Reset are normally-open contacts to GND. Stop and Emergency are
// normally-closed contacts to GND, so an open/broken wire requests a stop.
const uint8_t CONTROL_IO_ADDRESS = 0x20;
// Preserve the installed button allocation; chamber wiring uses status expander 0x24.
const uint8_t START_BUTTON_MASK = _BV(0);
const uint8_t RESET_BUTTON_MASK = _BV(1);
const uint8_t STOP_BUTTON_MASK = _BV(2);
const uint8_t EMERGENCY_BUTTON_MASK = _BV(3);
const unsigned long BUTTON_POLL_MS = 10;
const unsigned long BUTTON_DEBOUNCE_MS = 50;

// A second PCF8574T drives active-low inputs on the 5 V relay board. The
// relay contacts switch the separate 12 V status lamps; LOW energizes a relay.
const uint8_t STATUS_IO_ADDRESS = 0x24;
const uint8_t READY_LED_MASK = _BV(0);
const uint8_t RUNNING_LED_MASK = _BV(1);
const uint8_t STOPPED_LED_MASK = _BV(3);
const uint8_t BUZZER_MASK = _BV(4);
const unsigned long STATUS_IO_RETRY_MS = 1000;
const unsigned long STATUS_LED_BLINK_MS = 400;
const unsigned long COMPLETED_LED_BLINK_MS = 5000;
const unsigned long STATUS_SELF_TEST_STEP_MS = 700;
const uint8_t STATUS_SELF_TEST_STEP_COUNT = 5;

// Operator-confirmed LED expander 0x24: P2 door NC to GND, P5 fan, P6 unlock.
// P2 remains HIGH/released. P7 stays HIGH/off; no heater output is enabled.
ChamberControl chamber;
unsigned long lastDoorPollAtMs = 0;
uint8_t lastStatusInputs = 0xFF;
unsigned long lastGateCommandAtMs = 0;

// Mixer gate servo signal. A0 works as digital pin 14 on an Arduino Uno.
const uint8_t MIXER_GATE_SERVO_PIN = A0;
// Molder servo signal. A1 works as digital pin 15 on an Arduino Uno.
const uint8_t MOLDER_SERVO_PIN = A1;

// Adjust gradually to match the physical gate linkage. Swap the values if
// the movement is reversed. Stop if the servo buzzes or hits a hard stop.
const uint8_t MIXER_GATE_CLOSED_ANGLE = 34;
const uint8_t MIXER_GATE_OPEN_ANGLE = 130;

// Initial conservative molder angles. Adjust gradually for its linkage and
// swap the values if its physical open/close direction is reversed.
const uint8_t MOLDER_CLOSED_ANGLE = 120;
const uint8_t MOLDER_OPEN_ANGLE = 0;
// Independent motor speeds. PWM range: 0-255.
// 77 = 30%, 102 = 40%, 128 = 50%, 191 = 75%, 255 = 100%.
const uint8_t SHREDDER_FORWARD_DUTY = 102;  // 40%
const uint8_t SHREDDER_REVERSE_DUTY = 77;   // 30%
const uint8_t MIXER_FORWARD_DUTY = 102;     // 40%
const uint8_t MIXER_REVERSE_DUTY = 77;      // 30%

// Bench-test time limits. Every motor pulse stops automatically.
const unsigned long SHREDDER_PULSE_MS = 1200;
const unsigned long MIXER_PULSE_MS = 12000;
const unsigned long PRESSER_PULSE_MS = 1000;
const unsigned long PUSHER_PULSE_MS = 5000;

// Automatic demonstration process timings. Start with P after arming with R.
// Increase these only after the complete unloaded sequence works safely.
const unsigned long PROCESS_SHREDDER_RUN_MS = 5000;
// The mixer starts with the shredder, then continues alone for this long.
const unsigned long PROCESS_MIXER_FINISH_MS = 5000;
const unsigned long PROCESS_MATERIAL_RELEASE_MS = 3000;
const unsigned long PROCESS_PRESSER_EXTEND_MS = 5000;
const unsigned long PROCESS_PRESSER_RETRACT_MS = 5000;
const unsigned long PROCESS_PUSHER_EXTEND_MS = 5000;
const unsigned long PROCESS_PUSHER_RETRACT_MS = 5000;
const unsigned long MIXER_GATE_TRAVEL_MS = 1500;
const unsigned long MOLDER_TRAVEL_MS = 1500;
const unsigned long PROCESS_CLOSE_TRAVEL_MS =
    MIXER_GATE_TRAVEL_MS > MOLDER_TRAVEL_MS
        ? MIXER_GATE_TRAVEL_MS
        : MOLDER_TRAVEL_MS;

// Mechanical settling time before any motor starts in a new direction.
const unsigned long DIRECTION_CHANGE_PAUSE_MS = 2500;
const unsigned long SOFTWARE_STOP_REARM_DELAY_MS = 1000;

enum Motion : uint8_t {
  MOTION_STOPPED,
  SHREDDER_FORWARD,
  SHREDDER_REVERSE,
  MIXER_FORWARD,
  MIXER_REVERSE,
  PRESSER_EXTEND,
  PRESSER_RETRACT,
  PUSHER_FORWARD,
  PUSHER_REVERSE
};

enum RunState : uint8_t {
  STATE_IDLE,
  STATE_WAITING,
  STATE_RUNNING
};

enum AutomaticProcessState : uint8_t {
  PROCESS_IDLE,
  PROCESS_WAITING_TO_CLOSE_GATE,
  PROCESS_WAITING_FOR_GATE_TO_CLOSE,
  PROCESS_SHREDDING_AND_MIXING,
  PROCESS_MIXER_FINISHING,
  PROCESS_WAITING_TO_OPEN_GATE,
  PROCESS_WAITING_FOR_GATE_TO_OPEN,
  PROCESS_RELEASING_MATERIAL,
  PROCESS_WAITING_FOR_MIXER_GATE_TO_CLOSE,
  PROCESS_PRESSER_EXTENDING,
  PROCESS_PRESSER_RETRACTING,
  PROCESS_WAITING_TO_OPEN_MOLDER,
  PROCESS_WAITING_FOR_MOLDER_TO_OPEN,
  PROCESS_PUSHER_EXTENDING,
  PROCESS_PUSHER_RETRACTING,
  PROCESS_WAITING_TO_CLOSE_MOLDER,
  PROCESS_WAITING_FOR_MOLDER_TO_CLOSE
};

Servo mixerGateServo;
Servo molderServo;
Motion requestedMotion = MOTION_STOPPED;
Motion activeMotion = MOTION_STOPPED;
RunState runState = STATE_IDLE;
AutomaticProcessState automaticProcessState = PROCESS_IDLE;
unsigned long stateStartedAtMs = 0;
unsigned long lastMotorStoppedAtMs = 0;
unsigned long softwareStopLatchedAtMs = 0;
unsigned long processStateStartedAtMs = 0;
unsigned long requestedRunDurationMs = 0;
unsigned long activeRunDurationMs = 0;
bool softwareStopLatched = true;

struct DebouncedButton {
  bool initialized;
  bool rawActive;
  bool stableActive;
  unsigned long rawChangedAtMs;
};

DebouncedButton startButton = {false, false, false, 0};
DebouncedButton resetButton = {false, false, false, 0};
DebouncedButton stopButton = {false, true, true, 0};
DebouncedButton emergencyButton = {false, true, true, 0};
bool controlIoOnline = false;
unsigned long lastButtonPollAtMs = 0;
bool statusIoOnline = false;
uint8_t statusIoShadow = 0xFF;
unsigned long lastStatusIoRetryAtMs = 0;
unsigned long processCompletedAtMs = 0;
bool statusSelfTestActive = false;
bool diagnosticActive = false;
bool diagnosticStepRunning = false;
uint8_t diagnosticStep = 0;
unsigned long diagnosticStartedAtMs = 0;
unsigned long diagnosticStepAtMs = 0;
uint8_t statusSelfTestStep = 0;
unsigned long statusSelfTestStepStartedAtMs = 0;

const uint8_t BRIDGE_LINE_CAPACITY = 64;
char bridgeLine[BRIDGE_LINE_CAPACITY];
bool dhtCheckPending = false;
unsigned long dhtCheckSentAt = 0;
unsigned long dhtCheckSequence = 0;
uint8_t bridgeLineLength = 0;
unsigned long bridgeLastByteAtMs = 0;
const unsigned long BRIDGE_FRAME_TIMEOUT_MS = 750;
char lastRemoteCommandId[40] = "";
char lastRemoteStatus[10] = "";
char lastRemoteMessage[20] = "";

bool writeControlIo(uint8_t value) {
  Wire.beginTransmission(CONTROL_IO_ADDRESS);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool writeStatusIo(uint8_t value) {
  value = chamber.outputs(value);
  Wire.beginTransmission(STATUS_IO_ADDRESS);
  Wire.write(value);
  if (Wire.endTransmission() != 0) return false;
  statusIoShadow = value;
  return true;
}

bool readControlIo(uint8_t &value) {
  if (Wire.requestFrom(CONTROL_IO_ADDRESS, static_cast<uint8_t>(1)) != 1) {
    return false;
  }
  value = Wire.read();
  return true;
}

void seedButton(DebouncedButton &button, bool active,
                unsigned long nowMs) {
  button.initialized = true;
  button.rawActive = active;
  button.stableActive = active;
  button.rawChangedAtMs = nowMs;
}

bool updateButton(DebouncedButton &button, bool active,
                  unsigned long nowMs) {
  if (!button.initialized) {
    seedButton(button, active, nowMs);
    return false;
  }

  if (active != button.rawActive) {
    button.rawActive = active;
    button.rawChangedAtMs = nowMs;
  }

  if (button.stableActive != button.rawActive &&
      nowMs - button.rawChangedAtMs >= BUTTON_DEBOUNCE_MS) {
    button.stableActive = button.rawActive;
    return true;
  }
  return false;
}

bool initializeControlIo() {
  // Writing HIGH releases every quasi-bidirectional PCF8574 pin for input.
  uint8_t value = 0xFF;
  if (!writeControlIo(value) || !readControlIo(value)) {
    controlIoOnline = false;
    Serial.println(F("CONTROL BUTTONS OFFLINE: PCF8574 not found at 0x20"));
    return false;
  }

  const unsigned long nowMs = millis();
  seedButton(startButton, (value & START_BUTTON_MASK) == 0, nowMs);
  seedButton(resetButton, (value & RESET_BUTTON_MASK) == 0, nowMs);
  seedButton(stopButton, (value & STOP_BUTTON_MASK) != 0, nowMs);
  seedButton(emergencyButton,
             (value & EMERGENCY_BUTTON_MASK) != 0, nowMs);
  controlIoOnline = true;
  Serial.print(F("CONTROL BUTTONS ONLINE at 0x20; stop contact is "));
  Serial.print(stopButton.stableActive ? F("OPEN") : F("CLOSED"));
  Serial.print(F("; emergency contact is "));
  Serial.println(emergencyButton.stableActive ? F("OPEN") : F("CLOSED"));
  return true;
}

bool initializeStatusIo() {
  chamber.cancel();
  chamber.sample(false, false, millis());
  // HIGH switches every active-low indicator off and leaves unused pins safe.
  if (!writeStatusIo(0xFF)) {
    statusIoOnline = false;
    Serial.println(F("STATUS LIGHTS OFFLINE: PCF8574 not found at 0x24"));
    return false;
  }
  statusIoOnline = true;
  Serial.println(F("STATUS LIGHTS ONLINE at 0x24"));
  return true;
}

void printEventLine(Print &output, const __FlashStringHelper *type,
                    const __FlashStringHelper *severity,
                    const __FlashStringHelper *component,
                    const __FlashStringHelper *processState,
                    const __FlashStringHelper *message) {
  output.print(F("EVENT|"));
  output.print(type);
  output.print('|');
  output.print(severity);
  output.print('|');
  output.print(component);
  output.print('|');
  output.print(processState);
  output.print('|');
  output.println(message);
}

void emitEvent(const __FlashStringHelper *type,
               const __FlashStringHelper *severity,
               const __FlashStringHelper *component,
               const __FlashStringHelper *processState,
               const __FlashStringHelper *message) {
  printEventLine(espLink, type, severity, component, processState, message);
  printEventLine(Serial, type, severity, component, processState, message);
}

void printAckLine(Print &output, const char *id, const char *status,
                  const char *message) {
  output.print(F("ACK|"));
  output.print(id);
  output.print('|');
  output.print(status);
  output.print('|');
  output.println(message);
}

void emitAck(const char *id, const char *status, const char *message) {
  printAckLine(espLink, id, status, message);
  printAckLine(Serial, id, status, message);
}

void rememberRemoteResult(const char *id, const char *status,
                          const char *message) {
  strncpy(lastRemoteCommandId, id, sizeof(lastRemoteCommandId) - 1);
  lastRemoteCommandId[sizeof(lastRemoteCommandId) - 1] = '\0';
  strncpy(lastRemoteStatus, status, sizeof(lastRemoteStatus) - 1);
  lastRemoteStatus[sizeof(lastRemoteStatus) - 1] = '\0';
  strncpy(lastRemoteMessage, message, sizeof(lastRemoteMessage) - 1);
  lastRemoteMessage[sizeof(lastRemoteMessage) - 1] = '\0';
}

uint8_t dutyPercent(uint8_t duty) {
  return static_cast<uint8_t>((static_cast<unsigned int>(duty) * 100U) / 255U);
}

const __FlashStringHelper *motionName(Motion motion) {
  switch (motion) {
    case SHREDDER_FORWARD:
      return F("SHREDDER FORWARD");
    case SHREDDER_REVERSE:
      return F("SHREDDER REVERSE");
    case MIXER_FORWARD:
      return F("MIXER FORWARD");
    case MIXER_REVERSE:
      return F("MIXER REVERSE");
    case PRESSER_EXTEND:
      return F("PRESSER EXTEND");
    case PRESSER_RETRACT:
      return F("PRESSER RETRACT");
    case PUSHER_FORWARD:
      return F("PUSHER FORWARD");
    case PUSHER_REVERSE:
      return F("PUSHER REVERSE");
    default:
      return F("STOPPED");
  }
}

uint8_t motionDuty(Motion motion) {
  switch (motion) {
    case SHREDDER_FORWARD:
      return SHREDDER_FORWARD_DUTY;
    case SHREDDER_REVERSE:
      return SHREDDER_REVERSE_DUTY;
    case MIXER_FORWARD:
    case MIXER_REVERSE:
      return 255;
    case PRESSER_EXTEND:
    case PRESSER_RETRACT:
    case PUSHER_FORWARD:
    case PUSHER_REVERSE:
      return 255;
    default:
      return 0;
  }
}

unsigned long motionDurationMs(Motion motion) {
  switch (motion) {
    case SHREDDER_FORWARD:
    case SHREDDER_REVERSE:
      return SHREDDER_PULSE_MS;
    case MIXER_FORWARD:
    case MIXER_REVERSE:
      return MIXER_PULSE_MS;
    case PRESSER_EXTEND:
    case PRESSER_RETRACT:
      return PRESSER_PULSE_MS;
    case PUSHER_FORWARD:
    case PUSHER_REVERSE:
      return PUSHER_PULSE_MS;
    default:
      return 0;
  }
}

void stopMotorOutputs() {
  // LOW cancels analogWrite PWM on these Uno pins.
  digitalWrite(SHREDDER_RPWM, LOW);
  digitalWrite(SHREDDER_LPWM, LOW);
  digitalWrite(MIXER_RPWM, LOW);
  digitalWrite(MIXER_LPWM, LOW);
  digitalWrite(PRESSER_RPWM, LOW);
  digitalWrite(PRESSER_LPWM, LOW);
  digitalWrite(PUSHER_IN3, LOW);
  digitalWrite(PUSHER_IN4, LOW);
}

void stopAllMotors(bool printMessage = true) {
  stopMotorOutputs();
  requestedMotion = MOTION_STOPPED;
  activeMotion = MOTION_STOPPED;
  runState = STATE_IDLE;
  requestedRunDurationMs = 0;
  activeRunDurationMs = 0;
  lastMotorStoppedAtMs = millis();

  if (printMessage) {
    Serial.println(F("ALL MOTORS: stopped"));
  }
}

bool automaticProcessIsActive() {
  return automaticProcessState != PROCESS_IDLE;
}

void releaseMixerGateServo(bool printMessage = true) {
  if (mixerGateServo.attached()) {
    mixerGateServo.detach();
  }
  pinMode(MIXER_GATE_SERVO_PIN, OUTPUT);
  digitalWrite(MIXER_GATE_SERVO_PIN, LOW);

  if (printMessage) {
    Serial.println(F("MIXER GATE: servo released; no holding torque"));
  }
}

void releaseMolderServo(bool printMessage = true) {
  if (molderServo.attached()) {
    molderServo.detach();
  }
  pinMode(MOLDER_SERVO_PIN, OUTPUT);
  digitalWrite(MOLDER_SERVO_PIN, LOW);

  if (printMessage) {
    Serial.println(F("MOLDER: servo released; no holding torque"));
  }
}

void setMixerGateAngle(uint8_t angle,
                       const __FlashStringHelper *positionName) {
  if (!chamber.online || !chamber.doorClosed || chamber.unlockOn) return;
  lastGateCommandAtMs = millis();
  const uint8_t safeAngle = constrain(angle, 0, 180);
  if (!mixerGateServo.attached()) {
    // Store the requested position before enabling the signal to reduce the
    // chance of a startup twitch toward Servo's default center position.
    mixerGateServo.write(safeAngle);
    mixerGateServo.attach(MIXER_GATE_SERVO_PIN);
  } else {
    mixerGateServo.write(safeAngle);
  }

  Serial.print(F("MIXER GATE: moving to "));
  Serial.print(positionName);
  Serial.print(F(" at "));
  Serial.print(safeAngle);
  Serial.println(F(" degrees; servo holding position"));
}

void setMolderAngle(uint8_t angle,
                    const __FlashStringHelper *positionName) {
  if (!chamber.online || !chamber.doorClosed || chamber.unlockOn) return;
  lastGateCommandAtMs = millis();
  const uint8_t safeAngle = constrain(angle, 0, 180);
  if (!molderServo.attached()) {
    molderServo.write(safeAngle);
    molderServo.attach(MOLDER_SERVO_PIN);
  } else {
    molderServo.write(safeAngle);
  }

  Serial.print(F("MOLDER: moving to "));
  Serial.print(positionName);
  Serial.print(F(" at "));
  Serial.print(safeAngle);
  Serial.println(F(" degrees; servo holding position"));
}

void closeAndHoldBothGates() {
  setMixerGateAngle(MIXER_GATE_CLOSED_ANGLE, F("CLOSED"));
  setMolderAngle(MOLDER_CLOSED_ANGLE, F("CLOSED"));
}

void moveMixerGate(uint8_t angle, const __FlashStringHelper *positionName) {
  if (diagnosticActive) return;
  if (!chamberMotionReady()) return;
  if (softwareStopLatched) {
    Serial.println(F("SERVO COMMAND REJECTED: send R to arm first"));
    return;
  }

  if (automaticProcessIsActive()) {
    Serial.println(F("SERVO COMMAND REJECTED: automatic process is running"));
    return;
  }

  // Stop an active/pending motor request, then make the operator resend the
  // gate command after the mechanical coast-down interval.
  if (runState != STATE_IDLE) {
    stopAllMotors(false);
    Serial.println(F("MIXER GATE: motor stopped; wait 2.5 s, then resend O/C"));
    return;
  }

  const unsigned long stoppedForMs = millis() - lastMotorStoppedAtMs;
  if (stoppedForMs < DIRECTION_CHANGE_PAUSE_MS) {
    Serial.print(F("MIXER GATE: wait another "));
    Serial.print(DIRECTION_CHANGE_PAUSE_MS - stoppedForMs);
    Serial.println(F(" ms for motor coast-down"));
    return;
  }

  setMixerGateAngle(angle, positionName);
}

void moveMolder(uint8_t angle, const __FlashStringHelper *positionName) {
  if (diagnosticActive) return;
  if (!chamberMotionReady()) return;
  if (softwareStopLatched) {
    Serial.println(F("MOLDER COMMAND REJECTED: send R to arm first"));
    return;
  }

  if (automaticProcessIsActive()) {
    Serial.println(F("MOLDER COMMAND REJECTED: automatic process is running"));
    return;
  }

  if (runState != STATE_IDLE) {
    stopAllMotors(false);
    Serial.println(F("MOLDER: motor stopped; wait 2.5 s, then resend 5/6"));
    return;
  }

  const unsigned long stoppedForMs = millis() - lastMotorStoppedAtMs;
  if (stoppedForMs < DIRECTION_CHANGE_PAUSE_MS) {
    Serial.print(F("MOLDER: wait another "));
    Serial.print(DIRECTION_CHANGE_PAUSE_MS - stoppedForMs);
    Serial.println(F(" ms for motor coast-down"));
    return;
  }

  setMolderAngle(angle, positionName);
}

void latchSoftwareStop() {
  if (diagnosticActive) {
    diagnosticActive = false;
    diagnosticStepRunning = false;
    emitEvent(F("TEST_ABORTED"), F("warning"), F("diagnostics"), F("stopped"),
              F("Test interrupted; unfinished items remain unchecked"));
  }
  chamber.cancel();
  statusSelfTestActive = false;
  stopAllMotors(false);
  automaticProcessState = PROCESS_IDLE;
  softwareStopLatched = true;
  softwareStopLatchedAtMs = millis();
  // An open door never commands a new gate position or detaches a holding servo.
  if (chamber.online && chamber.doorClosed) closeAndHoldBothGates();
  if (!writeStatusIo(statusIoShadow)) statusIoOnline = false;
  Serial.println(F("SOFTWARE STOP LATCHED: motors stopped; Reset required"));
  Serial.println(F("Inspect the machine, then send R to arm again."));
  emitEvent(F("STOPPED"), F("warning"), F("controller"), F("stopped"),
            F("Software stop latched"));
}

const char *lastArmRejectReason = "reset_rejected";

bool armMotorTest() {
  lastArmRejectReason = "reset_rejected";
  if (diagnosticActive) {
    lastArmRejectReason = "test_running";
    Serial.println(F("ARM REJECTED: component test running; stop it first"));
    return false;
  }
  if (!chamberMotionReady()) {
    lastArmRejectReason = !chamber.online ? "chamber_offline" :
                          !chamber.doorClosed ? "door_open" :
                          chamber.unlockOn ? "unlock_active" : "fan_active";
    return false;
  }
  if (!controlIoOnline) {
    lastArmRejectReason = "buttons_offline";
    Serial.println(F("ARM REJECTED: control-button expander is offline"));
    return false;
  }

  if (emergencyButton.stableActive) {
    lastArmRejectReason = "emergency_open";
    Serial.println(F("ARM REJECTED: emergency contact is open"));
    return false;
  }

  if (stopButton.stableActive) {
    lastArmRejectReason = "stop_open";
    Serial.println(F("ARM REJECTED: physical Stop contact is open"));
    return false;
  }

  if (automaticProcessIsActive()) {
    lastArmRejectReason = "process_running";
    Serial.println(F("ARM REJECTED: automatic process is already running"));
    return false;
  }

  if (millis() - softwareStopLatchedAtMs < SOFTWARE_STOP_REARM_DELAY_MS) {
    lastArmRejectReason = "reset_too_soon";
    Serial.println(F("ARM REJECTED: wait 1 second after X, then send R again"));
    return false;
  }

  stopAllMotors(false);
  softwareStopLatched = false;
  Serial.println(F("MOTOR TEST ARMED: motor and gate commands enabled"));
  emitEvent(F("ARMED"), F("info"), F("controller"), F("stopped"),
            F("Controller armed"));
  return true;
}

void printStatusSelfTestStep(uint8_t step) {
  Serial.print(F("STATUS TEST: "));
  switch (step) {
    case 0:
      Serial.println(F("green Ready/Check light"));
      break;
    case 1:
      Serial.println(F("yellow Preparing/Running light"));
      break;
    case 2:
      Serial.println(F("red Stop/Fault light"));
      break;
    case 3:
      Serial.println(F("buzzer"));
      break;
    default:
      Serial.println(F("all three lights and buzzer"));
      break;
  }
}

void startStatusSelfTest() {
  if (diagnosticActive) return;
  if (!chamberMotionReady()) return;
  if (!statusIoOnline) {
    Serial.println(F("STATUS TEST REJECTED: status-light expander is offline"));
    return;
  }

  if (!controlIoOnline || stopButton.stableActive ||
      emergencyButton.stableActive) {
    Serial.println(F("STATUS TEST REJECTED: clear Stop/Emergency faults first"));
    return;
  }

  if (automaticProcessIsActive() || runState != STATE_IDLE) {
    Serial.println(F("STATUS TEST REJECTED: wait until all movement has stopped"));
    return;
  }

  if (!writeStatusIo(0xFF)) {
    statusIoOnline = false;
    Serial.println(F("STATUS TEST REJECTED: status-light expander did not respond"));
    return;
  }

  statusSelfTestActive = true;
  statusSelfTestStep = 0;
  statusSelfTestStepStartedAtMs = millis();
  Serial.println(F("STATUS TEST STARTED: no motors will move"));
  printStatusSelfTestStep(statusSelfTestStep);
}

void printHelp() {
  Serial.println();
  Serial.println(F("PaperBriq Arduino Uno actuator test ready"));
  Serial.println(F("S = read chamber DHT22 through ESP32 GPIO27 (no movement)"));
  Serial.println(F("R = arm/reset after a software stop"));
  Serial.println(F("1 = shredder forward pulse"));
  Serial.println(F("2 = shredder reverse pulse"));
  Serial.println(F("3 = mixer forward pulse"));
  Serial.println(F("4 = mixer reverse pulse"));
  Serial.println(F("E = presser extend pulse"));
  Serial.println(F("T = presser retract pulse"));
  Serial.println(F("F = pusher forward pulse"));
  Serial.println(F("B = pusher reverse pulse"));
  Serial.println(F("P = run the complete shred, mix, press, and eject cycle"));
  Serial.println(F("O = open mixer gate servo"));
  Serial.println(F("C = close mixer gate servo"));
  Serial.println(F("D = release mixer gate servo"));
  Serial.println(F("5 = open molder servo"));
  Serial.println(F("6 = close molder servo"));
  Serial.println(F("7 = release molder servo"));
  Serial.println(F("L = test green, yellow, red, buzzer, and all indicators"));
  Serial.println(F("X = latch software stop and cancel movement"));
  Serial.println(F("H = show commands"));
  Serial.println(F("Q = sequential component tests (unloaded, supervised; R first)"));
  Serial.println(F("I = door/fan/lock status (0x24: P2/P5/P6)"));
  Serial.println(F("V = 10-second fan test; N = fan off"));
  Serial.println(F("U = 1-second unlock; K = cancel unlock power"));
  Serial.println(F("For V/U: use X, wait for movement to stop; heater disabled"));
  Serial.println(F("No gate movement at boot; close door then R to arm"));
  Serial.println(F("Send R first to enable motor and manual gate commands"));
}

void requestPulse(Motion motion, unsigned long runDurationMs) {
  if (!chamberMotionReady()) return;
  if (softwareStopLatched) {
    Serial.println(F("COMMAND REJECTED: send R to arm first"));
    return;
  }

  // Stop both motors immediately. The requested direction starts only after
  // the mechanical settling interval has passed.
  stopMotorOutputs();
  activeMotion = MOTION_STOPPED;
  requestedMotion = motion;
  requestedRunDurationMs = runDurationMs;
  runState = STATE_WAITING;
  stateStartedAtMs = millis();

  Serial.print(motionName(motion));
  Serial.print(F(": requested at "));
  Serial.print(dutyPercent(motionDuty(motion)));
  Serial.println(F("% PWM; waiting for safety pause"));
}

void requestManualPulse(Motion motion) {
  if (diagnosticActive) return;
  if (statusSelfTestActive) {
    Serial.println(F("MANUAL COMMAND REJECTED: finish the status test first"));
    return;
  }

  if (automaticProcessIsActive()) {
    Serial.println(F("MANUAL COMMAND REJECTED: automatic process is running"));
    return;
  }

  requestPulse(motion, motionDurationMs(motion));
}

void startRequestedMotion() {
  if (!chamberMotionReady()) return;
  activeMotion = requestedMotion;
  requestedMotion = MOTION_STOPPED;
  activeRunDurationMs = requestedRunDurationMs;
  requestedRunDurationMs = 0;
  runState = STATE_RUNNING;
  stateStartedAtMs = millis();

  // Only one input on one BTS7960 is driven at a time.
  switch (activeMotion) {
    case SHREDDER_FORWARD:
      digitalWrite(SHREDDER_LPWM, LOW);
      analogWrite(SHREDDER_RPWM, SHREDDER_FORWARD_DUTY);
      break;

    case SHREDDER_REVERSE:
      digitalWrite(SHREDDER_RPWM, LOW);
      analogWrite(SHREDDER_LPWM, SHREDDER_REVERSE_DUTY);
      break;

    case MIXER_FORWARD:
      digitalWrite(MIXER_LPWM, LOW);
      digitalWrite(MIXER_RPWM, HIGH);
      break;

    case MIXER_REVERSE:
      digitalWrite(MIXER_RPWM, LOW);
      digitalWrite(MIXER_LPWM, HIGH);
      break;

    case PRESSER_EXTEND:
      digitalWrite(PRESSER_LPWM, LOW);
      digitalWrite(PRESSER_RPWM, HIGH);
      break;

    case PRESSER_RETRACT:
      digitalWrite(PRESSER_RPWM, LOW);
      digitalWrite(PRESSER_LPWM, HIGH);
      break;

    case PUSHER_FORWARD:
      // This actuator's installed polarity extends when IN4 is HIGH.
      digitalWrite(PUSHER_IN3, LOW);
      digitalWrite(PUSHER_IN4, HIGH);
      break;

    case PUSHER_REVERSE:
      // Retract using the opposite polarity.
      digitalWrite(PUSHER_IN4, LOW);
      digitalWrite(PUSHER_IN3, HIGH);
      break;

    default:
      stopAllMotors(false);
      return;
  }

  Serial.print(motionName(activeMotion));
  Serial.print(F(": running for "));
  Serial.print(activeRunDurationMs);
  Serial.println(F(" ms"));
  if (activeMotion == MIXER_FORWARD || activeMotion == MIXER_REVERSE) {
    emitEvent(F("MIXER_STARTED"), F("info"), F("mixer"), F("running"),
              F("Mixer started"));
  } else if (activeMotion == PRESSER_EXTEND || activeMotion == PRESSER_RETRACT) {
    emitEvent(F("PRESSER_STARTED"), F("info"), F("presser"), F("running"),
              F("Presser pulse started"));
  } else if (activeMotion == PUSHER_FORWARD || activeMotion == PUSHER_REVERSE) {
    emitEvent(F("PUSHER_STARTED"), F("info"), F("pusher"), F("running"),
              F("Pusher pulse started"));
  }
}

void serviceMotorState() {
  if (softwareStopLatched) {
    if (runState != STATE_IDLE) {
      stopAllMotors(false);
    }
    return;
  }

  const unsigned long nowMs = millis();

  if (runState == STATE_WAITING &&
      nowMs - stateStartedAtMs >= DIRECTION_CHANGE_PAUSE_MS) {
    startRequestedMotion();
  }

  if (runState == STATE_RUNNING &&
      nowMs - stateStartedAtMs >= activeRunDurationMs) {
    const Motion completedMotion = activeMotion;
    stopAllMotors(false);
    Serial.print(motionName(completedMotion));
    Serial.println(F(": pulse complete; stopped"));
    if (completedMotion == MIXER_FORWARD || completedMotion == MIXER_REVERSE) {
      emitEvent(F("MIXER_COMPLETED"), F("info"), F("mixer"),
                (automaticProcessIsActive() || diagnosticActive) ? F("running") : F("stopped"),
                F("Mixer completed"));
    } else if (completedMotion == PRESSER_EXTEND || completedMotion == PRESSER_RETRACT) {
      emitEvent(F("PRESSER_COMPLETED"), F("info"), F("presser"),
                (automaticProcessIsActive() || diagnosticActive) ? F("running") : F("stopped"),
                F("Presser pulse completed"));
    } else if (completedMotion == PUSHER_FORWARD || completedMotion == PUSHER_REVERSE) {
      emitEvent(F("PUSHER_COMPLETED"), F("info"), F("pusher"),
                (automaticProcessIsActive() || diagnosticActive) ? F("running") : F("stopped"),
                F("Pusher pulse completed"));
    }
  }
}

bool startAutomaticProcess() {
  if (diagnosticActive) {
    Serial.println(F("PROCESS REJECTED: component test running; stop it first"));
    return false;
  }
  if (!chamberMotionReady()) return false;
  if (statusSelfTestActive) {
    Serial.println(F("PROCESS REJECTED: finish the status test first"));
    return false;
  }

  if (!controlIoOnline) {
    Serial.println(F("PROCESS REJECTED: control-button expander is offline"));
    return false;
  }

  if (emergencyButton.stableActive) {
    Serial.println(F("PROCESS REJECTED: emergency contact is open"));
    return false;
  }

  if (stopButton.stableActive) {
    Serial.println(F("PROCESS REJECTED: physical Stop contact is open"));
    return false;
  }

  if (softwareStopLatched) {
    Serial.println(F("PROCESS REJECTED: send R to arm first"));
    return false;
  }

  if (automaticProcessIsActive()) {
    Serial.println(F("PROCESS REJECTED: a process is already running"));
    return false;
  }

  if (runState != STATE_IDLE) {
    Serial.println(F("PROCESS REJECTED: wait for the manual motor test to stop"));
    return false;
  }

  stopAllMotors(false);
  automaticProcessState = PROCESS_WAITING_TO_CLOSE_GATE;
  processStateStartedAtMs = millis();

  Serial.println();
  Serial.println(F("AUTOMATIC PROCESS STARTED"));
  Serial.println(F("STEP: closing mixer gate and molder"));
  Serial.print(F("Shredder + mixer time: "));
  Serial.print(PROCESS_SHREDDER_RUN_MS);
  Serial.print(F(" ms | Mixer-only finish time: "));
  Serial.print(PROCESS_MIXER_FINISH_MS);
  Serial.println(F(" ms"));
  emitEvent(F("PROCESS_STARTED"), F("info"), F("process"), F("running"),
            F("Process started"));
  return true;
}

void serviceControlButtons() {
  const unsigned long nowMs = millis();
  if (nowMs - lastButtonPollAtMs < BUTTON_POLL_MS) return;
  lastButtonPollAtMs = nowMs;

  uint8_t value = 0xFF;
  if (!readControlIo(value)) {
    if (controlIoOnline) {
      controlIoOnline = false;
      Serial.println(F("CONTROL BUTTONS LOST: latching software stop"));
      if (!softwareStopLatched || diagnosticActive || chamber.fanOn || chamber.unlockOn) latchSoftwareStop();
    }
    return;
  }

  if (!controlIoOnline) {
    initializeControlIo();
    return;
  }

  const bool stopChanged =
      updateButton(stopButton, (value & STOP_BUTTON_MASK) != 0, nowMs);
  const bool emergencyChanged = updateButton(
      emergencyButton, (value & EMERGENCY_BUTTON_MASK) != 0, nowMs);
  const bool resetChanged =
      updateButton(resetButton, (value & RESET_BUTTON_MASK) == 0, nowMs);
  const bool startChanged =
      updateButton(startButton, (value & START_BUTTON_MASK) == 0, nowMs);

  if (emergencyButton.stableActive) {
    if (emergencyChanged) {
      Serial.println(F("EMERGENCY STOP: contact opened"));
      if (!softwareStopLatched || diagnosticActive || chamber.fanOn || chamber.unlockOn) latchSoftwareStop();
    }
    return;
  }

  if (emergencyChanged) {
    Serial.println(F("EMERGENCY STOP: released; Reset is required to arm"));
  }

  if (stopButton.stableActive) {
    if (stopChanged) {
      Serial.println(F("STOP BUTTON: contact opened"));
      if (!softwareStopLatched || diagnosticActive || chamber.fanOn || chamber.unlockOn) latchSoftwareStop();
    }
    return;
  }

  if (stopChanged) {
    Serial.println(F("STOP BUTTON: released; Reset is required to arm"));
  }

  if (resetChanged && resetButton.stableActive) {
    Serial.println(F("RESET BUTTON: pressed"));
    armMotorTest();
    return;
  }

  if (startChanged && startButton.stableActive) {
    Serial.println(F("START BUTTON: pressed"));
    startAutomaticProcess();
  }
}

void serviceStatusLights() {
  const unsigned long nowMs = millis();
  if (!statusIoOnline) {
    if (nowMs - lastStatusIoRetryAtMs >= STATUS_IO_RETRY_MS) {
      lastStatusIoRetryAtMs = nowMs;
      initializeStatusIo();
    }
    return;
  }

  if (statusSelfTestActive) {
    const bool unsafeToContinue = !controlIoOnline || stopButton.stableActive ||
                                  emergencyButton.stableActive ||
                                  automaticProcessIsActive() ||
                                  runState != STATE_IDLE;
    if (unsafeToContinue) {
      statusSelfTestActive = false;
      writeStatusIo(0xFF);
      Serial.println(F("STATUS TEST CANCELLED: machine state changed"));
      return;
    }

    if (nowMs - statusSelfTestStepStartedAtMs >= STATUS_SELF_TEST_STEP_MS) {
      statusSelfTestStep++;
      statusSelfTestStepStartedAtMs = nowMs;
      if (statusSelfTestStep >= STATUS_SELF_TEST_STEP_COUNT) {
        statusSelfTestActive = false;
        writeStatusIo(0xFF);
        Serial.println(F("STATUS TEST COMPLETE: normal status display restored"));
        return;
      }
      printStatusSelfTestStep(statusSelfTestStep);
    }

    uint8_t testOutput = 0xFF;
    switch (statusSelfTestStep) {
      case 0:
        testOutput &= static_cast<uint8_t>(~READY_LED_MASK);
        break;
      case 1:
        testOutput &= static_cast<uint8_t>(~RUNNING_LED_MASK);
        break;
      case 2:
        testOutput &= static_cast<uint8_t>(~STOPPED_LED_MASK);
        break;
      case 3:
        testOutput &= static_cast<uint8_t>(~BUZZER_MASK);
        break;
      default:
        testOutput &= static_cast<uint8_t>(
            ~(READY_LED_MASK | RUNNING_LED_MASK | STOPPED_LED_MASK |
              BUZZER_MASK));
        break;
    }

    if (chamber.outputs(testOutput) != statusIoShadow && !writeStatusIo(testOutput)) {
      statusSelfTestActive = false;
      statusIoOnline = false;
      lastStatusIoRetryAtMs = nowMs;
      Serial.println(F("STATUS TEST FAILED: status-light expander was lost"));
    }
    return;
  }

  uint8_t desired = 0xFF;
  const bool blinkOn = ((nowMs / STATUS_LED_BLINK_MS) % 2) == 0;
  const bool safetyFault = !chamber.online || !chamber.doorClosed || !controlIoOnline || stopButton.stableActive ||
                           emergencyButton.stableActive;
  const bool preparing =
      automaticProcessState == PROCESS_WAITING_TO_CLOSE_GATE ||
      automaticProcessState == PROCESS_WAITING_FOR_GATE_TO_CLOSE;
  const bool motionActive = automaticProcessIsActive() ||
                            runState != STATE_IDLE;
  const bool completionBlinkActive = processCompletedAtMs != 0 &&
      nowMs - processCompletedAtMs < COMPLETED_LED_BLINK_MS;

  if (safetyFault) {
    // A broken/open Stop contact or lost button expander needs attention.
    if (blinkOn) {
      desired &= static_cast<uint8_t>(~STOPPED_LED_MASK);
      desired &= static_cast<uint8_t>(~BUZZER_MASK);
    }
  } else if (softwareStopLatched) {
    // A normal latched software stop remains clearly visible until Reset.
    desired &= static_cast<uint8_t>(~STOPPED_LED_MASK);
  } else if (preparing) {
    // The process was accepted and both gates are moving to their safe start.
    if (blinkOn) desired &= static_cast<uint8_t>(~RUNNING_LED_MASK);
  } else if (motionActive) {
    desired &= static_cast<uint8_t>(~RUNNING_LED_MASK);
  } else if (completionBlinkActive) {
    if (blinkOn) desired &= static_cast<uint8_t>(~READY_LED_MASK);
  } else {
    desired &= static_cast<uint8_t>(~READY_LED_MASK);
  }

  if (chamber.outputs(desired) != statusIoShadow && !writeStatusIo(desired)) {
    statusIoOnline = false;
    lastStatusIoRetryAtMs = nowMs;
    Serial.println(F("STATUS LIGHTS LOST: outputs switched off on reconnect"));
  }
}

void serviceAutomaticProcess() {
  if (softwareStopLatched) {
    automaticProcessState = PROCESS_IDLE;
    return;
  }
  // An idle process has no motion to guard. Do not spam rejection messages
  // during standalone fan/unlock tests or while waiting for the door.
  if (!automaticProcessIsActive()) return;
  if (!chamberMotionReady()) return;

  const unsigned long nowMs = millis();

  switch (automaticProcessState) {
    case PROCESS_WAITING_TO_CLOSE_GATE:
      if (nowMs - lastMotorStoppedAtMs >= DIRECTION_CHANGE_PAUSE_MS) {
        setMixerGateAngle(MIXER_GATE_CLOSED_ANGLE, F("CLOSED"));
        setMolderAngle(MOLDER_CLOSED_ANGLE, F("CLOSED"));
        automaticProcessState = PROCESS_WAITING_FOR_GATE_TO_CLOSE;
        processStateStartedAtMs = nowMs;
      }
      break;

    case PROCESS_WAITING_FOR_GATE_TO_CLOSE:
      if (nowMs - processStateStartedAtMs >= PROCESS_CLOSE_TRAVEL_MS) {
        // Run both motors together. The shredder uses hardware PWM while the
        // mixer uses its full-speed digital direction outputs on D9/D10.
        stopMotorOutputs();
        digitalWrite(SHREDDER_LPWM, LOW);
        analogWrite(SHREDDER_RPWM, SHREDDER_FORWARD_DUTY);
        // This installation's working mixer direction is LPWM/D10 (the same
        // direction verified by manual command 4).
        digitalWrite(MIXER_RPWM, LOW);
        digitalWrite(MIXER_LPWM, HIGH);
        automaticProcessState = PROCESS_SHREDDING_AND_MIXING;
        processStateStartedAtMs = nowMs;
        Serial.println(F("STEP: shredder and mixer started together"));
        emitEvent(F("SHREDDER_MIXER_STARTED"), F("info"), F("process"),
                  F("running"), F("Shredder and mixer started"));
      }
      break;

    case PROCESS_SHREDDING_AND_MIXING:
      if (nowMs - processStateStartedAtMs >= PROCESS_SHREDDER_RUN_MS) {
        // Stop only the shredder. The mixer remains running without a gap.
        digitalWrite(SHREDDER_RPWM, LOW);
        digitalWrite(SHREDDER_LPWM, LOW);
        automaticProcessState = PROCESS_MIXER_FINISHING;
        processStateStartedAtMs = nowMs;
        Serial.println(F("STEP: shredder stopped; mixer continuing alone"));
        emitEvent(F("SHREDDER_COMPLETED"), F("info"), F("shredder"),
                  F("running"), F("Shredder stopped; mixer continuing"));
      }
      break;

    case PROCESS_MIXER_FINISHING:
      if (nowMs - processStateStartedAtMs >= PROCESS_MIXER_FINISH_MS) {
        digitalWrite(MIXER_RPWM, LOW);
        digitalWrite(MIXER_LPWM, LOW);
        lastMotorStoppedAtMs = nowMs;
        automaticProcessState = PROCESS_WAITING_TO_OPEN_GATE;
        processStateStartedAtMs = nowMs;
        Serial.println(F("Mixer stopped; waiting before opening gate"));
        emitEvent(F("MIXER_COMPLETED"), F("info"), F("mixer"),
                  F("running"), F("Mixer finishing run completed"));
      }
      break;

    case PROCESS_WAITING_TO_OPEN_GATE:
      if (nowMs - lastMotorStoppedAtMs >= DIRECTION_CHANGE_PAUSE_MS) {
        Serial.println(F("STEP: opening mixer gate; molder remains closed"));
        setMixerGateAngle(MIXER_GATE_OPEN_ANGLE, F("OPEN"));
        automaticProcessState = PROCESS_WAITING_FOR_GATE_TO_OPEN;
        processStateStartedAtMs = nowMs;
      }
      break;

    case PROCESS_WAITING_FOR_GATE_TO_OPEN:
      if (nowMs - processStateStartedAtMs >= MIXER_GATE_TRAVEL_MS) {
        automaticProcessState = PROCESS_RELEASING_MATERIAL;
        processStateStartedAtMs = nowMs;
        Serial.println(F("STEP: releasing material into the closed molder"));
        emitEvent(F("MATERIAL_RELEASE_STARTED"), F("info"), F("process"),
                  F("running"), F("Releasing material into molder"));
      }
      break;

    case PROCESS_RELEASING_MATERIAL:
      if (nowMs - processStateStartedAtMs >= PROCESS_MATERIAL_RELEASE_MS) {
        Serial.println(F("STEP: closing mixer gate before pressing"));
        setMixerGateAngle(MIXER_GATE_CLOSED_ANGLE, F("CLOSED"));
        automaticProcessState = PROCESS_WAITING_FOR_MIXER_GATE_TO_CLOSE;
        processStateStartedAtMs = nowMs;
      }
      break;

    case PROCESS_WAITING_FOR_MIXER_GATE_TO_CLOSE:
      if (nowMs - processStateStartedAtMs >= MIXER_GATE_TRAVEL_MS) {
        Serial.println(F("STEP: presser extending"));
        emitEvent(F("PRESSING_STARTED"), F("info"), F("process"),
                  F("running"), F("Pressing sequence started"));
        requestPulse(PRESSER_EXTEND, PROCESS_PRESSER_EXTEND_MS);
        automaticProcessState = PROCESS_PRESSER_EXTENDING;
      }
      break;

    case PROCESS_PRESSER_EXTENDING:
      if (runState == STATE_IDLE) {
        Serial.println(F("STEP: presser extension complete; retracting"));
        requestPulse(PRESSER_RETRACT, PROCESS_PRESSER_RETRACT_MS);
        automaticProcessState = PROCESS_PRESSER_RETRACTING;
      }
      break;

    case PROCESS_PRESSER_RETRACTING:
      if (runState == STATE_IDLE) {
        Serial.println(F("Presser retracted; waiting before opening molder"));
        automaticProcessState = PROCESS_WAITING_TO_OPEN_MOLDER;
        processStateStartedAtMs = nowMs;
      }
      break;

    case PROCESS_WAITING_TO_OPEN_MOLDER:
      if (nowMs - lastMotorStoppedAtMs >= DIRECTION_CHANGE_PAUSE_MS) {
        Serial.println(F("STEP: opening molder gate for ejection"));
        emitEvent(F("EJECTION_STARTED"), F("info"), F("process"),
                  F("running"), F("Opening molder for ejection"));
        setMolderAngle(MOLDER_OPEN_ANGLE, F("OPEN"));
        automaticProcessState = PROCESS_WAITING_FOR_MOLDER_TO_OPEN;
        processStateStartedAtMs = nowMs;
      }
      break;

    case PROCESS_WAITING_FOR_MOLDER_TO_OPEN:
      if (nowMs - processStateStartedAtMs >= MOLDER_TRAVEL_MS) {
        Serial.println(F("STEP: molder open; pusher extending"));
        requestPulse(PUSHER_FORWARD, PROCESS_PUSHER_EXTEND_MS);
        automaticProcessState = PROCESS_PUSHER_EXTENDING;
      }
      break;

    case PROCESS_PUSHER_EXTENDING:
      if (runState == STATE_IDLE) {
        Serial.println(F("STEP: pusher extension complete; retracting"));
        requestPulse(PUSHER_REVERSE, PROCESS_PUSHER_RETRACT_MS);
        automaticProcessState = PROCESS_PUSHER_RETRACTING;
      }
      break;

    case PROCESS_PUSHER_RETRACTING:
      if (runState == STATE_IDLE) {
        Serial.println(F("Pusher retracted; waiting before closing molder"));
        automaticProcessState = PROCESS_WAITING_TO_CLOSE_MOLDER;
        processStateStartedAtMs = nowMs;
      }
      break;

    case PROCESS_WAITING_TO_CLOSE_MOLDER:
      if (nowMs - lastMotorStoppedAtMs >= DIRECTION_CHANGE_PAUSE_MS) {
        Serial.println(F("STEP: closing molder gate"));
        emitEvent(F("GATES_CLOSING"), F("info"), F("process"),
                  F("running"), F("Closing gates after ejection"));
        setMolderAngle(MOLDER_CLOSED_ANGLE, F("CLOSED"));
        automaticProcessState = PROCESS_WAITING_FOR_MOLDER_TO_CLOSE;
        processStateStartedAtMs = nowMs;
      }
      break;

    case PROCESS_WAITING_FOR_MOLDER_TO_CLOSE:
      if (nowMs - processStateStartedAtMs >= MOLDER_TRAVEL_MS) {
        automaticProcessState = PROCESS_IDLE;
        processCompletedAtMs = nowMs;
        Serial.println(F("PROCESS COMPLETE: mixer and molder gates closed"));
        emitEvent(F("PROCESS_COMPLETED"), F("info"), F("process"),
                  F("stopped"), F("Forming sequence finished; drying not enabled"));
      }
      break;

    case PROCESS_IDLE:
    default:
      break;
  }
}

void serviceChamber(bool force) {
  const unsigned long nowMs = millis();
  if (!force && nowMs - lastDoorPollAtMs < BUTTON_POLL_MS) return;
  lastDoorPollAtMs = nowMs;
  const bool wasFanOn = chamber.fanOn;
  const bool wasUnlockOn = chamber.unlockOn;
  chamber.tick(nowMs);
  const bool wasClosed = chamber.doorClosed;
  const bool wasOnline = chamber.online;
  const bool available = statusIoOnline &&
      Wire.requestFrom(STATUS_IO_ADDRESS, static_cast<uint8_t>(1)) == 1;
  const uint8_t inputs = available ? Wire.read() : 0xFF;
  lastStatusInputs = inputs;
  chamber.sample(available, (inputs & ChamberControl::DOOR) == 0, nowMs);
  if (!available) statusIoOnline = false;
  if ((!chamber.online || !chamber.doorClosed) &&
      (!softwareStopLatched || automaticProcessIsActive() || runState != STATE_IDLE)) {
    // Hardware protection must independently stop hazardous motion. This
    // software interlock neither releases holding torque nor repositions gates.
    latchSoftwareStop();
    emitEvent(F("FAULT"), F("error"), F("door"), F("fault"),
              F("Door open or input unavailable; Reset required"));
  }
  if (wasClosed != chamber.doorClosed || wasOnline != chamber.online) printChamberStatus();
  if (statusIoOnline && chamber.outputs(statusIoShadow) != statusIoShadow &&
      !writeStatusIo(statusIoShadow)) {
    statusIoOnline = false;
    chamber.sample(false, false, nowMs);
    if (!softwareStopLatched || diagnosticActive) latchSoftwareStop();
  }
  if (statusIoOnline && wasFanOn && !chamber.fanOn)
    Serial.println(F("FAN: OFF command sent"));
  if (statusIoOnline && wasUnlockOn && !chamber.unlockOn)
    Serial.println(F("UNLOCK: OFF command sent; pulse ended"));
}

bool chamberMotionReady() {
  serviceChamber(true);
  if (chamber.ready()) return true;
  if (!chamber.online) {
    Serial.println(F("ACTION BLOCKED: door/status board 0x24 not responding"));
  } else if (!chamber.doorClosed) {
    Serial.println(F("ACTION BLOCKED: door interlock P2 is OPEN; close door, send I, then Reset"));
  } else if (chamber.unlockOn) {
    Serial.println(F("ACTION BLOCKED: unlock pulse active; wait, then Reset"));
  } else if (chamber.fanOn) {
    Serial.println(F("ACTION BLOCKED: fan test active; send N to end test, then Reset"));
  }
  return false;
}

void printChamberStatus() {
  Serial.print(F("CHAMBER 0x24: door P2="));
  Serial.print(!chamber.online ? F("UNKNOWN") : chamber.doorClosed ? F("CLOSED") : F("OPEN"));
  Serial.print(F("; raw P2="));
  Serial.print(!chamber.online ? F("UNKNOWN") :
      (lastStatusInputs & ChamberControl::DOOR) ? F("HIGH") : F("LOW"));
  Serial.print(F("; fan P5=")); Serial.print(chamber.fanOn ? F("ON (command)") : F("OFF (command)"));
  Serial.print(F("; unlock P6=")); Serial.println(chamber.unlockOn ? F("ON (pulse)") : F("OFF"));
  if (chamber.online) {
    Serial.print(F("0x24 raw pins P0..P7: "));
    for (uint8_t pin = 0; pin < 8; ++pin) {
      Serial.print((lastStatusInputs & (1U << pin)) ? '1' : '0');
      if (pin < 7) Serial.print(' ');
    }
    Serial.println();
  }
}

bool runChamberCommand(const char *action) {
  if (diagnosticActive) return false;
  serviceChamber(true);
  const unsigned long nowMs = millis();
  const bool settled = softwareStopLatched && !automaticProcessIsActive() && runState == STATE_IDLE &&
      nowMs - lastMotorStoppedAtMs >= DIRECTION_CHANGE_PAUSE_MS &&
      nowMs - lastGateCommandAtMs >= DIRECTION_CHANGE_PAUSE_MS && !statusSelfTestActive;
  bool accepted = false;
  if (strcmp(action, "FAN_OFF") == 0) { chamber.fanOn = false; accepted = true; }
  else if (strcmp(action, "LOCK") == 0) { chamber.unlockOn = false; accepted = true; }
  else if (strcmp(action, "FAN_TEST") == 0) {
    accepted = controlIoOnline && !stopButton.stableActive && !emergencyButton.stableActive &&
        chamber.startFan(nowMs, settled);
  } else if (strcmp(action, "UNLOCK") == 0) {
    accepted = controlIoOnline && chamber.unlock(nowMs, settled);
  }
  if (!accepted) { Serial.println(F("CHAMBER REJECTED: stop, wait, and check door/board")); return false; }
  if (!statusIoOnline || !writeStatusIo(statusIoShadow)) {
    chamber.sample(false, false, nowMs); statusIoOnline = false; return false;
  }
  printChamberStatus();
  return true;
}

const __FlashStringHelper *diagnosticItem(uint8_t step) {
  switch (step) {
    case 0: return F("shredder");
    case 1: return F("mixer");
    case 2: case 3: return F("mixer_gate");
    case 4: case 5: return F("molder_gate");
    case 6: case 7: return F("presser");
    case 8: case 9: return F("pusher");
    case 10: return F("fan");
    case 11: return F("indicators");
    default: return F("lock");
  }
}

bool startComponentTest() {
  if (!chamberMotionReady() || diagnosticActive || softwareStopLatched ||
      automaticProcessIsActive() || runState != STATE_IDLE || statusSelfTestActive ||
      !controlIoOnline || stopButton.stableActive || emergencyButton.stableActive) return false;
  diagnosticActive = true;
  diagnosticStepRunning = false;
  diagnosticStep = 0;
  diagnosticStartedAtMs = millis();
  diagnosticStepAtMs = millis();
  emitEvent(F("TEST_STARTED"), F("info"), F("diagnostics"), F("running"),
            F("Unloaded component tests; completion is not movement feedback"));
  return true;
}

void serviceComponentTest() {
  if (!diagnosticActive) return;
  serviceChamber(true);
  if (!diagnosticActive) return;
  const unsigned long nowMs = millis();
  if (!chamber.online || !chamber.doorClosed || !controlIoOnline ||
      stopButton.stableActive || emergencyButton.stableActive ||
      nowMs - diagnosticStartedAtMs > 180000UL || nowMs - diagnosticStepAtMs > 15000UL) {
    latchSoftwareStop();
    return;
  }
  if (!diagnosticStepRunning) {
    if (nowMs - lastMotorStoppedAtMs < DIRECTION_CHANGE_PAUSE_MS ||
        nowMs - lastGateCommandAtMs < DIRECTION_CHANGE_PAUSE_MS) return;
    if (diagnosticStep != 3 && diagnosticStep != 5 && diagnosticStep != 7 && diagnosticStep != 9) {
      emitEvent(F("TEST_ITEM_STARTED"), F("info"), diagnosticItem(diagnosticStep), F("running"), F("Test command starting"));
    }
    diagnosticStepRunning = true;
    diagnosticStepAtMs = nowMs;
    switch (diagnosticStep) {
      case 0: requestPulse(SHREDDER_FORWARD, 1200); break;
      case 1: requestPulse(MIXER_REVERSE, 2000); break;
      case 2: setMixerGateAngle(MIXER_GATE_OPEN_ANGLE, F("TEST OPEN")); break;
      case 3: setMixerGateAngle(MIXER_GATE_CLOSED_ANGLE, F("TEST CLOSED")); break;
      case 4: setMolderAngle(MOLDER_OPEN_ANGLE, F("TEST OPEN")); break;
      case 5: setMolderAngle(MOLDER_CLOSED_ANGLE, F("TEST CLOSED")); break;
      case 6: requestPulse(PRESSER_EXTEND, 1000); break;
      case 7: requestPulse(PRESSER_RETRACT, 1000); break;
      case 8: requestPulse(PUSHER_FORWARD, 1000); break;
      case 9: requestPulse(PUSHER_REVERSE, 1000); break;
      case 10:
        if (!chamber.startFan(nowMs, true) || !writeStatusIo(statusIoShadow)) { latchSoftwareStop(); return; }
        break;
      case 11:
        statusSelfTestActive = true; statusSelfTestStep = 0;
        statusSelfTestStepStartedAtMs = nowMs;
        break;
      case 12:
        // All motion commands have finished and settled; no new motion may
        // be armed during or after this power-to-unlock pulse without Reset.
        softwareStopLatched = true;
        softwareStopLatchedAtMs = nowMs;
        if (!chamber.unlock(nowMs, true) || !writeStatusIo(statusIoShadow)) { latchSoftwareStop(); return; }
        break;
    }
    return;
  }
  bool finished = false;
  switch (diagnosticStep) {
    case 0: case 1: case 6: case 7: case 8: case 9:
      finished = runState == STATE_IDLE; break;
    case 2: case 3: case 4: case 5:
      finished = nowMs - diagnosticStepAtMs >= PROCESS_CLOSE_TRAVEL_MS; break;
    case 10:
      if (nowMs - diagnosticStepAtMs >= 3000) {
        chamber.fanOn = false;
        if (!writeStatusIo(statusIoShadow)) { latchSoftwareStop(); return; }
        finished = true;
      }
      break;
    case 11: finished = !statusSelfTestActive; break;
    case 12: finished = !chamber.unlockOn && (statusIoShadow & ChamberControl::UNLOCK) != 0; break;
  }
  if (!finished) return;
  if (diagnosticStep != 2 && diagnosticStep != 4 && diagnosticStep != 6 && diagnosticStep != 8) {
    emitEvent(F("TEST_ITEM_DONE"), F("info"), diagnosticItem(diagnosticStep), F("running"),
              F("Timed run completed; verify physical movement"));
  }
  diagnosticStep++;
  diagnosticStepRunning = false;
  diagnosticStepAtMs = nowMs;
  if (diagnosticStep > 12) {
    diagnosticActive = false;
    emitEvent(F("TEST_COMPLETED"), F("info"), F("diagnostics"), F("stopped"),
              F("Timed tests finished; physical function is not verified"));
  }
}

bool validRemoteCommandId(const char *id) {
  const size_t length = strlen(id);
  if (length == 0 || length >= sizeof(lastRemoteCommandId)) return false;
  for (size_t i = 0; i < length; ++i) {
    const char c = id[i];
    if (!isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') {
      return false;
    }
  }
  return true;
}

void failRemoteCommand(const char *id, const char *message) {
  emitAck(id, "FAILED", message);
  rememberRemoteResult(id, "FAILED", message);
}

void reportMachineHealth(const char *id) {
  // Read inputs and probe the indicator board without writing any outputs.
  uint8_t inputs = 0xFF;
  const bool controls = readControlIo(inputs);
  Wire.beginTransmission(STATUS_IO_ADDRESS);
  const bool indicators = Wire.endTransmission() == 0;
  espLink.print(F("HEALTH|"));
  espLink.print(id);
  espLink.print('|'); espLink.print(controls ? 1 : 0);
  espLink.print('|'); espLink.print(controls && (inputs & STOP_BUTTON_MASK) == 0 ? 1 : 0);
  espLink.print('|'); espLink.print(controls && (inputs & EMERGENCY_BUTTON_MASK) == 0 ? 1 : 0);
  espLink.print('|'); espLink.print(indicators ? 1 : 0);
  espLink.print('|'); espLink.print(!softwareStopLatched && chamber.ready() && !diagnosticActive ? 1 : 0);
  espLink.print('|'); espLink.print(chamber.online && chamber.doorClosed ? 1 : 0);
  espLink.print('|'); espLink.print(chamber.fanOn ? 1 : 0);
  espLink.print('|'); espLink.print(chamber.unlockOn ? 1 : 0);
  espLink.print('|'); espLink.println(diagnosticActive ? 1 : 0);
}

void handleRemoteCommandFrame(char *line) {
  // Mirror the received frame before strtok modifies it, so USB logs can
  // distinguish a missing incoming command from a missing outgoing reply.
  Serial.print(F("ESP32 -> Uno: "));
  Serial.println(line);
  char *prefix = strtok(line, "|");
  char *id = strtok(nullptr, "|");
  char *action = strtok(nullptr, "|");
  char *extra = strtok(nullptr, "|");
  if (prefix == nullptr || id == nullptr || action == nullptr || extra != nullptr ||
      strcmp(prefix, "CMD") != 0 || !validRemoteCommandId(id)) {
    latchSoftwareStop();
    emitEvent(F("FAULT"), F("error"), F("controller"), F("fault"),
              F("Invalid UART command"));
    return;
  }

  // This read-only probe must not replace the last actuator command's replay
  // protection. Older firmware rejects CHECK without starting any movement.
  if (strcmp(action, "CHECK") == 0) {
    reportMachineHealth(id);
    return;
  }

  // A repeated backend command ID must never repeat physical movement.
  if (strcmp(id, lastRemoteCommandId) == 0 && lastRemoteStatus[0] != '\0') {
    emitAck(id, lastRemoteStatus, lastRemoteMessage);
    return;
  }

  emitAck(id, "DELIVERED", "received");

  if (strcmp(action, "SELF_TEST") == 0) {
    if (startComponentTest()) {
      emitAck(id, "COMPLETED", "test_started");
      rememberRemoteResult(id, "COMPLETED", "test_started");
    } else failRemoteCommand(id, "test_rejected");
    return;
  }

  if (strcmp(action, "STOP") == 0) {
    latchSoftwareStop();
    emitAck(id, "COMPLETED", "stopped");
    rememberRemoteResult(id, "COMPLETED", "stopped");
    return;
  }

  if (strcmp(action, "RESET") == 0) {
    // RESET only clears the latched software stop and leaves every motor off.
    if (armMotorTest()) {
      emitAck(id, "COMPLETED", "armed");
      rememberRemoteResult(id, "COMPLETED", "armed");
    } else {
      failRemoteCommand(id, lastArmRejectReason);
    }
    return;
  }

  if (strcmp(action, "START") == 0) {
    // Starting is deliberately separate from arming. After boot or STOP, the
    // operator must issue RESET successfully before START can run anything.
    if (softwareStopLatched) {
      failRemoteCommand(id, "not_armed");
    } else if (automaticProcessIsActive() || runState != STATE_IDLE) {
      failRemoteCommand(id, "busy");
    } else if (startAutomaticProcess()) {
      emitAck(id, "COMPLETED", "started");
      rememberRemoteResult(id, "COMPLETED", "started");
    } else {
      failRemoteCommand(id, "start_rejected");
    }
    return;
  }

  failRemoteCommand(id, "bad_command");
}

void bridgeProtocolFault(const __FlashStringHelper *message) {
  const uint8_t partialLength = bridgeLineLength;
  bridgeLineLength = 0;
  latchSoftwareStop();
  // Keep the stop behavior, but show what was received before discarding it.
  // Printable text only so corrupted control bytes cannot garble the monitor.
  Serial.print(F("UART RX partial (")); Serial.print(static_cast<unsigned int>(partialLength));
  Serial.print(F(" bytes): ["));
  for (uint8_t i = 0; i < partialLength; ++i) {
    const uint8_t c = static_cast<uint8_t>(bridgeLine[i]);
    Serial.print(c >= 32 && c <= 126 ? static_cast<char>(c) : '?');
  }
  Serial.println(F("]"));
  emitEvent(F("FAULT"), F("error"), F("controller"), F("fault"), message);
}

void requestDhtCheck() {
  if (dhtCheckPending) { Serial.println(F("DHT22: waiting for ESP32 reply")); return; }
  dhtCheckPending = true;
  dhtCheckSentAt = millis();
  ++dhtCheckSequence;
  espLink.print(F("DHT_CHECK|")); espLink.println(dhtCheckSequence);
  Serial.println(F("Reading chamber DHT22 on ESP32 GPIO27..."));
}

void handleDhtReply(char *line) {
  // Read-only response: never touches actuator replay IDs or motion state.
  strtok(line, "|");
  char *id = strtok(nullptr, "|");
  char *status = strtok(nullptr, "|");
  char *temperature = strtok(nullptr, "|");
  char *humidity = strtok(nullptr, "|");
  char *extra = strtok(nullptr, "|");
  if (!dhtCheckPending || millis() - dhtCheckSentAt >= 10000UL || !id || !status) return;
  char *end;
  const unsigned long sequence = strtoul(id, &end, 10);
  if (*end || sequence != dhtCheckSequence) return;
  if (strcmp(status, "ERROR") == 0 && !temperature) {
    dhtCheckPending = false;
    Serial.println(F("DHT22: no fresh valid reading. Check ESP32 sensor wiring; try S again."));
    return;
  }
  if (strcmp(status, "OK") != 0 || !temperature || !humidity || extra) return;
  const double t = strtod(temperature, &end);
  if (*end || !isfinite(t) || t < -40 || t > 80) return;
  const double h = strtod(humidity, &end);
  if (*end || !isfinite(h) || h < 0 || h > 100) return;
  dhtCheckPending = false;
  Serial.print(F("Chamber DHT22: ")); Serial.print(temperature);
  Serial.print(F(" C, ")); Serial.print(humidity); Serial.println(F(" %RH (ESP32 reading)"));
}

void serviceBridgeCommands() {
  while (espLink.available() > 0) {
    const char c = static_cast<char>(espLink.read());
    bridgeLastByteAtMs = millis();
    if (c == '\n') {
      bridgeLine[bridgeLineLength] = '\0';
      if (strncmp(bridgeLine, "DHT|", 4) == 0) handleDhtReply(bridgeLine);
      else if (bridgeLineLength > 0) handleRemoteCommandFrame(bridgeLine);
      bridgeLineLength = 0;
    } else if (c != '\r') {
      if (bridgeLineLength + 1 < BRIDGE_LINE_CAPACITY) {
        bridgeLine[bridgeLineLength++] = c;
      } else {
        bridgeProtocolFault(F("UART frame overflow"));
        while (espLink.available() > 0 && espLink.read() != '\n') {}
      }
    }
  }

  if (bridgeLineLength > 0 &&
      millis() - bridgeLastByteAtMs >= BRIDGE_FRAME_TIMEOUT_MS) {
    bridgeProtocolFault(F("UART frame timeout"));
  }
  if (dhtCheckPending && millis() - dhtCheckSentAt >= 10000UL) {
    dhtCheckPending = false;
    Serial.println(F("DHT22: ESP32 reply timed out. Update ESP32 firmware and check UART link."));
  }
}

void serviceSerialCommands() {
  while (Serial.available() > 0) {
    const char command = Serial.read();

    switch (command) {
      case 's': case 'S': requestDhtCheck(); break;
      case 'q': case 'Q':
        if (!startComponentTest()) Serial.println(F("TEST REJECTED: close door, Reset, and stop other actions"));
        break;
      case 'i': case 'I':
        serviceChamber(true); printChamberStatus(); break;
      case 'v': case 'V':
        runChamberCommand("FAN_TEST"); break;
      case 'n': case 'N':
        runChamberCommand("FAN_OFF"); break;
      case 'u': case 'U':
        runChamberCommand("UNLOCK"); break;
      case 'k': case 'K':
        runChamberCommand("LOCK"); break;
      case '1':
        requestManualPulse(SHREDDER_FORWARD);
        break;
      case '2':
        requestManualPulse(SHREDDER_REVERSE);
        break;
      case '3':
        requestManualPulse(MIXER_FORWARD);
        break;
      case '4':
        requestManualPulse(MIXER_REVERSE);
        break;
      case 'e':
      case 'E':
        requestManualPulse(PRESSER_EXTEND);
        break;
      case 't':
      case 'T':
        requestManualPulse(PRESSER_RETRACT);
        break;
      case 'f':
      case 'F':
        requestManualPulse(PUSHER_FORWARD);
        break;
      case 'b':
      case 'B':
        requestManualPulse(PUSHER_REVERSE);
        break;
      case '5':
        moveMolder(MOLDER_OPEN_ANGLE, F("OPEN"));
        break;
      case '6':
        moveMolder(MOLDER_CLOSED_ANGLE, F("CLOSED"));
        break;
      case '7':
        if (automaticProcessIsActive() || diagnosticActive) {
          Serial.println(F("MOLDER RELEASE REJECTED: process is running"));
        } else if (runState == STATE_IDLE) {
          releaseMolderServo(true);
        } else {
          Serial.println(F("MOLDER RELEASE REJECTED: stop the motor first"));
        }
        break;
      case 'p':
      case 'P':
        startAutomaticProcess();
        break;
      case 'o':
      case 'O':
        moveMixerGate(MIXER_GATE_OPEN_ANGLE, F("OPEN"));
        break;
      case 'c':
      case 'C':
        moveMixerGate(MIXER_GATE_CLOSED_ANGLE, F("CLOSED"));
        break;
      case 'd':
      case 'D':
        if (automaticProcessIsActive() || diagnosticActive) {
          Serial.println(F("SERVO RELEASE REJECTED: process is running"));
        } else if (runState == STATE_IDLE) {
          releaseMixerGateServo(true);
        } else {
          Serial.println(F("SERVO RELEASE REJECTED: stop the motor first"));
        }
        break;
      case 'x':
      case 'X':
        latchSoftwareStop();
        return;
      case 'r':
      case 'R':
        armMotorTest();
        break;
      case 'l':
      case 'L':
        startStatusSelfTest();
        break;
      case 'h':
      case 'H':
      case '?':
        printHelp();
        break;
      case '\r':
      case '\n':
      case ' ':
      case '\t':
        // Ignore Serial Monitor line endings and whitespace.
        break;
      default:
        Serial.print(F("Unknown command: "));
        Serial.println(command);
        Serial.println(F("Send H to show the command list."));
        break;
    }
  }
}

void setup() {
  // Load LOW before switching each output to OUTPUT to reduce startup glitches.
  digitalWrite(SHREDDER_RPWM, LOW);
  digitalWrite(SHREDDER_LPWM, LOW);
  digitalWrite(MIXER_RPWM, LOW);
  digitalWrite(MIXER_LPWM, LOW);
  digitalWrite(PRESSER_RPWM, LOW);
  digitalWrite(PRESSER_LPWM, LOW);
  digitalWrite(PUSHER_IN3, LOW);
  digitalWrite(PUSHER_IN4, LOW);
  digitalWrite(MIXER_GATE_SERVO_PIN, LOW);
  digitalWrite(MOLDER_SERVO_PIN, LOW);

  pinMode(SHREDDER_RPWM, OUTPUT);
  pinMode(SHREDDER_LPWM, OUTPUT);
  pinMode(MIXER_RPWM, OUTPUT);
  pinMode(MIXER_LPWM, OUTPUT);
  pinMode(PRESSER_RPWM, OUTPUT);
  pinMode(PRESSER_LPWM, OUTPUT);
  pinMode(PUSHER_IN3, OUTPUT);
  pinMode(PUSHER_IN4, OUTPUT);
  pinMode(MIXER_GATE_SERVO_PIN, OUTPUT);
  pinMode(MOLDER_SERVO_PIN, OUTPUT);
  stopAllMotors(false);
  releaseMixerGateServo(false);
  releaseMolderServo(false);

  Serial.begin(9600);
  espLink.begin(ESP_LINK_BAUD);
  Serial.println(F("Uno link diagnostic: RX=A2, TX=A3, baud=9600"));
  Wire.begin();
  Wire.setWireTimeout(25000, true);
  delay(500);
  initializeControlIo();
  initializeStatusIo();
  serviceChamber(true);
  printHelp();
  emitEvent(F("CONTROLLER_READY"), F("info"), F("controller"), F("stopped"),
            F("Controller ready"));
}

void loop() {
  serviceChamber();
  serviceControlButtons();
  serviceStatusLights();
  serviceBridgeCommands();
  serviceMotorState();
  serviceComponentTest();
  serviceAutomaticProcess();
  serviceSerialCommands();
  serviceChamber();
  serviceControlButtons();
  serviceStatusLights();
  serviceBridgeCommands();
  serviceMotorState();
  serviceComponentTest();
  serviceAutomaticProcess();
}
