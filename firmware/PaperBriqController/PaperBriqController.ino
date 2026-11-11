// PaperBriq production controller for Arduino Uno.
// Upload ArduinoUnoMotorTest separately for individual actuator calibration.
#if defined(PAPERBRIQ_STANDALONE_BENCH) && PAPERBRIQ_STANDALONE_BENCH
#error "Production firmware cannot enable standalone bench mode."
#endif
#define PAPERBRIQ_STANDALONE_BENCH 0

#include <Arduino.h>
#include <Servo.h>
#include <SoftwareSerial.h>
#include <Wire.h>
#include "ChamberControl.h"
#include "TimedCommands.h"
#include "ProductionTimer.h"
#include "ThermalCycle.h"
#include "ProductionLimits.h"
#include <stdlib.h>
#include <math.h>

void serviceChamber(bool force = false);
bool chamberMotionReady();
bool runChamberCommand(const char *action);
void printChamberStatus();
bool startComponentTest();
void serviceComponentTest();
void stopHeaterTest();
void serviceHeaterTest();
void stopPumpTest();
void servicePumpTest();
void startPumpTest();
void startHeaterTest();
bool benchConditionsReady();
void serviceProduction();
void handlePaperSignal(char value);
ProductionTimer production;
ThermalCycle thermal;
bool paperClearSeen = false;
bool paperDetected = false;
bool sensorControlledProcess = false;
bool productionPumpWasOn = false;
unsigned long paperSignalAt = 0;

// Production uses verified operating limits and the external thermostat/cutoff.
// The heater fan remains continuously powered. Bench mode is locked off above;
// individual calibration commands belong to the separate diagnostic sketch.
#ifndef PAPERBRIQ_VERBOSE_USB_LOGS
#define PAPERBRIQ_VERBOSE_USB_LOGS PAPERBRIQ_STANDALONE_BENCH
#endif
#if PAPERBRIQ_VERBOSE_USB_LOGS
#define USB_LOG Serial
#else
// Keep UART ACK/events and all USB input commands; omit verbose USB copies to
// leave flash space for production timing. Diagnostic builds can restore them.
struct QuietUsbLog {
  template<class T> void print(const T &) {}
  template<class T> void println(const T &) {}
  void println() {}
} USB_LOG;
#endif
const unsigned long HEATER_TEST_PULSE_MS = PAPERBRIQ_STANDALONE_BENCH ? 10000UL : 3000UL;
bool heaterBenchUsed = false;

// 12 V / 6 A pump: verify D3 trigger, fused power, suitable inductive-load
// suppression and water supply before enabling this supervised bench test.
#ifndef PAPERBRIQ_PUMP_VERIFIED
#define PAPERBRIQ_PUMP_VERIFIED 1
#endif
const uint8_t PUMP_ENABLE_PIN = 3;
bool pumpTestActive = false, pumpHasRun = false;
unsigned long pumpTestAt = 0;
unsigned long pumpDurationMs = 3000;
unsigned long heaterDurationMs = HEATER_TEST_PULSE_MS;

// Enable only after verifying D4 -> thermostat S1, S2 -> MOSFET TRIG,
// independent thermal cutoff, fused load wiring and continuous heater airflow.
#ifndef PAPERBRIQ_HEATER_VERIFIED
#define PAPERBRIQ_HEATER_VERIFIED 1
#endif
const uint8_t HEATER_ENABLE_PIN = 4;
bool heaterTestActive = false;
bool heaterReadingValid = false;
unsigned long heaterTestAt = 0, heaterReadingAt = 0;
float heaterTemperature = 0;

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
// P2 remains HIGH/released. P7 stays HIGH/off; heater enable uses Uno D4.
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

// Forming movement timings. Operator confirmed all four 5-second presser/pusher
// travels on 2026-10-31. Pump duration is a separate per-batch setting.
const unsigned long BENCH_SHREDDER_RUN_MS = 5000;
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
  PROCESS_WAITING_FOR_PAPER,
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
    USB_LOG.println(F("CONTROL BUTTONS OFFLINE: PCF8574 not found at 0x20"));
    return false;
  }

  const unsigned long nowMs = millis();
  seedButton(startButton, (value & START_BUTTON_MASK) == 0, nowMs);
  seedButton(resetButton, (value & RESET_BUTTON_MASK) == 0, nowMs);
  seedButton(stopButton, (value & STOP_BUTTON_MASK) != 0, nowMs);
  seedButton(emergencyButton,
             (value & EMERGENCY_BUTTON_MASK) != 0, nowMs);
  controlIoOnline = true;
  USB_LOG.print(F("CONTROL BUTTONS ONLINE at 0x20; stop contact is "));
  USB_LOG.print(stopButton.stableActive ? F("OPEN") : F("CLOSED"));
  USB_LOG.print(F("; emergency contact is "));
  USB_LOG.println(emergencyButton.stableActive ? F("OPEN") : F("CLOSED"));
  return true;
}

bool initializeStatusIo() {
  chamber.cancel();
  chamber.sample(false, false, millis());
  // HIGH switches every active-low indicator off and leaves unused pins safe.
  if (!writeStatusIo(0xFF)) {
    statusIoOnline = false;
    USB_LOG.println(F("STATUS LIGHTS OFFLINE: PCF8574 not found at 0x24"));
    return false;
  }
  statusIoOnline = true;
  USB_LOG.println(F("STATUS LIGHTS ONLINE at 0x24"));
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
    USB_LOG.println(F("ALL MOTORS: stopped"));
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
    USB_LOG.println(F("MIXER GATE: servo released; no holding torque"));
  }
}

void releaseMolderServo(bool printMessage = true) {
  if (molderServo.attached()) {
    molderServo.detach();
  }
  pinMode(MOLDER_SERVO_PIN, OUTPUT);
  digitalWrite(MOLDER_SERVO_PIN, LOW);

  if (printMessage) {
    USB_LOG.println(F("MOLDER: servo released; no holding torque"));
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

  USB_LOG.print(F("MIXER GATE: moving to "));
  USB_LOG.print(positionName);
  USB_LOG.print(F(" at "));
  USB_LOG.print(safeAngle);
  USB_LOG.println(F(" degrees; servo holding position"));
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

  USB_LOG.print(F("MOLDER: moving to "));
  USB_LOG.print(positionName);
  USB_LOG.print(F(" at "));
  USB_LOG.print(safeAngle);
  USB_LOG.println(F(" degrees; servo holding position"));
}

void closeAndHoldBothGates() {
  setMixerGateAngle(MIXER_GATE_CLOSED_ANGLE, F("CLOSED"));
  setMolderAngle(MOLDER_CLOSED_ANGLE, F("CLOSED"));
}

void moveMixerGate(uint8_t angle, const __FlashStringHelper *positionName) {
  if (diagnosticActive) return;
  if (!chamberMotionReady()) return;
  if (softwareStopLatched) {
    USB_LOG.println(F("SERVO COMMAND REJECTED: send R to arm first"));
    return;
  }

  if (automaticProcessIsActive()) {
    USB_LOG.println(F("SERVO COMMAND REJECTED: automatic process is running"));
    return;
  }

  // Stop an active/pending motor request, then make the operator resend the
  // gate command after the mechanical coast-down interval.
  if (runState != STATE_IDLE) {
    stopAllMotors(false);
    USB_LOG.println(F("MIXER GATE: motor stopped; wait 2.5 s, then resend O/C"));
    return;
  }

  const unsigned long stoppedForMs = millis() - lastMotorStoppedAtMs;
  if (stoppedForMs < DIRECTION_CHANGE_PAUSE_MS) {
    USB_LOG.print(F("MIXER GATE: wait another "));
    USB_LOG.print(DIRECTION_CHANGE_PAUSE_MS - stoppedForMs);
    USB_LOG.println(F(" ms for motor coast-down"));
    return;
  }

  setMixerGateAngle(angle, positionName);
}

void moveMolder(uint8_t angle, const __FlashStringHelper *positionName) {
  if (diagnosticActive) return;
  if (!chamberMotionReady()) return;
  if (softwareStopLatched) {
    USB_LOG.println(F("MOLDER COMMAND REJECTED: send R to arm first"));
    return;
  }

  if (automaticProcessIsActive()) {
    USB_LOG.println(F("MOLDER COMMAND REJECTED: automatic process is running"));
    return;
  }

  if (runState != STATE_IDLE) {
    stopAllMotors(false);
    USB_LOG.println(F("MOLDER: motor stopped; wait 2.5 s, then resend 5/6"));
    return;
  }

  const unsigned long stoppedForMs = millis() - lastMotorStoppedAtMs;
  if (stoppedForMs < DIRECTION_CHANGE_PAUSE_MS) {
    USB_LOG.print(F("MOLDER: wait another "));
    USB_LOG.print(DIRECTION_CHANGE_PAUSE_MS - stoppedForMs);
    USB_LOG.println(F(" ms for motor coast-down"));
    return;
  }

  setMolderAngle(angle, positionName);
}

void latchSoftwareStop() {
  production.stop(false, millis());
  stopPumpTest();
  stopHeaterTest();
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
  USB_LOG.println(F("SOFTWARE STOP LATCHED: motors stopped; Reset required"));
  USB_LOG.println(F("Inspect the machine, then send R to arm again."));
  emitEvent(F("STOPPED"), F("warning"), F("controller"), F("stopped"),
            F("Software stop latched"));
}

const char *lastArmRejectReason = "reset_rejected";

bool armController() {
  if (thermal.heating(production)) return false;
  lastArmRejectReason = "reset_rejected";
  if (pumpTestActive) { lastArmRejectReason = "pump_active"; return false; }
  if (heaterTestActive) { lastArmRejectReason = "heater_active"; return false; }
  if (diagnosticActive) {
    lastArmRejectReason = "test_running";
    USB_LOG.println(F("ARM REJECTED: component test running; stop it first"));
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
    USB_LOG.println(F("ARM REJECTED: control-button expander is offline"));
    return false;
  }

  if (emergencyButton.stableActive) {
    lastArmRejectReason = "emergency_open";
    USB_LOG.println(F("ARM REJECTED: emergency contact is open"));
    return false;
  }

  if (stopButton.stableActive) {
    lastArmRejectReason = "stop_open";
    USB_LOG.println(F("ARM REJECTED: physical Stop contact is open"));
    return false;
  }

  if (automaticProcessIsActive()) {
    lastArmRejectReason = "process_running";
    USB_LOG.println(F("ARM REJECTED: automatic process is already running"));
    return false;
  }

  if (millis() - softwareStopLatchedAtMs < SOFTWARE_STOP_REARM_DELAY_MS) {
    lastArmRejectReason = "reset_too_soon";
    USB_LOG.println(F("ARM REJECTED: wait 1 second after X, then send R again"));
    return false;
  }

  stopAllMotors(false);
  softwareStopLatched = false;
  USB_LOG.println(F("CONTROLLER READY: press Start for a prepared batch"));
  emitEvent(F("ARMED"), F("info"), F("controller"), F("stopped"),
            F("Controller armed"));
  return true;
}

void printStatusSelfTestStep(uint8_t step) {
  USB_LOG.print(F("STATUS TEST: "));
  switch (step) {
    case 0:
      USB_LOG.println(F("green Ready/Check light"));
      break;
    case 1:
      USB_LOG.println(F("yellow Preparing/Running light"));
      break;
    case 2:
      USB_LOG.println(F("red Stop/Fault light"));
      break;
    case 3:
      USB_LOG.println(F("buzzer"));
      break;
    default:
      USB_LOG.println(F("all three lights and buzzer"));
      break;
  }
}

void startStatusSelfTest() {
  if (diagnosticActive) return;
  if (!chamberMotionReady()) return;
  if (!statusIoOnline) {
    USB_LOG.println(F("STATUS TEST REJECTED: status-light expander is offline"));
    return;
  }

  if (!controlIoOnline || stopButton.stableActive ||
      emergencyButton.stableActive) {
    USB_LOG.println(F("STATUS TEST REJECTED: clear Stop/Emergency faults first"));
    return;
  }

  if (automaticProcessIsActive() || runState != STATE_IDLE) {
    USB_LOG.println(F("STATUS TEST REJECTED: wait until all movement has stopped"));
    return;
  }

  if (!writeStatusIo(0xFF)) {
    statusIoOnline = false;
    USB_LOG.println(F("STATUS TEST REJECTED: status-light expander did not respond"));
    return;
  }

  statusSelfTestActive = true;
  statusSelfTestStep = 0;
  statusSelfTestStepStartedAtMs = millis();
  USB_LOG.println(F("STATUS TEST STARTED: no motors will move"));
  printStatusSelfTestStep(statusSelfTestStep);
}

void printHelp() {
  Serial.println(F("PaperBriqController - PRODUCTION | USB 9600 baud"));
  Serial.println(F("R=Reset; P=Start forming; X=Stop; S=temperature; I=door status"));
  Serial.println(F("Q=supervised unloaded component check; U=unlock; K=end unlock"));
  Serial.println(F("Set/apply pump time in app before Reset and Start."));
  Serial.println(F("Use app for preheat/drying. Individual actuator tests: ArduinoUnoMotorTest."));
}

void requestPulse(Motion motion, unsigned long runDurationMs) {
  if (!chamberMotionReady()) return;
  if (softwareStopLatched) {
    USB_LOG.println(F("COMMAND REJECTED: send R to arm first"));
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

  USB_LOG.print(motionName(motion));
  USB_LOG.print(F(": requested at "));
  USB_LOG.print(dutyPercent(motionDuty(motion)));
  USB_LOG.println(F("% PWM; waiting for safety pause"));
}

void requestManualPulse(Motion motion) {
  if (diagnosticActive) return;
  if (statusSelfTestActive) {
    USB_LOG.println(F("MANUAL COMMAND REJECTED: finish the status test first"));
    return;
  }

  if (automaticProcessIsActive()) {
    USB_LOG.println(F("MANUAL COMMAND REJECTED: automatic process is running"));
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

  USB_LOG.print(motionName(activeMotion));
  USB_LOG.print(F(": running for "));
  USB_LOG.print(activeRunDurationMs);
  USB_LOG.println(F(" ms"));
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
    USB_LOG.print(motionName(completedMotion));
    USB_LOG.println(F(": pulse complete; stopped"));
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
  if (!PAPERBRIQ_STANDALONE_BENCH &&
      !(production.kind == 'P' && production.phase == ProductionTimer::WAITING)) {
    USB_LOG.println(F("PROCESS REJECTED: set and enable the batch pump time first"));
    return false;
  }
  if (diagnosticActive) {
    USB_LOG.println(F("PROCESS REJECTED: component test running; stop it first"));
    return false;
  }
  if (!chamberMotionReady()) return false;
  if (statusSelfTestActive) {
    USB_LOG.println(F("PROCESS REJECTED: finish the status test first"));
    return false;
  }

  if (!controlIoOnline) {
    USB_LOG.println(F("PROCESS REJECTED: control-button expander is offline"));
    return false;
  }

  if (emergencyButton.stableActive) {
    USB_LOG.println(F("PROCESS REJECTED: emergency contact is open"));
    return false;
  }

  if (stopButton.stableActive) {
    USB_LOG.println(F("PROCESS REJECTED: physical Stop contact is open"));
    return false;
  }

  if (softwareStopLatched) {
    USB_LOG.println(F("PROCESS REJECTED: send R to arm first"));
    return false;
  }

  if (automaticProcessIsActive()) {
    USB_LOG.println(F("PROCESS REJECTED: a process is already running"));
    return false;
  }

  if (runState != STATE_IDLE) {
    USB_LOG.println(F("PROCESS REJECTED: wait for the manual motor test to stop"));
    return false;
  }

  stopAllMotors(false);
  automaticProcessState = PROCESS_WAITING_TO_CLOSE_GATE;
  sensorControlledProcess = !(PAPERBRIQ_STANDALONE_BENCH && !production.active());
  processStateStartedAtMs = millis();

  USB_LOG.println();
  USB_LOG.println(F("AUTOMATIC PROCESS STARTED"));
  USB_LOG.println(F("STEP: closing mixer gate and molder"));
  USB_LOG.print(F("Shredder follows inlet; mixer finish time: "));
  USB_LOG.print(PROCESS_MIXER_FINISH_MS);
  USB_LOG.println(F(" ms"));
  emitEvent(F("PROCESS_STARTED"), F("info"), F("process"), F("running"),
            F("Single batch"));
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
      USB_LOG.println(F("CONTROL BUTTONS LOST: latching software stop"));
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
      USB_LOG.println(F("EMERGENCY STOP: contact opened"));
      if (!softwareStopLatched || diagnosticActive || chamber.fanOn || chamber.unlockOn) latchSoftwareStop();
    }
    return;
  }

  if (emergencyChanged) {
    USB_LOG.println(F("EMERGENCY STOP: released; Reset is required to arm"));
  }

  if (stopButton.stableActive) {
    if (stopChanged) {
      USB_LOG.println(F("STOP BUTTON: contact opened"));
      if (!softwareStopLatched || diagnosticActive || chamber.fanOn || chamber.unlockOn) latchSoftwareStop();
    }
    return;
  }

  if (stopChanged) {
    USB_LOG.println(F("STOP BUTTON: released; Reset is required to arm"));
  }

  if (resetChanged && resetButton.stableActive) {
    USB_LOG.println(F("RESET BUTTON: pressed"));
    armController();
    return;
  }

  if (startChanged && startButton.stableActive) {
    USB_LOG.println(F("START BUTTON: pressed"));
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
      USB_LOG.println(F("STATUS TEST CANCELLED: machine state changed"));
      return;
    }

    if (nowMs - statusSelfTestStepStartedAtMs >= STATUS_SELF_TEST_STEP_MS) {
      statusSelfTestStep++;
      statusSelfTestStepStartedAtMs = nowMs;
      if (statusSelfTestStep >= STATUS_SELF_TEST_STEP_COUNT) {
        statusSelfTestActive = false;
        writeStatusIo(0xFF);
        USB_LOG.println(F("STATUS TEST COMPLETE: normal status display restored"));
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
      USB_LOG.println(F("STATUS TEST FAILED: status-light expander was lost"));
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
    USB_LOG.println(F("STATUS LIGHTS LOST: outputs switched off on reconnect"));
  }
}

void startPaperProcessing() {
  stopMotorOutputs();
  digitalWrite(SHREDDER_LPWM, LOW);
  analogWrite(SHREDDER_RPWM, SHREDDER_FORWARD_DUTY);
  digitalWrite(MIXER_RPWM, LOW);
  digitalWrite(MIXER_LPWM, HIGH);
  automaticProcessState = PROCESS_SHREDDING_AND_MIXING;
  processStateStartedAtMs = millis();
  emitEvent(F("SHREDDER_MIXER_STARTED"), F("info"), F("process"),
            F("running"), F("Paper detected; shredding and mixing"));
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

  // A stale last-detected level must never leave the shredder running. Keep
  // checking the inlet link until feeding/finish-mixing has actually ended.
  const bool feeding = automaticProcessState == PROCESS_WAITING_FOR_PAPER ||
      automaticProcessState == PROCESS_SHREDDING_AND_MIXING ||
      automaticProcessState == PROCESS_MIXER_FINISHING;
  if (sensorControlledProcess && feeding && nowMs - paperSignalAt >= 1500UL &&
      (automaticProcessState != PROCESS_WAITING_FOR_PAPER ||
       nowMs - processStateStartedAtMs > 2000UL)) {
    latchSoftwareStop();
    emitEvent(F("FAULT"), F("error"), F("paper_sensor"), F("fault"),
              F("Paper sensor link lost; Reset required"));
    return;
  }

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
        if (!sensorControlledProcess) startPaperProcessing();
        else {
          automaticProcessState = PROCESS_WAITING_FOR_PAPER;
          processStateStartedAtMs = nowMs;
          paperClearSeen = false;
          emitEvent(F("WAITING_FOR_PAPER"), F("info"), F("process"),
                    F("running"), F("Ready for paper at shredder inlet"));
        }
      }
      break;

    case PROCESS_WAITING_FOR_PAPER:
      // No motors or water until a fresh clear-to-detected transition.
      break;

    case PROCESS_SHREDDING_AND_MIXING:
      if (sensorControlledProcess ? !paperDetected :
          nowMs - processStateStartedAtMs >= BENCH_SHREDDER_RUN_MS) {
        // Stop only the shredder. The mixer remains running without a gap.
        digitalWrite(SHREDDER_RPWM, LOW);
        digitalWrite(SHREDDER_LPWM, LOW);
        automaticProcessState = PROCESS_MIXER_FINISHING;
        processStateStartedAtMs = nowMs;
        USB_LOG.println(F("STEP: shredder stopped; mixer continuing alone"));
        emitEvent(F("SHREDDER_COMPLETED"), F("info"), F("shredder"),
                  F("running"), F("Shredder stopped; mixer continuing"));
      }
      break;

    case PROCESS_MIXER_FINISHING:
      if (sensorControlledProcess && paperDetected) {
        // New paper during the feeding gap resumes shredding without changing
        // the batch's pump deadline or interrupting the mixer.
        digitalWrite(SHREDDER_LPWM, LOW);
        analogWrite(SHREDDER_RPWM, SHREDDER_FORWARD_DUTY);
        automaticProcessState = PROCESS_SHREDDING_AND_MIXING;
        processStateStartedAtMs = nowMs;
        emitEvent(F("SHREDDER_MIXER_STARTED"), F("info"), F("process"),
                  F("running"), F("Paper detected; shredding resumed"));
        break;
      }
      if (production.kind == 'P' && production.phase == ProductionTimer::RUNNING) break;
      if (production.kind == 'P' && production.phase == ProductionTimer::WAITING &&
          nowMs - processStateStartedAtMs >= PROCESS_MIXER_FINISH_MS) {
        production.stop(true, nowMs); latchSoftwareStop(); return;
      }
      if (nowMs - processStateStartedAtMs >= PROCESS_MIXER_FINISH_MS) {
        digitalWrite(MIXER_RPWM, LOW);
        digitalWrite(MIXER_LPWM, LOW);
        lastMotorStoppedAtMs = nowMs;
        automaticProcessState = PROCESS_WAITING_TO_OPEN_GATE;
        processStateStartedAtMs = nowMs;
        USB_LOG.println(F("Mixer stopped; waiting before opening gate"));
        emitEvent(F("MIXER_COMPLETED"), F("info"), F("mixer"),
                  F("running"), F("Mixer finishing run completed"));
      }
      break;

    case PROCESS_WAITING_TO_OPEN_GATE:
      if (nowMs - lastMotorStoppedAtMs >= DIRECTION_CHANGE_PAUSE_MS) {
        USB_LOG.println(F("STEP: opening mixer gate; molder remains closed"));
        setMixerGateAngle(MIXER_GATE_OPEN_ANGLE, F("OPEN"));
        automaticProcessState = PROCESS_WAITING_FOR_GATE_TO_OPEN;
        processStateStartedAtMs = nowMs;
      }
      break;

    case PROCESS_WAITING_FOR_GATE_TO_OPEN:
      if (nowMs - processStateStartedAtMs >= MIXER_GATE_TRAVEL_MS) {
        automaticProcessState = PROCESS_RELEASING_MATERIAL;
        processStateStartedAtMs = nowMs;
        USB_LOG.println(F("STEP: releasing material into the closed molder"));
        emitEvent(F("MATERIAL_RELEASE_STARTED"), F("info"), F("process"),
                  F("running"), F("Releasing material into molder"));
      }
      break;

    case PROCESS_RELEASING_MATERIAL:
      if (nowMs - processStateStartedAtMs >= PROCESS_MATERIAL_RELEASE_MS) {
        USB_LOG.println(F("STEP: closing mixer gate before pressing"));
        setMixerGateAngle(MIXER_GATE_CLOSED_ANGLE, F("CLOSED"));
        automaticProcessState = PROCESS_WAITING_FOR_MIXER_GATE_TO_CLOSE;
        processStateStartedAtMs = nowMs;
      }
      break;

    case PROCESS_WAITING_FOR_MIXER_GATE_TO_CLOSE:
      if (nowMs - processStateStartedAtMs >= MIXER_GATE_TRAVEL_MS) {
        USB_LOG.println(F("STEP: presser extending"));
        emitEvent(F("PRESSING_STARTED"), F("info"), F("process"),
                  F("running"), F("Pressing sequence started"));
        requestPulse(PRESSER_EXTEND, PROCESS_PRESSER_EXTEND_MS);
        automaticProcessState = PROCESS_PRESSER_EXTENDING;
      }
      break;

    case PROCESS_PRESSER_EXTENDING:
      if (runState == STATE_IDLE) {
        USB_LOG.println(F("STEP: presser extension complete; retracting"));
        requestPulse(PRESSER_RETRACT, PROCESS_PRESSER_RETRACT_MS);
        automaticProcessState = PROCESS_PRESSER_RETRACTING;
      }
      break;

    case PROCESS_PRESSER_RETRACTING:
      if (runState == STATE_IDLE) {
        USB_LOG.println(F("Presser retracted; waiting before opening molder"));
        automaticProcessState = PROCESS_WAITING_TO_OPEN_MOLDER;
        processStateStartedAtMs = nowMs;
      }
      break;

    case PROCESS_WAITING_TO_OPEN_MOLDER:
      if (nowMs - lastMotorStoppedAtMs >= DIRECTION_CHANGE_PAUSE_MS) {
        USB_LOG.println(F("STEP: opening molder gate for ejection"));
        emitEvent(F("EJECTION_STARTED"), F("info"), F("process"),
                  F("running"), F("Opening molder for ejection"));
        setMolderAngle(MOLDER_OPEN_ANGLE, F("OPEN"));
        automaticProcessState = PROCESS_WAITING_FOR_MOLDER_TO_OPEN;
        processStateStartedAtMs = nowMs;
      }
      break;

    case PROCESS_WAITING_FOR_MOLDER_TO_OPEN:
      if (nowMs - processStateStartedAtMs >= MOLDER_TRAVEL_MS) {
        USB_LOG.println(F("STEP: molder open; pusher extending"));
        requestPulse(PUSHER_FORWARD, PROCESS_PUSHER_EXTEND_MS);
        automaticProcessState = PROCESS_PUSHER_EXTENDING;
      }
      break;

    case PROCESS_PUSHER_EXTENDING:
      if (runState == STATE_IDLE) {
        USB_LOG.println(F("STEP: pusher extension complete; retracting"));
        requestPulse(PUSHER_REVERSE, PROCESS_PUSHER_RETRACT_MS);
        automaticProcessState = PROCESS_PUSHER_RETRACTING;
      }
      break;

    case PROCESS_PUSHER_RETRACTING:
      if (runState == STATE_IDLE) {
        USB_LOG.println(F("Pusher retracted; waiting before closing molder"));
        automaticProcessState = PROCESS_WAITING_TO_CLOSE_MOLDER;
        processStateStartedAtMs = nowMs;
      }
      break;

    case PROCESS_WAITING_TO_CLOSE_MOLDER:
      if (nowMs - lastMotorStoppedAtMs >= DIRECTION_CHANGE_PAUSE_MS) {
        USB_LOG.println(F("STEP: closing molder gate"));
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
        USB_LOG.println(F("PROCESS COMPLETE: mixer and molder gates closed"));
        emitEvent(F("PROCESS_COMPLETED"), F("info"), F("process"),
                  F("stopped"), F("Forming finished; load chamber before drying"));
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
    USB_LOG.println(F("FAN: OFF command sent"));
  if (statusIoOnline && wasUnlockOn && !chamber.unlockOn)
    USB_LOG.println(F("UNLOCK: OFF command sent; pulse ended"));
}

bool chamberMotionReady() {
  if (thermal.heating(production)) return false;
  if (heaterTestActive || pumpTestActive) return false;
  serviceChamber(true);
  if (chamber.ready()) return true;
  if (!chamber.online) {
    USB_LOG.println(F("ACTION BLOCKED: door/status board 0x24 not responding"));
  } else if (!chamber.doorClosed) {
    USB_LOG.println(F("ACTION BLOCKED: door interlock P2 is OPEN; close door, send I, then Reset"));
  } else if (chamber.unlockOn) {
    USB_LOG.println(F("ACTION BLOCKED: unlock pulse active; wait, then Reset"));
  } else if (chamber.fanOn) {
    USB_LOG.println(F("ACTION BLOCKED: fan test active; send N to end test, then Reset"));
  }
  return false;
}

void printChamberStatus() {
  USB_LOG.print(F("CHAMBER 0x24: door P2="));
  USB_LOG.print(!chamber.online ? F("UNKNOWN") : chamber.doorClosed ? F("CLOSED") : F("OPEN"));
  USB_LOG.print(F("; raw P2="));
  USB_LOG.print(!chamber.online ? F("UNKNOWN") :
      (lastStatusInputs & ChamberControl::DOOR) ? F("HIGH") : F("LOW"));
  USB_LOG.print(F("; fan P5=")); USB_LOG.print(chamber.fanOn ? F("ON (command)") : F("OFF (command)"));
  USB_LOG.print(F("; unlock P6=")); USB_LOG.println(chamber.unlockOn ? F("ON (pulse)") : F("OFF"));
  if (chamber.online) {
    USB_LOG.print(F("0x24 raw pins P0..P7: "));
    for (uint8_t pin = 0; pin < 8; ++pin) {
      USB_LOG.print((lastStatusInputs & (1U << pin)) ? '1' : '0');
      if (pin < 7) USB_LOG.print(' ');
    }
    USB_LOG.println();
  }
}

bool runChamberCommand(const char *action) {
  if (heaterTestActive || pumpTestActive) return false;
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
  if (!accepted) { USB_LOG.println(F("CHAMBER REJECTED: stop, wait, and check door/board")); return false; }
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
  if (production.active()) return false;
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
  serviceProduction();
  espLink.print(F("PRO2|")); espLink.print(id);
  espLink.print('|'); espLink.print(PAPERBRIQ_PRODUCTION_PUMP_MAX_SECONDS);
  espLink.print('|'); espLink.print(PAPERBRIQ_PRODUCTION_DRYING_MAX_SECONDS);
  espLink.print('|'); espLink.print(PAPERBRIQ_PUMP_VERIFIED ? 1 : 0);
  espLink.print('|'); espLink.print(PAPERBRIQ_HEATER_VERIFIED ? 1 : 0);
  espLink.print('|'); espLink.print(production.kind == 'P' ? 1 : production.kind == 'D' ? 2 : production.kind == 'H' ? 3 : 0);
  espLink.print('|'); espLink.print((uint8_t)production.phase);
  espLink.print('|'); espLink.print(production.run);
  espLink.print('|'); espLink.print(production.total);
  espLink.print('|'); espLink.print(production.elapsed(millis()));
  espLink.print('|'); espLink.print(production.remaining(millis()));
  espLink.print('|'); espLink.print(PAPERBRIQ_PREHEAT_MAX_SECONDS);
  espLink.print('|'); espLink.print(PAPERBRIQ_CHAMBER_TARGET_C);
  espLink.print('|'); espLink.print(heaterReadingValid && millis() - heaterReadingAt < 5000UL ? 1 : 0);
  espLink.print('|'); espLink.print((uint8_t)thermal.stage);
  espLink.print('|'); espLink.println(PAPERBRIQ_HEATER_FAN_CONFIRMED ? 1 : 0);
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
  espLink.print('|'); espLink.print(diagnosticActive ? 1 : 0);
  espLink.print('|'); espLink.print(PAPERBRIQ_STANDALONE_BENCH ? 1 : 0);
  espLink.print('|'); espLink.print(heaterTestActive ? 1 : 0);
  espLink.print('|'); espLink.print(pumpTestActive ? 1 : 0);
  const bool idle = benchConditionsReady() && !production.active() && !heaterTestActive && !pumpTestActive;
  espLink.print('|'); espLink.print(idle && (!heaterBenchUsed || millis() - heaterTestAt >= 60000UL) ? 1 : 0);
  espLink.print('|'); espLink.println(idle && (!pumpHasRun || millis() - pumpTestAt >= 10000UL) ? 1 : 0);
}

void handleRemoteCommandFrame(char *line) {
  // Mirror the received frame before strtok modifies it, so USB logs can
  // distinguish a missing incoming command from a missing outgoing reply.
  USB_LOG.print(F("ESP32 -> Uno: "));
  USB_LOG.println(line);
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

  char output; bool edit; uint32_t run, productionSeconds;
  if (parseProductionCommand(action, output, edit, run, productionSeconds)) {
    serviceChamber(true); serviceControlButtons(); serviceProduction();
    const uint32_t maximum = output == 'P' ? PAPERBRIQ_PRODUCTION_PUMP_MAX_SECONDS :
        output == 'H' ? PAPERBRIQ_PREHEAT_MAX_SECONDS : PAPERBRIQ_PRODUCTION_DRYING_MAX_SECONDS;
    const bool verified = output == 'P' ? PAPERBRIQ_PUMP_VERIFIED :
        PAPERBRIQ_HEATER_VERIFIED && PAPERBRIQ_HEATER_FAN_CONFIRMED &&
        PAPERBRIQ_CHAMBER_TARGET_C >= 30 && PAPERBRIQ_CHAMBER_TARGET_C <= 50 &&
        heaterReadingValid && millis() - heaterReadingAt < 5000UL &&
        heaterTemperature < PAPERBRIQ_CHAMBER_TARGET_C + 5;
    bool accepted = false;
    if (edit) accepted = verified && production.adjust(output, run, productionSeconds, maximum, millis());
    else {
      accepted = production.start(output, run, productionSeconds, maximum, verified,
          benchConditionsReady() && !pumpTestActive && !heaterTestActive &&
          millis() - lastMotorStoppedAtMs >= 2500UL && millis() - lastGateCommandAtMs >= 2500UL, millis());
      if (accepted) paperClearSeen = false;
      if (accepted && output != 'P') thermal.begin(production, millis());
    }
    serviceProduction();
    if (accepted) { emitAck(id, "COMPLETED", "production_applied"); rememberRemoteResult(id, "COMPLETED", "production_applied"); }
    else failRemoteCommand(id, "production_blocked");
    return;
  }

  const uint8_t seconds = timedCommandSeconds(action);
  if (seconds && PAPERBRIQ_STANDALONE_BENCH) {
    if (heaterTestActive || pumpTestActive) { failRemoteCommand(id, "output_busy"); return; }
    const bool heater = action[0] == 'H';
    if (heater) { heaterDurationMs = seconds * 1000UL; startHeaterTest(); }
    else { pumpDurationMs = seconds * 1000UL; startPumpTest(); }
    if (heater ? heaterTestActive : pumpTestActive) {
      emitAck(id, "COMPLETED", "timed_run_started");
      rememberRemoteResult(id, "COMPLETED", "timed_run_started");
    } else failRemoteCommand(id, "timed_run_blocked");
    return;
  }

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
    if (armController()) {
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
  USB_LOG.print(F("UART RX partial (")); USB_LOG.print(static_cast<unsigned int>(partialLength));
  USB_LOG.print(F(" bytes): ["));
  for (uint8_t i = 0; i < partialLength; ++i) {
    const uint8_t c = static_cast<uint8_t>(bridgeLine[i]);
    USB_LOG.print(c >= 32 && c <= 126 ? static_cast<char>(c) : '?');
  }
  USB_LOG.println(F("]"));
  emitEvent(F("FAULT"), F("error"), F("controller"), F("fault"), message);
}

void requestDhtCheck() {
  if (dhtCheckPending) { USB_LOG.println(F("DHT22: waiting for ESP32 reply")); return; }
  if (!thermal.heating(production)) heaterReadingValid = false;
  dhtCheckPending = true;
  dhtCheckSentAt = millis();
  ++dhtCheckSequence;
  espLink.print(F("DHT_CHECK|")); espLink.println(dhtCheckSequence);
  USB_LOG.println(F("Reading chamber DHT22 on ESP32 GPIO27..."));
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
    heaterReadingValid = false;
    dhtCheckPending = false;
    USB_LOG.println(F("DHT22: no fresh valid reading. Check ESP32 sensor wiring; try S again."));
    return;
  }
  if (strcmp(status, "OK") != 0 || !temperature || !humidity || extra) return;
  const double t = strtod(temperature, &end);
  if (*end || !isfinite(t) || t < -40 || t > 80) return;
  const double h = strtod(humidity, &end);
  if (*end || !isfinite(h) || h < 0 || h > 100) return;
  heaterTemperature = t;
  heaterReadingAt = millis();
  heaterReadingValid = true;
  dhtCheckPending = false;
  USB_LOG.print(F("Chamber DHT22: ")); USB_LOG.print(temperature);
  USB_LOG.print(F(" C, ")); USB_LOG.print(humidity); USB_LOG.println(F(" %RH (ESP32 reading)"));
}

void serviceBridgeCommands() {
  while (espLink.available() > 0) {
    const char c = static_cast<char>(espLink.read());
    bridgeLastByteAtMs = millis();
    if (c == '\n') {
      bridgeLine[bridgeLineLength] = '\0';
      if (bridgeLineLength == 4 && strncmp(bridgeLine, "IR|", 3) == 0 &&
          (bridgeLine[3] == '0' || bridgeLine[3] == '1')) handlePaperSignal(bridgeLine[3]);
      else if (strncmp(bridgeLine, "DHT|", 4) == 0) handleDhtReply(bridgeLine);
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
    USB_LOG.println(F("DHT22: ESP32 reply timed out. Update ESP32 firmware and check UART link."));
  }
}

bool benchConditionsReady() {
  return softwareStopLatched && !diagnosticActive && !statusSelfTestActive &&
      runState == STATE_IDLE && !automaticProcessIsActive() &&
      statusIoOnline && chamber.ready() && controlIoOnline &&
      !stopButton.rawActive && !stopButton.stableActive &&
      !emergencyButton.rawActive && !emergencyButton.stableActive;
}

void serviceChamberTemperature() {
  // Poll without invalidating the last sample while a reply is in flight.
  // A missing/error reply still expires locally; HTTP delays cannot keep heat on.
  if (!PAPERBRIQ_HEATER_FAN_CONFIRMED || heaterTestActive || dhtCheckPending ||
      millis() - dhtCheckSentAt < 2500UL) return;
  dhtCheckPending = true;
  dhtCheckSentAt = millis();
  ++dhtCheckSequence;
  espLink.print(F("DHT_CHECK|")); espLink.println(dhtCheckSequence);
}

void serviceProduction() {
  if (!production.active()) {
    if (productionPumpWasOn && production.phase == ProductionTimer::FINISHED &&
        automaticProcessState == PROCESS_MIXER_FINISHING) processStateStartedAtMs = millis();
    productionPumpWasOn = false;
    if (production.run) {
      if (!pumpTestActive) digitalWrite(PUMP_ENABLE_PIN, LOW);
      if (!heaterTestActive) digitalWrite(HEATER_ENABLE_PIN, LOW);
    }
    if (thermal.stage != ThermalCycle::FAULT) thermal.stage = ThermalCycle::IDLE;
    return;
  }
  const bool safe = chamber.ready() && statusIoOnline && controlIoOnline &&
      !stopButton.rawActive && !stopButton.stableActive &&
      !emergencyButton.rawActive && !emergencyButton.stableActive &&
      !diagnosticActive && !statusSelfTestActive && !pumpTestActive && !heaterTestActive;
  const bool mixing = automaticProcessState == PROCESS_SHREDDING_AND_MIXING ||
      automaticProcessState == PROCESS_MIXER_FINISHING;
  if (production.kind == 'P') {
    production.tick(safe && (!production.on() || (mixing && !softwareStopLatched)), millis());
  } else {
    thermal.tick(production, safe && PAPERBRIQ_HEATER_FAN_CONFIRMED &&
        softwareStopLatched && !automaticProcessIsActive() && runState == STATE_IDLE,
        heaterReadingValid && millis() - heaterReadingAt < 5000UL, heaterTemperature,
        PAPERBRIQ_CHAMBER_TARGET_C, PAPERBRIQ_PREHEAT_MAX_SECONDS * 1000UL, millis());
  }
  if (productionPumpWasOn && production.phase == ProductionTimer::FINISHED &&
      automaticProcessState == PROCESS_MIXER_FINISHING) processStateStartedAtMs = millis();
  productionPumpWasOn = production.kind == 'P' && production.on();
  digitalWrite(PUMP_ENABLE_PIN, production.kind == 'P' && production.on() ? HIGH : LOW);
  digitalWrite(HEATER_ENABLE_PIN, thermal.heating(production) ? HIGH : LOW);
}

void handlePaperSignal(char value) {
  const unsigned long now = millis();
  const bool edge = value == '1' && paperClearSeen && now - paperSignalAt < 1500UL;
  // Check the old heartbeat before accepting a new one, so a delayed frame
  // cannot hide a sensor-link outage and silently resume feeding.
  serviceAutomaticProcess();
  paperDetected = value == '1';
  paperClearSeen = value == '0'; paperSignalAt = now;
  if (edge && automaticProcessState == PROCESS_WAITING_FOR_PAPER &&
      !softwareStopLatched && chamberMotionReady() && controlIoOnline &&
      !stopButton.rawActive && !stopButton.stableActive &&
      !emergencyButton.rawActive && !emergencyButton.stableActive &&
      production.kind == 'P' && production.phase == ProductionTimer::WAITING) startPaperProcessing();
  production.detectPaper(edge, automaticProcessState == PROCESS_SHREDDING_AND_MIXING ||
      automaticProcessState == PROCESS_MIXER_FINISHING, now);
  serviceProduction();
  serviceAutomaticProcess();
}

bool heaterConditionsReady() {
  return !pumpTestActive && benchConditionsReady() &&
      (PAPERBRIQ_STANDALONE_BENCH ?
        (!heaterReadingValid || heaterTemperature < 40.0f) :
        (heaterReadingValid && millis() - heaterReadingAt < 5000UL && heaterTemperature < 40.0f));
}

void stopPumpTest() {
  digitalWrite(PUMP_ENABLE_PIN, LOW);
  if (pumpTestActive) USB_LOG.println(F("PUMP: D3 OFF"));
  pumpTestActive = false;
}

void servicePumpTest() {
  if (pumpTestActive && (millis() - pumpTestAt >= pumpDurationMs ||
      heaterTestActive || !benchConditionsReady())) stopPumpTest();
}

void startPumpTest() {
  if (production.active()) return;
  if (!PAPERBRIQ_PUMP_VERIFIED && !PAPERBRIQ_STANDALONE_BENCH) {
    USB_LOG.println(F("PUMP BLOCKED: wiring/suppression verification pending")); return;
  }
  if (pumpTestActive) return;
  serviceChamber(true);
  serviceControlButtons();
  if (heaterTestActive || !benchConditionsReady() ||
      millis() - lastMotorStoppedAtMs < 2500UL ||
      millis() - lastGateCommandAtMs < 2500UL ||
      (pumpHasRun && millis() - pumpTestAt < 10000UL)) {
    USB_LOG.println(F("PUMP BLOCKED: stop, close door, check contacts; wait 10s between starts")); return;
  }
  pumpTestAt = millis(); pumpHasRun = true; pumpTestActive = true;
  digitalWrite(PUMP_ENABLE_PIN, HIGH);
  USB_LOG.print(F("PUMP: D3 ON, milliseconds=")); USB_LOG.println(pumpDurationMs);
}

void stopHeaterTest() {
  digitalWrite(HEATER_ENABLE_PIN, LOW);
  if (heaterTestActive) {
    heaterReadingValid = false;
    USB_LOG.println(F("HEATER: D4 OFF"));
  }
  heaterTestActive = false;
}

void serviceHeaterTest() {
  if (heaterTestActive && (millis() - heaterTestAt >= heaterDurationMs || !heaterConditionsReady())) {
    stopHeaterTest();
    // Consume the reading: another test requires another successful S reply.
    heaterReadingValid = false;
  }
}

void startHeaterTest() {
  if (production.active()) return;
  if (!PAPERBRIQ_HEATER_VERIFIED && !PAPERBRIQ_STANDALONE_BENCH) {
    USB_LOG.println(F("HEATER BLOCKED: wiring/cutoff verification pending")); return;
  }
  if (heaterTestActive) return; // Repeated J never extends the pulse.
  if (PAPERBRIQ_STANDALONE_BENCH && heaterBenchUsed && millis() - heaterTestAt < 60000UL) {
    USB_LOG.println(F("HEATER: wait 60s between bench starts")); return;
  }
  serviceChamber(true);
  serviceControlButtons();
  if (!heaterConditionsReady() || millis() - lastMotorStoppedAtMs < 2500UL ||
      millis() - lastGateCommandAtMs < 2500UL) {
    USB_LOG.println(F("HEATER BLOCKED: stop/door/contacts/temperature")); return;
  }
  heaterTestAt = millis();
  heaterBenchUsed = true;
  heaterTestActive = true;
  digitalWrite(HEATER_ENABLE_PIN, HIGH);
  USB_LOG.print(F("HEATER: D4 ON, milliseconds=")); USB_LOG.println(heaterDurationMs);
}

void serviceSerialCommands() {
  while (Serial.available() > 0) {
    const char command = Serial.read();
    switch (command) {
      case 's': case 'S': requestDhtCheck(); break;
      case 'i': case 'I': serviceChamber(true); printChamberStatus(); break;
      case 'q': case 'Q':
        if (!startComponentTest()) Serial.println(F("CHECK REJECTED: close door, Reset, stop other actions"));
        break;
      case 'u': case 'U': runChamberCommand("UNLOCK"); break;
      case 'k': case 'K': runChamberCommand("LOCK"); break;
      case 'p': case 'P': startAutomaticProcess(); break;
      case 'x': case 'X': latchSoftwareStop(); return;
      case 'r': case 'R': armController(); break;
      case 'h': case 'H': case '?': printHelp(); break;
      case '\r': case '\n': case ' ': case '\t': break;
      default:
        Serial.println(F("Unsupported production command. Send H for help."));
        break;
    }
  }
}

void setup() {
  // Load LOW before switching each output to OUTPUT to reduce startup glitches.
  digitalWrite(HEATER_ENABLE_PIN, LOW);
  pinMode(HEATER_ENABLE_PIN, OUTPUT);
  digitalWrite(PUMP_ENABLE_PIN, LOW);
  pinMode(PUMP_ENABLE_PIN, OUTPUT);
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
  USB_LOG.println(F("Uno link diagnostic: RX=A2, TX=A3, baud=9600"));
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
  serviceChamberTemperature();
  serviceProduction();
  servicePumpTest();
  serviceHeaterTest();
  serviceChamber();
  serviceControlButtons();
  servicePumpTest();
  serviceHeaterTest();
  serviceStatusLights();
  serviceBridgeCommands();
  serviceMotorState();
  serviceComponentTest();
  serviceAutomaticProcess();
  serviceSerialCommands();
  serviceProduction();
  serviceChamber();
  serviceControlButtons();
  servicePumpTest();
  serviceHeaterTest();
  serviceStatusLights();
  serviceBridgeCommands();
  serviceMotorState();
  serviceComponentTest();
  serviceAutomaticProcess();
}
