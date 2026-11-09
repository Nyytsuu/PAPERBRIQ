#include <Arduino.h>
#include <PWMServo.h>
#include <NeoSWSerial.h>
#include <Wire.h>
#include "ChamberControl.h"
#include "TimedCommands.h"
#include "ProductionTimer.h"
#include "ThermalCycle.h"
#include "ProductionLimits.h"
#include "StandaloneCycle.h"
#include "BatchMixer.h"
#include <stdlib.h>
#include <math.h>

// Reuse common flash strings instead of storing each F() occurrence separately.
const char PB_TEXT_info[] PROGMEM = "info";
const char PB_TEXT_running[] PROGMEM = "running";
const char PB_TEXT_CLOSED[] PROGMEM = "CLOSED";
const char PB_TEXT_stopped[] PROGMEM = "stopped";
const char PB_TEXT_process[] PROGMEM = "process";
const char PB_TEXT_OPEN[] PROGMEM = "OPEN";
const char PB_TEXT_shredder[] PROGMEM = "shredder";
const char PB_TEXT_controller[] PROGMEM = "controller";
const char PB_TEXT_FAULT[] PROGMEM = "FAULT";
const char PB_TEXT_error[] PROGMEM = "error";
const char PB_TEXT_fault[] PROGMEM = "fault";
const char PB_TEXT_mixer[] PROGMEM = "mixer";
const char PB_TEXT_diagnostics[] PROGMEM = "diagnostics";
const char PB_TEXT_SHREDDER_STARTED[] PROGMEM = "SHREDDER_STARTED";
const char PB_TEXT_presser[] PROGMEM = "presser";
const char PB_TEXT_pusher[] PROGMEM = "pusher";

const char PB_SHARED_15[] PROGMEM = "STOPPED";
const char PB_SHARED_20[] PROGMEM = " at ";
const char PB_SHARED_28[] PROGMEM = " ms for motor coast-down";
const char PB_SHARED_34[] PROGMEM = "warning";
const char PB_SHARED_69[] PROGMEM = "SHREDDER_COMMAND_RECEIVED";
const char PB_SHARED_70[] PROGMEM = "waiting";
const char PB_SHARED_76[] PROGMEM = " ms";
const char PB_SHARED_86[] PROGMEM = "SHREDDER_COMPLETED";
const char PB_SHARED_88[] PROGMEM = "MIXER_COMPLETED";
const char PB_SHARED_121[] PROGMEM = "SHREDDER_MIXER_STARTED";
const char PB_SHARED_124[] PROGMEM = "Shred ON";
const char PB_SHARED_163[] PROGMEM = "door";
const char PB_SHARED_171[] PROGMEM = "UNKNOWN";
const char PB_SHARED_192[] PROGMEM = "TEST OPEN";
const char PB_SHARED_193[] PROGMEM = "TEST CLOSED";
const char PB_SHARED_207[] PROGMEM = "DHT_CHECK|";
const char PB_SHARED_247[] PROGMEM = "Local mode; UART off";

const char PB_BLOCKED[] PROGMEM = "Blocked: R/door/IO/busy";

void serviceChamber(bool force = false);
bool chamberMotionReady(bool localSession = false);
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
bool benchConditionsReady(bool allowArmedPump = false);
void serviceProduction();
void serviceStandaloneCycle();
bool startStandaloneCycle(bool fillOnly = false, uint8_t firstLoad = 0);
void handlePaperSignal(char value);

ProductionTimer production;
ThermalCycle thermal;
StandaloneCycle localCycle;
BatchMixer batchMixer;
uint8_t positioningStep = 0; // Z advances only on an explicit USB command.

bool paperClearSeen = false;
bool paperDetected = false;
bool sensorControlledProcess = false;
bool irShredTest = false;
bool paperOutletDetected = false, haveDualPaper = false, paperExtensionActive = false, paperBothClear = false;
unsigned long dualPaperAt = 0, lastPaperOccupiedAt = 0, paperExtensionAt = 0;
const unsigned long PAPER_CLEAR_MS = 5000UL;
bool productionPumpWasOn = false;
unsigned long paperSignalAt = 0;

// ============================================================
// BENCH / LOG SETTINGS
// ============================================================

#ifndef PAPERBRIQ_STANDALONE_BENCH
#define PAPERBRIQ_STANDALONE_BENCH 0
#endif

// Enable explicitly when resuming ESP32 integration.
#ifndef PAPERBRIQ_ESP_LINK_ENABLED
#define PAPERBRIQ_ESP_LINK_ENABLED 1
#endif

#ifndef PAPERBRIQ_FILL_IR_ENABLED
#define PAPERBRIQ_FILL_IR_ENABLED 0
#endif

// Full verbose output exceeds Uno flash with the current process. UART RX
// commands, rejected frames and partial-frame faults always print to USB.
#ifndef PAPERBRIQ_VERBOSE_USB_LOGS
#define PAPERBRIQ_VERBOSE_USB_LOGS PAPERBRIQ_STANDALONE_BENCH
#endif

#if PAPERBRIQ_VERBOSE_USB_LOGS
#define USB_LOG Serial
#else
struct QuietUsbLog {
  template<class T>
  void print(const T &) {}

  template<class T>
  void println(const T &) {}

  void println() {}
} USB_LOG;
#endif

const unsigned long HEATER_TEST_PULSE_MS =
    PAPERBRIQ_STANDALONE_BENCH ? 10000UL : 3000UL;

bool heaterBenchUsed = false;

// ============================================================
// PUMP
// ============================================================

#ifndef PAPERBRIQ_PUMP_VERIFIED
#define PAPERBRIQ_PUMP_VERIFIED 1
#endif

const uint8_t PUMP_ENABLE_PIN = 3;

bool pumpTestActive = false;
bool pumpHasRun = false;

unsigned long pumpTestAt = 0;

// FIXED: Y pump test is 15 seconds.
unsigned long pumpDurationMs = 15000UL;

unsigned long heaterDurationMs = HEATER_TEST_PULSE_MS;

// ============================================================
// HEATER
// ============================================================

#ifndef PAPERBRIQ_HEATER_VERIFIED
#define PAPERBRIQ_HEATER_VERIFIED 1
#endif

const uint8_t HEATER_ENABLE_PIN = 4;

bool heaterTestActive = false;
bool heaterReadingValid = false;

unsigned long heaterTestAt = 0;
unsigned long heaterReadingAt = 0;

float heaterTemperature = 0;

// ============================================================
// MOTOR PINS
// ============================================================

// BTS7960 - Shredder
const uint8_t SHREDDER_RPWM = 5;
const uint8_t SHREDDER_LPWM = 6;

// BTS7960 - Presser
const uint8_t PRESSER_RPWM = 7;
const uint8_t PRESSER_LPWM = 8;

// BTS7960 - Mixer
const uint8_t MIXER_RPWM = A0;
const uint8_t MIXER_LPWM = A1;
// false = manual 4 (A1); true = manual 3 (A0).
// Change only to the direction verified on the installed mixer.
const bool PROCESS_MIXER_USE_RPWM = true;

// L298N - Pusher
const uint8_t PUSHER_IN3 = 11;
const uint8_t PUSHER_IN4 = 12;

// ============================================================
// ESP32 UART
// ============================================================

const uint8_t ESP_LINK_RX = A2;
const uint8_t ESP_LINK_TX = A3;

// Active-low digital paper sensors for M with the ESP32 link disabled.
const uint8_t FILL_IR_IN_PIN = 2;
const uint8_t FILL_IR_OUT_PIN = A2;
const unsigned long FILL_IR_CLEAR_MS = 2000UL;
const unsigned long FILL_SHREDDER_MAX_MS = 60000UL;
unsigned long fillLastPaperAtMs = 0;

const unsigned long ESP_LINK_BAUD = 9600;

NeoSWSerial espLink(ESP_LINK_RX, ESP_LINK_TX);

// ============================================================
// PCF8574 CONTROL BUTTONS
// ============================================================

const uint8_t CONTROL_IO_ADDRESS = 0x20;

const uint8_t START_BUTTON_MASK = _BV(0);
const uint8_t RESET_BUTTON_MASK = _BV(1);
const uint8_t STOP_BUTTON_MASK = _BV(2);
const uint8_t EMERGENCY_BUTTON_MASK = _BV(3);

const unsigned long BUTTON_POLL_MS = 10;
const unsigned long BUTTON_DEBOUNCE_MS = 50;

// ============================================================
// PCF8574 STATUS / DOOR
// ============================================================

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

ChamberControl chamber;

unsigned long lastDoorPollAtMs = 0;
uint8_t lastStatusInputs = 0xFF;
unsigned long lastGateCommandAtMs = 0;

// ============================================================
// SERVOS
// ============================================================

const uint8_t MIXER_GATE_SERVO_PIN = 9;
const uint8_t MOLDER_SERVO_PIN = 10;

const uint8_t MIXER_GATE_CLOSED_ANGLE = 34;
const uint8_t MIXER_GATE_OPEN_ANGLE = 130;

const uint8_t MOLDER_CLOSED_ANGLE = 120;
const uint8_t MOLDER_OPEN_ANGLE = 0;

// ============================================================
// MOTOR SPEED
// ============================================================

const uint8_t SHREDDER_FORWARD_DUTY = 189; // 60% PWM; was 102 (40%).
const uint8_t SHREDDER_REVERSE_DUTY = 77;

const uint8_t MIXER_FORWARD_DUTY = 102;
const uint8_t MIXER_REVERSE_DUTY = 77;

// ============================================================
// MANUAL MOTOR TIMES
// ============================================================

const unsigned long SHREDDER_PULSE_MS = 16000;
const unsigned long MIXER_PULSE_MS = 15000;
const unsigned long PRESSER_PULSE_MS = 15000;
const unsigned long PUSHER_PULSE_MS = 23000;

// ============================================================
// AUTOMATIC PROCESS TIMES
// ============================================================

// W / physical Start: start water and timed shredding after gate closure.
// 1000 ms = 1 second. Measure delivered water; this is not a calibrated dose.
const uint16_t LOCAL_FEED_WATER_MS = 20000;
const uint16_t LOCAL_MIX_WATER_MS = 40000;
static_assert(LOCAL_FEED_WATER_MS > 0 && LOCAL_FEED_WATER_MS <= LOCAL_MIX_WATER_MS &&
              LOCAL_MIX_WATER_MS <= 40000, "Check W pump times");
bool localLoadPaper = false;
bool localMixWater = false;
bool inletClearForDose = false;
unsigned long inletClearedAt = 0;

const unsigned long BENCH_SHREDDER_RUN_MS = 30000;
const unsigned long LOCAL_SHREDDER_LEAD_MS = 10000UL;
const unsigned long BATCH_MIX_FORWARD_MS = 10000UL;
const unsigned long BATCH_MIX_REVERSE_MS = 5000UL;
const unsigned long BATCH_MIX_PAUSE_MS = 2000UL;
const unsigned long FILL_MIX_EXTRA_MS = 5000UL;
const unsigned long FILL_GATE_WAIT_MS = 15000UL;
const unsigned long LOCAL_GATE_WAIT_MS = 10000UL;
const unsigned long BATCH_MIX_TOTAL_MS =
    2UL * (BATCH_MIX_FORWARD_MS + BATCH_MIX_REVERSE_MS) + 3UL * BATCH_MIX_PAUSE_MS;
static_assert(BATCH_MIX_FORWARD_MS > 0 && BATCH_MIX_REVERSE_MS > 0 && BATCH_MIX_PAUSE_MS >= 2000UL,
              "Mixer needs positive run times and >=2s reversal pauses");
const unsigned long PROCESS_MIXER_FINISH_MS = 15000;

const unsigned long PROCESS_MATERIAL_RELEASE_MS = 40000; // Extra 30s chute water before pressing.

const unsigned long PROCESS_PRESSER_EXTEND_MS = 19000;
// W/physical Start: compact each newly loaded batch into the same molder.
const unsigned long BATCH_PRESS_FIRST_MS = 15000UL;
const unsigned long BATCH_PRESS_SECOND_MS = 13000UL;
const unsigned long BATCH_PRESS_THIRD_MS = 11000UL;
const unsigned long BATCH_PRESS_THIRD_REPEAT_MS = 13000UL;
const unsigned long PROCESS_PRESSER_RETRACT_MS = PAPERBRIQ_PRESSER_RETURN_MS;

const unsigned long PROCESS_PUSHER_EXTEND_MS = PAPERBRIQ_PUSHER_FROM_PARK_MS;
const unsigned long LOCAL_PUSHER_EXTEND_MS = 2 * PROCESS_PUSHER_EXTEND_MS;
const unsigned long PROCESS_PUSHER_RETRACT_MS = PAPERBRIQ_PUSHER_RETURN_TO_PARK_MS;
const unsigned long LOCAL_PUSHER_RETRACT_MS = 3 * PUSHER_PULSE_MS;

const unsigned long MIXER_GATE_TRAVEL_MS = 1500;
const unsigned long MOLDER_TRAVEL_MS = 1500;

const unsigned long PROCESS_CLOSE_TRAVEL_MS =
    MIXER_GATE_TRAVEL_MS > MOLDER_TRAVEL_MS
        ? MIXER_GATE_TRAVEL_MS
        : MOLDER_TRAVEL_MS;

const unsigned long DIRECTION_CHANGE_PAUSE_MS = 2500;
const unsigned long SOFTWARE_STOP_REARM_DELAY_MS = 1000;

const unsigned long FILL_WATER_TIMEOUT_MS =
    BENCH_SHREDDER_RUN_MS +
    BATCH_MIX_FORWARD_MS + BATCH_MIX_TOTAL_MS + FILL_MIX_EXTRA_MS + FILL_GATE_WAIT_MS + 5000UL;

// ============================================================
// ENUMS
// ============================================================

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
  PROCESS_WAITING_FOR_MOLDER_TO_CLOSE,
  PROCESS_WAITING_FOR_NEXT_BATCH
};

// ============================================================
// OBJECTS / STATES
// ============================================================

PWMServo mixerGateServo;
PWMServo molderServo;

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

// ============================================================
// BUTTON STRUCT
// ============================================================

struct DebouncedButton {
  bool initialized;
  bool rawActive;
  bool stableActive;
  unsigned long rawChangedAtMs;
};

DebouncedButton startButton =
    {false, false, false, 0};

DebouncedButton resetButton =
    {false, false, false, 0};

DebouncedButton stopButton =
    {false, true, true, 0};

DebouncedButton emergencyButton =
    {false, true, true, 0};

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

// ============================================================
// UART VARIABLES
// ============================================================

const uint8_t BRIDGE_LINE_CAPACITY = 64;

char bridgeLine[BRIDGE_LINE_CAPACITY];

bool dhtCheckPending = false;

unsigned long dhtCheckSentAt = 0;
unsigned long dhtCheckSequence = 0;

uint8_t bridgeLineLength = 0;
bool discardOverflowedLine = false;
uint8_t statusIoFailCount = 0;
const uint8_t STATUS_IO_FAIL_LIMIT = 5;

unsigned long bridgeLastByteAtMs = 0;

const unsigned long BRIDGE_FRAME_TIMEOUT_MS = 750;

char lastRemoteCommandId[40] = "";
char lastRemoteStatus[10] = "";
char lastRemoteMessage[20] = "";

// ============================================================
// PCF8574 FUNCTIONS
// ============================================================

bool writeControlIo(uint8_t value) {
  Wire.beginTransmission(CONTROL_IO_ADDRESS);
  Wire.write(value);

  return Wire.endTransmission() == 0;
}

bool writeStatusIo(uint8_t value) {
  value = chamber.outputs(value);

  Wire.beginTransmission(STATUS_IO_ADDRESS);
  Wire.write(value);

  if (Wire.endTransmission() != 0) {
    return false;
  }

  statusIoShadow = value;

  return true;
}

bool readControlIo(uint8_t &value) {
  if (Wire.requestFrom(
          CONTROL_IO_ADDRESS,
          static_cast<uint8_t>(1)) != 1) {
    return false;
  }

  value = Wire.read();

  return true;
}

// ============================================================
// BUTTON FUNCTIONS
// ============================================================

void seedButton(
    DebouncedButton &button,
    bool active,
    unsigned long nowMs) {

  button.initialized = true;
  button.rawActive = active;
  button.stableActive = active;
  button.rawChangedAtMs = nowMs;
}

bool updateButton(
    DebouncedButton &button,
    bool active,
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
      nowMs - button.rawChangedAtMs >=
          BUTTON_DEBOUNCE_MS) {

    button.stableActive = button.rawActive;

    return true;
  }

  return false;
}

bool initializeControlIo() {

  uint8_t value = 0xFF;

  if (!writeControlIo(value) ||
      !readControlIo(value)) {

    controlIoOnline = false;

    USB_LOG.println(
        F("CONTROL BUTTONS OFFLINE: PCF8574 not found at 0x20"));

    return false;
  }

  const unsigned long nowMs = millis();

  seedButton(
      startButton,
      (value & START_BUTTON_MASK) == 0,
      nowMs);

  seedButton(
      resetButton,
      (value & RESET_BUTTON_MASK) == 0,
      nowMs);

  seedButton(
      stopButton,
      (value & STOP_BUTTON_MASK) != 0,
      nowMs);

  seedButton(
      emergencyButton,
      (value & EMERGENCY_BUTTON_MASK) != 0,
      nowMs);

  controlIoOnline = true;

  USB_LOG.print(
      F("CONTROL BUTTONS ONLINE at 0x20; stop contact is "));

  USB_LOG.print(
      stopButton.stableActive
          ? reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_OPEN)
          : reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_CLOSED));

  USB_LOG.print(
      F("; emergency contact is "));

  USB_LOG.println(
      emergencyButton.stableActive
          ? reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_OPEN)
          : reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_CLOSED));

  return true;
}

bool initializeStatusIo() {

  chamber.cancel();

  chamber.sample(false, false, millis());

  if (!writeStatusIo(0xFF)) {

    statusIoOnline = false;

    USB_LOG.println(
        F("STATUS LIGHTS OFFLINE: PCF8574 not found at 0x24"));

    return false;
  }

 statusIoOnline = true;
statusIoFailCount = 0;

  USB_LOG.println(
      F("STATUS LIGHTS ONLINE at 0x24"));

  return true;
}

// ============================================================
// EVENTS
// ============================================================

void printEventLine(
    Print &output,
    const __FlashStringHelper *type,
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

void emitEvent(
    const __FlashStringHelper *type,
    const __FlashStringHelper *severity,
    const __FlashStringHelper *component,
    const __FlashStringHelper *processState,
    const __FlashStringHelper *message) {

  printEventLine(
      espLink,
      type,
      severity,
      component,
      processState,
      message);

  printEventLine(
      Serial,
      type,
      severity,
      component,
      processState,
      message);
}

void printAckLine(
    Print &output,
    const char *id,
    const char *status,
    const char *message) {

  output.print(F("ACK|"));

  output.print(id);
  output.print('|');

  output.print(status);
  output.print('|');

  output.println(message);
}

void emitAck(
    const char *id,
    const char *status,
    const char *message) {

  printAckLine(
      espLink,
      id,
      status,
      message);

  printAckLine(
      Serial,
      id,
      status,
      message);
}

void rememberRemoteResult(
    const char *id,
    const char *status,
    const char *message) {

  strncpy(
      lastRemoteCommandId,
      id,
      sizeof(lastRemoteCommandId) - 1);

  lastRemoteCommandId[
      sizeof(lastRemoteCommandId) - 1] = '\0';

  strncpy(
      lastRemoteStatus,
      status,
      sizeof(lastRemoteStatus) - 1);

  lastRemoteStatus[
      sizeof(lastRemoteStatus) - 1] = '\0';

  strncpy(
      lastRemoteMessage,
      message,
      sizeof(lastRemoteMessage) - 1);

  lastRemoteMessage[
      sizeof(lastRemoteMessage) - 1] = '\0';
}

// ============================================================
// MOTOR HELPERS
// ============================================================

uint8_t dutyPercent(uint8_t duty) {

  return static_cast<uint8_t>(
      (static_cast<unsigned int>(duty) * 100U) /
      255U);
}

const __FlashStringHelper *motionName(
    Motion motion) {

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
      return reinterpret_cast<const __FlashStringHelper *>(PB_SHARED_15);
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

  digitalWrite(
      SHREDDER_RPWM,
      LOW);

  digitalWrite(
      SHREDDER_LPWM,
      LOW);

  digitalWrite(
      MIXER_RPWM,
      LOW);

  digitalWrite(
      MIXER_LPWM,
      LOW);

  digitalWrite(
      PRESSER_RPWM,
      LOW);

  digitalWrite(
      PRESSER_LPWM,
      LOW);

  digitalWrite(
      PUSHER_IN3,
      LOW);

  digitalWrite(
      PUSHER_IN4,
      LOW);
}

void stopAllMotors(
    bool printMessage = true) {

  stopMotorOutputs();

  requestedMotion = MOTION_STOPPED;

  activeMotion = MOTION_STOPPED;

  runState = STATE_IDLE;

  requestedRunDurationMs = 0;

  activeRunDurationMs = 0;

  lastMotorStoppedAtMs = millis();

  if (printMessage) {
    USB_LOG.println(
        F("ALL MOTORS: stopped"));
  }
}

bool automaticProcessIsActive() {

  return automaticProcessState !=
      PROCESS_IDLE;
}

// ============================================================
// SERVO FUNCTIONS
// ============================================================

void releaseMixerGateServo(
    bool printMessage = true) {

  if (mixerGateServo.attached()) {
    mixerGateServo.detach();
  }

  pinMode(
      MIXER_GATE_SERVO_PIN,
      OUTPUT);

  digitalWrite(
      MIXER_GATE_SERVO_PIN,
      LOW);

  if (printMessage) {

    USB_LOG.println(
        F("MIXER GATE: servo released; no holding torque"));
  }
}

void releaseMolderServo(
    bool printMessage = true) {

  if (molderServo.attached()) {
    molderServo.detach();
  }

  pinMode(
      MOLDER_SERVO_PIN,
      OUTPUT);

  digitalWrite(
      MOLDER_SERVO_PIN,
      LOW);

  if (printMessage) {

    USB_LOG.println(
        F("MOLDER: servo released; no holding torque"));
  }
}

void __attribute__((noinline)) setMixerGateAngle(
    uint8_t angle,
    const __FlashStringHelper *positionName) {

  if (!chamber.online ||
      !chamber.doorClosed ||
      chamber.unlockOn) {

    return;
  }

  lastGateCommandAtMs = millis();

  const uint8_t safeAngle =
      constrain(angle, 0, 180);

  if (!mixerGateServo.attached()) {
    mixerGateServo.attach(
        MIXER_GATE_SERVO_PIN);
  }

  mixerGateServo.write(safeAngle);

  USB_LOG.print(
      F("GATE "));

  USB_LOG.print(
      positionName);

  USB_LOG.print(
      reinterpret_cast<const __FlashStringHelper *>(PB_SHARED_20));

  USB_LOG.print(
      safeAngle);

  USB_LOG.println(
      F(" deg requested"));
}

void __attribute__((noinline)) setMolderAngle(
    uint8_t angle,
    const __FlashStringHelper *positionName) {

  if (!chamber.online ||
      !chamber.doorClosed ||
      chamber.unlockOn) {

    return;
  }

  lastGateCommandAtMs = millis();

  const uint8_t safeAngle =
      constrain(angle, 0, 180);

  if (!molderServo.attached()) {
    molderServo.attach(
        MOLDER_SERVO_PIN);
  }

  molderServo.write(safeAngle);

  Serial.print(
      F("MOLDER "));

  Serial.print(
      positionName);

  Serial.print(
      reinterpret_cast<const __FlashStringHelper *>(PB_SHARED_20));

  Serial.print(
      safeAngle);

  Serial.println(
      F(" deg requested"));
}

void closeAndHoldBothGates() {

  setMixerGateAngle(
      MIXER_GATE_CLOSED_ANGLE,
      reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_CLOSED));

  setMolderAngle(
      MOLDER_CLOSED_ANGLE,
      reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_CLOSED));
}

void moveMixerGate(
    uint8_t angle,
    const __FlashStringHelper *positionName) {

  if (diagnosticActive) {
    return;
  }

  if (!chamberMotionReady()) {
    return;
  }

  if (softwareStopLatched) {

    USB_LOG.println(
        F("SERVO COMMAND REJECTED: send R to arm first"));

    return;
  }

  if (automaticProcessIsActive()) {

    USB_LOG.println(
        F("SERVO COMMAND REJECTED: automatic process is running"));

    return;
  }

  if (runState != STATE_IDLE) {

    stopAllMotors(false);

    USB_LOG.println(
        F("MIXER GATE: motor stopped; wait 2.5 s, then resend O/C"));

    return;
  }

  const unsigned long stoppedForMs =
      millis() - lastMotorStoppedAtMs;

  if (stoppedForMs <
      DIRECTION_CHANGE_PAUSE_MS) {

    USB_LOG.print(
        F("MIXER GATE: wait another "));

    USB_LOG.print(
        DIRECTION_CHANGE_PAUSE_MS -
        stoppedForMs);

    USB_LOG.println(
        reinterpret_cast<const __FlashStringHelper *>(PB_SHARED_28));

    return;
  }

  setMixerGateAngle(
      angle,
      positionName);
}

void moveMolder(
    uint8_t angle,
    const __FlashStringHelper *positionName) {

  if (diagnosticActive) {
    return;
  }

  if (!chamberMotionReady()) {
    return;
  }

  if (softwareStopLatched) {

    USB_LOG.println(
        F("MOLDER COMMAND REJECTED: send R to arm first"));

    return;
  }

  if (automaticProcessIsActive()) {

    USB_LOG.println(
        F("MOLDER COMMAND REJECTED: automatic process is running"));

    return;
  }

  if (runState != STATE_IDLE) {

    stopAllMotors(false);

    USB_LOG.println(
        F("MOLDER: motor stopped; wait 2.5 s, then resend 5/6"));

    return;
  }

  const unsigned long stoppedForMs =
      millis() - lastMotorStoppedAtMs;

  if (stoppedForMs <
      DIRECTION_CHANGE_PAUSE_MS) {

    USB_LOG.print(
        F("MOLDER: wait another "));

    USB_LOG.print(
        DIRECTION_CHANGE_PAUSE_MS -
        stoppedForMs);

    USB_LOG.println(
        reinterpret_cast<const __FlashStringHelper *>(PB_SHARED_28));

    return;
  }

  setMolderAngle(
      angle,
      positionName);
}

// ============================================================
// SOFTWARE STOP
// ============================================================

void latchSoftwareStop() {
  irShredTest = false;
  const bool wasPositioning = positioningStep != 0;
  positioningStep = 0;

  localCycle.cancel();

  production.stop(
      false,
      millis());

  stopPumpTest();

  stopHeaterTest();
  stopAllMotors(false);

  if (diagnosticActive) {

    diagnosticActive = false;

    diagnosticStepRunning = false;

    emitEvent(
        F("TEST_ABORTED"),
        reinterpret_cast<const __FlashStringHelper *>(PB_SHARED_34),
        reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_diagnostics),
        reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_stopped),
        F("Inspect cancellation"));
  }

  chamber.cancel();

  statusSelfTestActive = false;

  automaticProcessState =
      PROCESS_IDLE;

  softwareStopLatched = true;

  softwareStopLatchedAtMs =
      millis();

  if (chamber.online &&
      chamber.doorClosed && !wasPositioning) {

    closeAndHoldBothGates();
  }

  if (!writeStatusIo(
          statusIoShadow)) {

    statusIoOnline = false;
  }

  USB_LOG.println(
      F("SOFTWARE STOP LATCHED: motors stopped; Reset required"));

  USB_LOG.println(
      F("Inspect the machine, then send R to arm again."));

  emitEvent(
      reinterpret_cast<const __FlashStringHelper *>(PB_SHARED_15),
      reinterpret_cast<const __FlashStringHelper *>(PB_SHARED_34),
      reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_controller),
      reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_stopped),
      F("Stop latched"));
}

const char *lastArmRejectReason =
    "reset_rejected";

// ============================================================
// ARM
// ============================================================

bool armMotorTest() {
  if (positioningStep) return false;

  if (localCycle.active()) {
    return false;
  }

  if (thermal.heating(production)) {
    return false;
  }

  lastArmRejectReason =
      "reset_rejected";

  if (pumpTestActive) {

    lastArmRejectReason =
        "pump_active";

    return false;
  }

  if (heaterTestActive) {

    lastArmRejectReason =
        "heater_active";

    return false;
  }

  if (diagnosticActive) {

    lastArmRejectReason =
        "test_running";

    USB_LOG.println(
        F("ARM REJECTED: component test running; stop it first"));

    return false;
  }

  if (!chamberMotionReady()) {

    lastArmRejectReason =
        !chamber.online
            ? "chamber_offline"
            : !chamber.doorClosed
                  ? "door_open"
                  : chamber.unlockOn
                        ? "unlock_active"
                        : "fan_active";

    return false;
  }

  if (!controlIoOnline) {

    lastArmRejectReason =
        "buttons_offline";

    USB_LOG.println(
        F("ARM REJECTED: control-button expander is offline"));

    return false;
  }

  if (emergencyButton.stableActive) {

    lastArmRejectReason =
        "emergency_open";

    USB_LOG.println(
        F("ARM REJECTED: emergency contact is open"));

    return false;
  }

  if (stopButton.stableActive) {

    lastArmRejectReason =
        "stop_open";

    USB_LOG.println(
        F("ARM REJECTED: physical Stop contact is open"));

    return false;
  }

  if (automaticProcessIsActive()) {

    lastArmRejectReason =
        "process_running";

    USB_LOG.println(
        F("ARM REJECTED: automatic process is already running"));

    return false;
  }

  if (millis() -
          softwareStopLatchedAtMs <
      SOFTWARE_STOP_REARM_DELAY_MS) {

    lastArmRejectReason =
        "reset_too_soon";

    USB_LOG.println(
        F("ARM REJECTED: wait 1 second after X, then send R again"));

    return false;
  }

  stopAllMotors(false);

  softwareStopLatched = false;

  USB_LOG.println(
      F("MOTOR TEST ARMED: motor and gate commands enabled"));

  emitEvent(
      F("ARMED"),
      reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_info),
      reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_controller),
      reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_stopped),
      F("Armed"));

  return true;
}

// ============================================================
// STATUS TEST
// ============================================================

void printStatusSelfTestStep(
    uint8_t step) {

  USB_LOG.print(
      F("STATUS TEST: "));

  switch (step) {

    case 0:
      USB_LOG.println(
          F("green Ready/Check light"));
      break;

    case 1:
      USB_LOG.println(
          F("yellow Preparing/Running light"));
      break;

    case 2:
      USB_LOG.println(
          F("red Stop/Fault light"));
      break;

    case 3:
      USB_LOG.println(
          F("buzzer"));
      break;

    default:
      USB_LOG.println(
          F("all three lights and buzzer"));
      break;
  }
}

void startStatusSelfTest() {
  if (positioningStep) return;

  if (localCycle.active()) {
    return;
  }

  if (diagnosticActive) {
    return;
  }

  if (!chamberMotionReady()) {
    return;
  }

  if (!statusIoOnline) {

    USB_LOG.println(
        F("STATUS TEST REJECTED: status-light expander is offline"));

    return;
  }

  if (!controlIoOnline ||
      stopButton.stableActive ||
      emergencyButton.stableActive) {

    USB_LOG.println(
        F("STATUS TEST REJECTED: clear Stop/Emergency faults first"));

    return;
  }

  if (automaticProcessIsActive() ||
      runState != STATE_IDLE) {

    USB_LOG.println(
        F("STATUS TEST REJECTED: wait until all movement has stopped"));

    return;
  }

  if (!writeStatusIo(0xFF)) {

    statusIoOnline = false;

    USB_LOG.println(
        F("STATUS TEST REJECTED: status-light expander did not respond"));

    return;
  }

  statusSelfTestActive = true;

  statusSelfTestStep = 0;

  statusSelfTestStepStartedAtMs =
      millis();

  USB_LOG.println(
      F("STATUS TEST STARTED: no motors will move"));

  printStatusSelfTestStep(
      statusSelfTestStep);
}

// ============================================================
// HELP
// ============================================================

void printHelp() {
  Serial.println(F(
      "PWM9/10 R/X=arm/stop W/M/G=batch/fill/dry P=ESP Z=pos\r\n"
      "1/2=shred 3/4=mix E/T=press F/B=push\r\n"
      "O/C/D=gate 5/6/7=molder I=door\r\n"
      "V/N=fan U/K=lock J=heat Y=pump S=DHT Q=test L=LED"));

  Serial.print(F("Y pump ms="));
  Serial.println(pumpDurationMs);
}

// ============================================================
// MOTOR REQUEST
// ============================================================

void requestPulse(
    Motion motion,
    unsigned long runDurationMs) {

  if (!chamberMotionReady()) {
    return;
  }

  if (softwareStopLatched) {

    USB_LOG.println(
        F("COMMAND REJECTED: send R to arm first"));

    return;
  }

  stopMotorOutputs();

  activeMotion = MOTION_STOPPED;

  requestedMotion = motion;

  requestedRunDurationMs =
      runDurationMs;

  runState = STATE_WAITING;

  stateStartedAtMs =
      millis();

  USB_LOG.print(
      motionName(motion));

  USB_LOG.print(
      F(": requested at "));

  USB_LOG.print(
      dutyPercent(
          motionDuty(motion)));

  USB_LOG.println(
      F("% PWM; waiting for safety pause"));

  // Shredder command confirmation.
  if (motion == SHREDDER_FORWARD) {

    emitEvent(
        reinterpret_cast<const __FlashStringHelper *>(PB_SHARED_69),
        reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_info),
        reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_shredder),
        reinterpret_cast<const __FlashStringHelper *>(PB_SHARED_70),
        F("Forward"));

  } else if (motion ==
             SHREDDER_REVERSE) {

    emitEvent(
        reinterpret_cast<const __FlashStringHelper *>(PB_SHARED_69),
        reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_info),
        reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_shredder),
        reinterpret_cast<const __FlashStringHelper *>(PB_SHARED_70),
        F("Reverse"));
  }
}

void requestManualPulse(
    Motion motion) {

  if (diagnosticActive) {
    return;
  }

  if (statusSelfTestActive) {

    USB_LOG.println(
        F("MANUAL COMMAND REJECTED: finish the status test first"));

    return;
  }

  if (automaticProcessIsActive()) {

    USB_LOG.println(
        F("MANUAL COMMAND REJECTED: automatic process is running"));

    return;
  }

  requestPulse(
      motion,
      motionDurationMs(motion));
}

// ============================================================
// START REQUESTED MOTOR
// ============================================================

void startRequestedMotion() {

  if (!chamberMotionReady()) {
    return;
  }

  activeMotion =
      requestedMotion;

  requestedMotion =
      MOTION_STOPPED;

  activeRunDurationMs =
      requestedRunDurationMs;

  requestedRunDurationMs = 0;

  runState =
      STATE_RUNNING;

  stateStartedAtMs =
      millis();

  switch (activeMotion) {

    case SHREDDER_FORWARD:

      digitalWrite(
          SHREDDER_LPWM,
          LOW);

      analogWrite(
          SHREDDER_RPWM,
          SHREDDER_FORWARD_DUTY);

      break;

    case SHREDDER_REVERSE:

      digitalWrite(
          SHREDDER_RPWM,
          LOW);

      analogWrite(
          SHREDDER_LPWM,
          SHREDDER_REVERSE_DUTY);

      break;

    case MIXER_FORWARD:

      digitalWrite(
          MIXER_LPWM,
          LOW);

      digitalWrite(
          MIXER_RPWM,
          HIGH);

      break;

    case MIXER_REVERSE:

      digitalWrite(
          MIXER_RPWM,
          LOW);

      digitalWrite(
          MIXER_LPWM,
          HIGH);

      break;

    case PRESSER_EXTEND:

      digitalWrite(
          PRESSER_LPWM,
          LOW);

      digitalWrite(
          PRESSER_RPWM,
          HIGH);

      break;

    case PRESSER_RETRACT:

      digitalWrite(
          PRESSER_RPWM,
          LOW);

      digitalWrite(
          PRESSER_LPWM,
          HIGH);

      break;

    case PUSHER_FORWARD:

      digitalWrite(
          PUSHER_IN3,
          LOW);

      digitalWrite(
          PUSHER_IN4,
          HIGH);

      break;

    case PUSHER_REVERSE:

      digitalWrite(
          PUSHER_IN4,
          LOW);

      digitalWrite(
          PUSHER_IN3,
          HIGH);

      break;

    default:

      stopAllMotors(false);

      return;
  }

  USB_LOG.print(
      motionName(
          activeMotion));

  USB_LOG.print(
      F(": running for "));

  USB_LOG.print(
      activeRunDurationMs);

  USB_LOG.println(
      reinterpret_cast<const __FlashStringHelper *>(PB_SHARED_76));

  if (activeMotion ==
          SHREDDER_FORWARD ||
      activeMotion ==
          SHREDDER_REVERSE) {

    emitEvent(
        reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_SHREDDER_STARTED),
        reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_info),
        reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_shredder),
        reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_running),
        activeMotion ==
                SHREDDER_FORWARD
            ? F("Forward ON")
            : F("Reverse ON"));

  } else if (
      activeMotion ==
          MIXER_FORWARD ||
      activeMotion ==
          MIXER_REVERSE) {

    emitEvent(
        F("MIXER_STARTED"),
        reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_info),
        reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_mixer),
        reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_running),
        F("Mixer started"));

  } else if (
      activeMotion ==
          PRESSER_EXTEND ||
      activeMotion ==
          PRESSER_RETRACT) {

    emitEvent(
        F("PRESSER_STARTED"),
        reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_info),
        reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_presser),
        reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_running),
        F("Presser ON"));

  } else if (
      activeMotion ==
          PUSHER_FORWARD ||
      activeMotion ==
          PUSHER_REVERSE) {

    emitEvent(
        F("PUSHER_STARTED"),
        reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_info),
        reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_pusher),
        reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_running),
        F("Pusher ON"));
  }
}

// ============================================================
// MOTOR STATE
// ============================================================

void serviceMotorState() {

  if (softwareStopLatched) {

    if (runState != STATE_IDLE) {
      stopAllMotors(false);
    }

    return;
  }

  const unsigned long nowMs =
      millis();

  if (runState ==
          STATE_WAITING &&
      nowMs -
              stateStartedAtMs >=
          DIRECTION_CHANGE_PAUSE_MS) {

    startRequestedMotion();
  }

  if (runState ==
          STATE_RUNNING &&
      nowMs -
              stateStartedAtMs >=
          activeRunDurationMs) {

    const Motion completedMotion =
        activeMotion;

    stopAllMotors(false);

    USB_LOG.print(
        motionName(
            completedMotion));

    USB_LOG.println(
        F(": pulse complete; stopped"));

    if (completedMotion ==
            SHREDDER_FORWARD ||
        completedMotion ==
            SHREDDER_REVERSE) {

      emitEvent(
          reinterpret_cast<const __FlashStringHelper *>(PB_SHARED_86),
          reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_info),
          reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_shredder),
          reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_stopped),
          F("Shred OFF"));

    } else if (
        completedMotion ==
            MIXER_FORWARD ||
        completedMotion ==
            MIXER_REVERSE) {

      emitEvent(
          reinterpret_cast<const __FlashStringHelper *>(PB_SHARED_88),
          reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_info),
          reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_mixer),
          (automaticProcessIsActive() ||
           diagnosticActive)
              ? reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_running)
              : reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_stopped),
          F("Mixer OFF"));

    } else if (
        completedMotion ==
            PRESSER_EXTEND ||
        completedMotion ==
            PRESSER_RETRACT) {

      emitEvent(
          F("PRESSER_COMPLETED"),
          reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_info),
          reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_presser),
          (automaticProcessIsActive() ||
           diagnosticActive)
              ? reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_running)
              : reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_stopped),
          F("Presser OFF"));

    } else if (
        completedMotion ==
            PUSHER_FORWARD ||
        completedMotion ==
            PUSHER_REVERSE) {

      emitEvent(
          F("PUSHER_COMPLETED"),
          reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_info),
          reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_pusher),
          (automaticProcessIsActive() ||
           diagnosticActive)
              ? reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_running)
              : reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_stopped),
          F("Pusher OFF"));
    }
  }
}

// ============================================================
// START AUTOMATIC PROCESS
// ============================================================

bool __attribute__((noinline)) startAutomaticProcess(
    bool localDryRun = false, bool localSession = false, bool fillOnly = false) {
  if (positioningStep) return false;

  if (localCycle.active() && !(localSession && localCycle.canForm())) {
    return false;
  }

  if (localDryRun &&
      production.active()) {

    return false;
  }

  if (!localDryRun &&
      !PAPERBRIQ_STANDALONE_BENCH &&
      !(production.kind == 'P' &&
        production.phase ==
            ProductionTimer::WAITING)) {

    USB_LOG.println(
        F("PROCESS REJECTED: set and enable the batch pump time first"));

    return false;
  }

  if (diagnosticActive) {

    USB_LOG.println(
        F("PROCESS REJECTED: component test running; stop it first"));

    return false;
  }

  if (!chamberMotionReady(localSession)) {
    return false;
  }

  if (statusSelfTestActive) {

    USB_LOG.println(
        F("PROCESS REJECTED: finish the status test first"));

    return false;
  }

  if (!controlIoOnline) {

    USB_LOG.println(
        F("PROCESS REJECTED: control-button expander is offline"));

    return false;
  }

  if (emergencyButton.stableActive) {

    USB_LOG.println(
        F("PROCESS REJECTED: emergency contact is open"));

    return false;
  }

  if (stopButton.stableActive) {

    USB_LOG.println(
        F("PROCESS REJECTED: physical Stop contact is open"));

    return false;
  }

  if (softwareStopLatched) {

    USB_LOG.println(
        F("PROCESS REJECTED: send R to arm first"));

    return false;
  }

  if (automaticProcessIsActive()) {

    USB_LOG.println(
        F("PROCESS REJECTED: a process is already running"));

    return false;
  }

  if (runState != STATE_IDLE) {

    USB_LOG.println(
        F("PROCESS REJECTED: wait for the manual motor test to stop"));

    return false;
  }

  stopAllMotors(false);

  if (localSession) {
    if (!localCycle.active()) {
      localCycle.preheat(millis());
      if (!fillOnly) {
        Serial.println(F("TEMP? W/X"));
        return true;
      }
    }
    // Break heater enable BEFORE starting any forming movement.
    digitalWrite(HEATER_ENABLE_PIN, LOW);
    localCycle.begin();
    localCycle.fillOnly = fillOnly;
    localLoadPaper = localMixWater = false;
  }

  automaticProcessState =
      PROCESS_WAITING_TO_CLOSE_GATE;

  // G = no ESP32/IR paper waiting.
  sensorControlledProcess =
      !localDryRun &&
      !(PAPERBRIQ_STANDALONE_BENCH &&
        !production.active());

  processStateStartedAtMs =
      millis();

  USB_LOG.println();

  USB_LOG.println(
      F("AUTOMATIC PROCESS STARTED"));

  USB_LOG.println(
      F("STEP: closing mixer gate and molder"));

  USB_LOG.print(
      F("Shredder follows inlet; mixer finish time: "));

  USB_LOG.print(
      PROCESS_MIXER_FINISH_MS);

  USB_LOG.println(
      reinterpret_cast<const __FlashStringHelper *>(PB_SHARED_76));

  emitEvent(
      F("PROCESS_STARTED"),
      reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_info),
      reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_process),
      reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_running),
      F("Process started"));

  return true;
}

// ============================================================
// CONTROL BUTTONS
// ============================================================

void serviceControlButtons() {

  const unsigned long nowMs =
      millis();

  if (nowMs -
          lastButtonPollAtMs <
      BUTTON_POLL_MS) {

    return;
  }

  lastButtonPollAtMs =
      nowMs;

  uint8_t value = 0xFF;

  if (!readControlIo(value)) {

    if (controlIoOnline) {

      controlIoOnline = false;

      USB_LOG.println(
          F("CONTROL BUTTONS LOST: latching software stop"));

      if (!softwareStopLatched ||
          diagnosticActive ||
          chamber.fanOn ||
          chamber.unlockOn) {

        latchSoftwareStop();
      }
    }

    return;
  }

  if (!controlIoOnline) {

    initializeControlIo();

    return;
  }

  const bool stopChanged =
      updateButton(
          stopButton,
          (value &
           STOP_BUTTON_MASK) != 0,
          nowMs);

  const bool emergencyChanged =
      updateButton(
          emergencyButton,
          (value &
           EMERGENCY_BUTTON_MASK) != 0,
          nowMs);

  const bool resetChanged =
      updateButton(
          resetButton,
          (value &
           RESET_BUTTON_MASK) == 0,
          nowMs);

  const bool startChanged =
      updateButton(
          startButton,
          (value &
           START_BUTTON_MASK) == 0,
          nowMs);

  if (emergencyButton.stableActive) {

    if (emergencyChanged) {

      USB_LOG.println(
          F("E-stop contact open"));

      if (!softwareStopLatched ||
          diagnosticActive ||
          chamber.fanOn ||
          chamber.unlockOn) {

        latchSoftwareStop();
      }
    }

    return;
  }

  if (emergencyChanged) {

    USB_LOG.println(
        F("EMERGENCY STOP: released; Reset is required to arm"));
  }

  if (stopButton.stableActive) {

    if (stopChanged) {

      USB_LOG.println(
          F("Stop contact open"));

      if (!softwareStopLatched ||
          diagnosticActive ||
          chamber.fanOn ||
          chamber.unlockOn) {

        latchSoftwareStop();
      }
    }

    return;
  }

  if (stopChanged) {

    USB_LOG.println(
        F("STOP BUTTON: released; Reset is required to arm"));
  }

  if (resetChanged &&
      resetButton.stableActive) {

    USB_LOG.println(
        F("RESET BUTTON: pressed"));

    armMotorTest();

    return;
  }

  if (startChanged &&
      startButton.stableActive) {

    Serial.println(
        F("START pressed"));

    startStandaloneCycle();
  }
}

// ============================================================
// STATUS LIGHTS
// ============================================================

void serviceStatusLights() {

  const unsigned long nowMs =
      millis();

  if (!statusIoOnline) {

    if (nowMs -
            lastStatusIoRetryAtMs >=
        STATUS_IO_RETRY_MS) {

      lastStatusIoRetryAtMs =
          nowMs;

      initializeStatusIo();
    }

    return;
  }

  if (statusSelfTestActive) {

    const bool unsafeToContinue =
        !controlIoOnline ||
        stopButton.stableActive ||
        emergencyButton.stableActive ||
        automaticProcessIsActive() ||
        runState != STATE_IDLE;

    if (unsafeToContinue) {

      statusSelfTestActive = false;

      writeStatusIo(0xFF);

      USB_LOG.println(
          F("STATUS TEST CANCELLED: machine state changed"));

      return;
    }

    if (nowMs -
            statusSelfTestStepStartedAtMs >=
        STATUS_SELF_TEST_STEP_MS) {

      statusSelfTestStep++;

      statusSelfTestStepStartedAtMs =
          nowMs;

      if (statusSelfTestStep >=
          STATUS_SELF_TEST_STEP_COUNT) {

        statusSelfTestActive = false;

        writeStatusIo(0xFF);

        USB_LOG.println(
            F("STATUS TEST COMPLETE: normal status display restored"));

        return;
      }

      printStatusSelfTestStep(
          statusSelfTestStep);
    }

    uint8_t testOutput = 0xFF;

    switch (statusSelfTestStep) {

      case 0:

        testOutput &=
            static_cast<uint8_t>(
                ~READY_LED_MASK);

        break;

      case 1:

        testOutput &=
            static_cast<uint8_t>(
                ~RUNNING_LED_MASK);

        break;

      case 2:

        testOutput &=
            static_cast<uint8_t>(
                ~STOPPED_LED_MASK);

        break;

      case 3:

        testOutput &=
            static_cast<uint8_t>(
                ~BUZZER_MASK);

        break;

      default:

        testOutput &=
            static_cast<uint8_t>(
                ~(READY_LED_MASK |
                  RUNNING_LED_MASK |
                  STOPPED_LED_MASK |
                  BUZZER_MASK));

        break;
    }

    if (chamber.outputs(testOutput) !=
            statusIoShadow &&
        !writeStatusIo(testOutput)) {

      statusSelfTestActive = false;

      statusIoOnline = false;

      lastStatusIoRetryAtMs =
          nowMs;

      USB_LOG.println(
          F("STATUS TEST FAILED: status-light expander was lost"));
    }

    return;
  }

  uint8_t desired = 0xFF;

  const bool blinkOn =
      ((nowMs /
        STATUS_LED_BLINK_MS) %
       2) == 0;

  const bool safetyFault =
      !chamber.online ||
      !chamber.doorClosed ||
      !controlIoOnline ||
      stopButton.stableActive ||
      emergencyButton.stableActive;

  const bool preparing =
      automaticProcessState ==
          PROCESS_WAITING_TO_CLOSE_GATE ||
      automaticProcessState ==
          PROCESS_WAITING_FOR_GATE_TO_CLOSE;

  const bool motionActive =
      automaticProcessIsActive() ||
      runState != STATE_IDLE;

  const bool completionBlinkActive =
      processCompletedAtMs != 0 &&
      nowMs -
              processCompletedAtMs <
          COMPLETED_LED_BLINK_MS;

  if (safetyFault) {

    if (blinkOn) {

      desired &=
          static_cast<uint8_t>(
              ~STOPPED_LED_MASK);

      desired &=
          static_cast<uint8_t>(
              ~BUZZER_MASK);
    }

  } else if (
      localCycle.heating() && localCycle.phase != StandaloneCycle::READY) {

    desired &=
        static_cast<uint8_t>(
            ~RUNNING_LED_MASK);

  } else if (
      softwareStopLatched) {

    desired &=
        static_cast<uint8_t>(
            ~STOPPED_LED_MASK);

  } else if (
      preparing) {

    if (blinkOn) {

      desired &=
          static_cast<uint8_t>(
              ~RUNNING_LED_MASK);
    }

  } else if (
      motionActive) {

    desired &=
        static_cast<uint8_t>(
            ~RUNNING_LED_MASK);

  } else if (
      completionBlinkActive) {

    if (blinkOn) {

      desired &=
          static_cast<uint8_t>(
              ~READY_LED_MASK);
    }

  } else {

    desired &=
        static_cast<uint8_t>(
            ~READY_LED_MASK);
  }

  if (chamber.outputs(desired) !=
          statusIoShadow &&
      !writeStatusIo(desired)) {

    statusIoOnline = false;

    lastStatusIoRetryAtMs =
        nowMs;

    USB_LOG.println(
        F("STATUS LIGHTS LOST: outputs switched off on reconnect"));
  }
}

// ============================================================
// START PAPER PROCESSING
// ============================================================

void applyBatchMixer() {
  digitalWrite(MIXER_RPWM, LOW);
  digitalWrite(MIXER_LPWM, LOW);
  if (!(batchMixer.stage & 1)) {
    const bool rpwm = PROCESS_MIXER_USE_RPWM ^ ((batchMixer.stage & 2) != 0);
    digitalWrite(rpwm ? MIXER_RPWM : MIXER_LPWM, HIGH);
  }
}

void startPaperProcessing() {
  paperExtensionActive = false;
  lastPaperOccupiedAt = millis();

  stopMotorOutputs();

  digitalWrite(
      SHREDDER_LPWM,
      LOW);

  analogWrite(
      SHREDDER_RPWM,
      SHREDDER_FORWARD_DUTY);

  if (sensorControlledProcess) {
  digitalWrite(
      MIXER_RPWM,
      PROCESS_MIXER_USE_RPWM ? HIGH : LOW);

  digitalWrite(
      MIXER_LPWM,
      PROCESS_MIXER_USE_RPWM ? LOW : HIGH);
  }

  Serial.println(F("SHREDDING"));

  automaticProcessState =
      PROCESS_SHREDDING_AND_MIXING;

  processStateStartedAtMs =
      millis();

  if (localCycle.fillOnly || localLoadPaper) localCycle.startWater(processStateStartedAtMs);

  emitEvent(
      reinterpret_cast<const __FlashStringHelper *>(PB_SHARED_121),
      reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_info),
      reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_process),
      reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_running),
      sensorControlledProcess
          ? F("Paper; mixing")
          : F("Batch started"));

  emitEvent(
      reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_SHREDDER_STARTED),
      reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_info),
      reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_shredder),
      reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_running),
      reinterpret_cast<const __FlashStringHelper *>(PB_SHARED_124));
}

// ============================================================
// AUTOMATIC PROCESS
// ============================================================

void serviceAutomaticProcess() {

  if (softwareStopLatched) {

    automaticProcessState =
        PROCESS_IDLE;

    return;
  }

  if (!automaticProcessIsActive()) {
    return;
  }

  if (!chamberMotionReady()) {
    return;
  }

  const unsigned long nowMs =
      millis();

  const bool feeding =
      automaticProcessState ==
          PROCESS_WAITING_FOR_PAPER ||
      automaticProcessState ==
          PROCESS_SHREDDING_AND_MIXING ||
      automaticProcessState ==
          PROCESS_MIXER_FINISHING;

  const bool irBatch = PAPERBRIQ_ESP_LINK_ENABLED && localCycle.active() && !localCycle.fillOnly;
  if ((sensorControlledProcess || irBatch) &&
      feeding &&
      (irBatch ? (!haveDualPaper || nowMs - dualPaperAt >= 1500UL) : nowMs - paperSignalAt >= 1500UL) &&
      (automaticProcessState !=
           PROCESS_WAITING_FOR_PAPER ||
       nowMs -
               processStateStartedAtMs >
           2000UL)) {

    latchSoftwareStop();

    emitEvent(
        reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_FAULT),
        reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_error),
        F("paper_sensor"),
        reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_fault),
        F("Paper link lost; R"));

    return;
  }

  const bool localRecipe = localCycle.active();
  const bool localFill = localRecipe && localCycle.fillOnly;
  const bool sensorFill = !PAPERBRIQ_ESP_LINK_ENABLED && localFill && PAPERBRIQ_FILL_IR_ENABLED;
  const bool keepShredderOn = localRecipe && !sensorFill;

  switch (automaticProcessState) {

    case PROCESS_WAITING_TO_CLOSE_GATE:

      if (nowMs -
              lastMotorStoppedAtMs >=
          DIRECTION_CHANGE_PAUSE_MS) {

        setMixerGateAngle(
            MIXER_GATE_CLOSED_ANGLE,
            reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_CLOSED));

        setMolderAngle(
            MOLDER_CLOSED_ANGLE,
            reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_CLOSED));

        automaticProcessState =
            PROCESS_WAITING_FOR_GATE_TO_CLOSE;

        processStateStartedAtMs =
            nowMs;
      }

      break;

    case PROCESS_WAITING_FOR_GATE_TO_CLOSE:

      if (nowMs -
              processStateStartedAtMs >=
          PROCESS_CLOSE_TRAVEL_MS) {

        if (sensorFill) {
          automaticProcessState = PROCESS_WAITING_FOR_PAPER;
          Serial.println(F("M: paper"));
        } else if (!sensorControlledProcess) {
          startPaperProcessing();

        } else {

          automaticProcessState =
              PROCESS_WAITING_FOR_PAPER;

          processStateStartedAtMs =
              nowMs;

          paperClearSeen = false;

          emitEvent(
              F("WAITING_FOR_PAPER"),
              reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_info),
              reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_process),
              reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_running),
              F("Ready for paper"));
        }
      }

      break;

    case PROCESS_WAITING_FOR_PAPER:
      if (sensorFill && digitalRead(FILL_IR_IN_PIN) == LOW) {
        startPaperProcessing();
        fillLastPaperAtMs = processStateStartedAtMs;
      }
      break;

    case PROCESS_SHREDDING_AND_MIXING:
      if (sensorFill) {
        if (nowMs - processStateStartedAtMs >= FILL_SHREDDER_MAX_MS) {
          latchSoftwareStop();
          Serial.println(F("M: check feed"));
          break;
        }
        if (digitalRead(FILL_IR_IN_PIN) == LOW || digitalRead(FILL_IR_OUT_PIN) == LOW) {
          fillLastPaperAtMs = nowMs;
        }
        if (nowMs - fillLastPaperAtMs < FILL_IR_CLEAR_MS) break;
      }
      if (sensorControlledProcess
              ? !paperDetected
              : sensorFill || nowMs - processStateStartedAtMs >=
                  (localRecipe && !localFill ? LOCAL_SHREDDER_LEAD_MS : BENCH_SHREDDER_RUN_MS)) {

        if (!keepShredderOn) {
          digitalWrite(SHREDDER_RPWM, LOW);
          digitalWrite(SHREDDER_LPWM, LOW);
        }

        automaticProcessState =
            PROCESS_MIXER_FINISHING;

        processStateStartedAtMs =
            nowMs;

        if (!sensorControlledProcess) {
          batchMixer.begin(nowMs, localCycle.active());
          applyBatchMixer();
          Serial.println(F("MIXING"));
        }

        if (!keepShredderOn) {
          emitEvent(
              reinterpret_cast<const __FlashStringHelper *>(PB_SHARED_86),
              reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_info),
              reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_shredder),
              reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_running),
              F("Mixing"));
        }
      }

      break;

    case PROCESS_MIXER_FINISHING:

      if (sensorControlledProcess &&
          paperDetected) {

        digitalWrite(
            SHREDDER_LPWM,
            LOW);

        analogWrite(
            SHREDDER_RPWM,
            SHREDDER_FORWARD_DUTY);

        automaticProcessState =
            PROCESS_SHREDDING_AND_MIXING;

        processStateStartedAtMs =
            nowMs;

        emitEvent(
            reinterpret_cast<const __FlashStringHelper *>(PB_SHARED_121),
            reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_info),
            reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_process),
            reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_running),
            F("Shredding resumed"));

        emitEvent(
            reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_SHREDDER_STARTED),
            reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_info),
            reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_shredder),
            reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_running),
            reinterpret_cast<const __FlashStringHelper *>(PB_SHARED_124));

        break;
      }

      if (production.kind == 'P' &&
          production.phase ==
              ProductionTimer::RUNNING) {

        break;
      }

      if (production.kind == 'P' &&
          production.phase ==
              ProductionTimer::WAITING &&
          nowMs -
                  processStateStartedAtMs >=
              PROCESS_MIXER_FINISH_MS) {

        production.stop(
            true,
            nowMs);

        latchSoftwareStop();

        return;
      }

      if (!sensorControlledProcess &&
          batchMixer.tick(nowMs, BATCH_MIX_FORWARD_MS, BATCH_MIX_REVERSE_MS,
                          BATCH_MIX_PAUSE_MS, localRecipe ? FILL_MIX_EXTRA_MS : 0)) {
        applyBatchMixer();
      }

      if (sensorControlledProcess
              ? nowMs - processStateStartedAtMs >= PROCESS_MIXER_FINISH_MS
              : batchMixer.done()) {

        const bool clearingPaper = nowMs - lastPaperOccupiedAt < PAPER_CLEAR_MS;
        if (irBatch && (clearingPaper || (localMixWater && localCycle.pumping()))) {
          if (!clearingPaper) digitalWrite(SHREDDER_RPWM, LOW);
          if (!paperExtensionActive) {
            paperExtensionActive = true;
            paperExtensionAt = nowMs;
            digitalWrite(MIXER_RPWM, LOW);
            digitalWrite(MIXER_LPWM, LOW);
          }
          if (nowMs - paperExtensionAt >= BATCH_MIX_PAUSE_MS) {
            digitalWrite(PROCESS_MIXER_USE_RPWM ? MIXER_RPWM : MIXER_LPWM, HIGH);
          }
          break;
        }
        paperExtensionActive = false;

        if (keepShredderOn) digitalWrite(SHREDDER_RPWM, LOW);

        digitalWrite(
            MIXER_RPWM,
            LOW);

        digitalWrite(
            MIXER_LPWM,
            LOW);

        lastMotorStoppedAtMs =
            nowMs;

        automaticProcessState =
            PROCESS_WAITING_TO_OPEN_GATE;

        processStateStartedAtMs =
            nowMs;

        USB_LOG.println(
            F("Mixer stopped; waiting before opening gate"));

        emitEvent(
            reinterpret_cast<const __FlashStringHelper *>(PB_SHARED_88),
            reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_info),
            reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_mixer),
            reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_running),
            F("Mixing ended"));
      }

      break;

    case PROCESS_WAITING_TO_OPEN_GATE:

      if (nowMs -
              lastMotorStoppedAtMs >=
          (localRecipe ? (localFill ? FILL_GATE_WAIT_MS : LOCAL_GATE_WAIT_MS)
                       : DIRECTION_CHANGE_PAUSE_MS) &&
          !(irBatch && localCycle.pumping())) {

        if (localRecipe) {
          localCycle.stopWater();
          digitalWrite(PUMP_ENABLE_PIN, LOW);
        }

        USB_LOG.println(
            F("STEP: opening mixer gate; molder remains closed"));

        setMixerGateAngle(
            MIXER_GATE_OPEN_ANGLE,
            reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_OPEN));

        automaticProcessState =
            PROCESS_WAITING_FOR_GATE_TO_OPEN;

        processStateStartedAtMs =
            nowMs;
      }

      break;

    case PROCESS_WAITING_FOR_GATE_TO_OPEN:

      if (nowMs -
              processStateStartedAtMs >=
          MIXER_GATE_TRAVEL_MS) {

        automaticProcessState =
            PROCESS_RELEASING_MATERIAL;

        processStateStartedAtMs =
            nowMs;

        USB_LOG.println(
            F("STEP: releasing material into the closed molder"));

        emitEvent(
            F("MATERIAL_RELEASE_STARTED"),
            reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_info),
            reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_process),
            reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_running),
            F("Release"));
      }

      break;

    case PROCESS_RELEASING_MATERIAL:

      if (nowMs -
              processStateStartedAtMs >=
          PROCESS_MATERIAL_RELEASE_MS) {

        USB_LOG.println(
            F("STEP: closing mixer gate before pressing"));

        setMixerGateAngle(
            MIXER_GATE_CLOSED_ANGLE,
            reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_CLOSED));

        automaticProcessState =
            PROCESS_WAITING_FOR_MIXER_GATE_TO_CLOSE;

        processStateStartedAtMs =
            nowMs;
      }

      break;

    case PROCESS_WAITING_FOR_MIXER_GATE_TO_CLOSE:

      if (nowMs -
              processStateStartedAtMs >=
          MIXER_GATE_TRAVEL_MS) {

        // End water before even requesting a presser movement. G and the
        // separate ESP32-controlled recipe do not own this local output.
        if (localCycle.active()) {
          localCycle.stopWater();
          digitalWrite(PUMP_ENABLE_PIN, LOW);
          if (localCycle.fillOnly) {
            latchSoftwareStop();
            Serial.println(F("FILL DONE; R"));
            break;
          }
        }

        USB_LOG.println(
            F("STEP: presser extending"));

        emitEvent(
            F("PRESSING_STARTED"),
            reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_info),
            reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_process),
            reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_running),
            F("Pressing"));

        requestPulse(
            PRESSER_EXTEND,
            localCycle.active()
                ? (localCycle.batch == 0 ? BATCH_PRESS_FIRST_MS :
                   localCycle.batch == 1 ? BATCH_PRESS_SECOND_MS : BATCH_PRESS_THIRD_MS)
                : PROCESS_PRESSER_EXTEND_MS);

        automaticProcessState =
            PROCESS_PRESSER_EXTENDING;
      }

      break;

    case PROCESS_PRESSER_EXTENDING:

      if (runState == STATE_IDLE) {

        USB_LOG.println(
            F("STEP: presser extension complete; retracting"));

        requestPulse(
            PRESSER_RETRACT,
            PROCESS_PRESSER_RETRACT_MS);

        automaticProcessState =
            PROCESS_PRESSER_RETRACTING;
      }

      break;

    case PROCESS_PRESSER_RETRACTING:

      if (runState == STATE_IDLE) {

        if (localRecipe && !localFill) {
          if (localCycle.batch < 2) {
            localLoadPaper = localMixWater = false;
            automaticProcessState = PROCESS_WAITING_FOR_NEXT_BATCH;
            Serial.println(F("NEXT LOAD: paper"));
            break;
          }
          if (!localCycle.repressed) {
            localCycle.repressed = true;
            requestPulse(PRESSER_EXTEND, BATCH_PRESS_THIRD_REPEAT_MS);
            automaticProcessState = PROCESS_PRESSER_EXTENDING;
            break;
          }
        }

        USB_LOG.println(
            F("Presser retracted; waiting before opening molder"));

        automaticProcessState =
            PROCESS_WAITING_TO_OPEN_MOLDER;

        processStateStartedAtMs =
            nowMs;
      }

      break;

    case PROCESS_WAITING_TO_OPEN_MOLDER:

      if (nowMs -
              lastMotorStoppedAtMs >=
          DIRECTION_CHANGE_PAUSE_MS) {

        USB_LOG.println(
            F("STEP: opening molder gate for ejection"));

        emitEvent(
            F("EJECTION_STARTED"),
            reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_info),
            reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_process),
            reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_running),
            F("Open molder"));

        setMolderAngle(
            MOLDER_OPEN_ANGLE,
            reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_OPEN));

        automaticProcessState =
            PROCESS_WAITING_FOR_MOLDER_TO_OPEN;

        processStateStartedAtMs =
            nowMs;
      }

      break;

    case PROCESS_WAITING_FOR_MOLDER_TO_OPEN:

      if (nowMs -
              processStateStartedAtMs >=
          MOLDER_TRAVEL_MS) {

        USB_LOG.println(
            F("STEP: molder open; pusher extending"));

        requestPulse(
            PUSHER_FORWARD,
            localRecipe && !localFill ? LOCAL_PUSHER_EXTEND_MS : PROCESS_PUSHER_EXTEND_MS);

        automaticProcessState =
            PROCESS_PUSHER_EXTENDING;
      }

      break;

    case PROCESS_PUSHER_EXTENDING:

      if (runState == STATE_IDLE) {

        USB_LOG.println(
            F("STEP: pusher extension complete; retracting"));

        requestPulse(
            PUSHER_REVERSE,
            localRecipe && !localFill ? LOCAL_PUSHER_RETRACT_MS : PROCESS_PUSHER_RETRACT_MS);

        automaticProcessState =
            PROCESS_PUSHER_RETRACTING;
      }

      break;

    case PROCESS_PUSHER_RETRACTING:

      if (runState == STATE_IDLE) {

        USB_LOG.println(
            F("Pusher retracted; waiting before closing molder"));

        automaticProcessState =
            PROCESS_WAITING_TO_CLOSE_MOLDER;

        processStateStartedAtMs =
            nowMs;
      }

      break;

    case PROCESS_WAITING_TO_CLOSE_MOLDER:

      if (nowMs -
              lastMotorStoppedAtMs >=
          DIRECTION_CHANGE_PAUSE_MS) {

        USB_LOG.println(
            F("STEP: closing molder gate"));

        emitEvent(
            F("GATES_CLOSING"),
            reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_info),
            reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_process),
            reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_running),
            F("Close gates"));

        setMolderAngle(
            MOLDER_CLOSED_ANGLE,
            reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_CLOSED));

        automaticProcessState =
            PROCESS_WAITING_FOR_MOLDER_TO_CLOSE;

        processStateStartedAtMs =
            nowMs;
      }

      break;

    case PROCESS_WAITING_FOR_MOLDER_TO_CLOSE:

      if (nowMs -
              processStateStartedAtMs >=
          MOLDER_TRAVEL_MS) {

        automaticProcessState =
            PROCESS_IDLE;

        if (localCycle.phase ==
            StandaloneCycle::FORMING) {

          stopAllMotors(false);

          localCycle.heat(
              nowMs);

          Serial.println(
              F("DRYING"));

          break;
        }

        processCompletedAtMs =
            nowMs;

        USB_LOG.println(
            F("PROCESS COMPLETE: mixer and molder gates closed"));

        emitEvent(
            F("PROCESS_COMPLETED"),
            reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_info),
            reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_process),
            reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_stopped),
            sensorControlledProcess
                ? F("Formed; load to dry")
                : F("Inspect cycle"));
      }

      break;

    case PROCESS_IDLE:
    default:
      break;
  }
}

// ============================================================
// CHAMBER
// ============================================================

void serviceChamber(bool force) {

  const unsigned long nowMs = millis();

  if (!force &&
      nowMs - lastDoorPollAtMs < BUTTON_POLL_MS) {
    return;
  }

  lastDoorPollAtMs = nowMs;

  const bool wasClosed = chamber.doorClosed;
  const bool wasOnline = chamber.online;

  chamber.tick(nowMs);

  const bool readOk =
      Wire.requestFrom(
          STATUS_IO_ADDRESS,
          static_cast<uint8_t>(1)
      ) == 1;

  if (readOk) {
    lastStatusInputs = Wire.read();
    statusIoFailCount = 0;
    statusIoOnline = true;
  } else {
    if (statusIoFailCount < 255) {
      statusIoFailCount++;
    }
  }

  const bool rawDoorClosed =
      (lastStatusInputs & ChamberControl::DOOR) == 0;

  const bool boardAvailable =
      statusIoOnline &&
      statusIoFailCount < STATUS_IO_FAIL_LIMIT;

  chamber.sample(
      boardAvailable,
      rawDoorClosed,
      nowMs
  );

  // Real door opening = immediate stop
  if (readOk &&
      !rawDoorClosed &&
      (!softwareStopLatched ||
       automaticProcessIsActive() ||
       runState != STATE_IDLE)) {

    latchSoftwareStop();

    emitEvent(
        reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_FAULT),
        reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_error),
        reinterpret_cast<const __FlashStringHelper *>(PB_SHARED_163),
        reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_fault),
        F("Door open; Reset")
    );

    return;
  }

  // Only treat 0x24 as lost after several consecutive failures
  if (statusIoFailCount >= STATUS_IO_FAIL_LIMIT) {

    statusIoOnline = false;

    chamber.sample(
        false,
        rawDoorClosed,
        nowMs
    );

    if (!softwareStopLatched ||
        automaticProcessIsActive() ||
        runState != STATE_IDLE) {

      latchSoftwareStop();

      emitEvent(
          reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_FAULT),
          reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_error),
          reinterpret_cast<const __FlashStringHelper *>(PB_SHARED_163),
          reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_fault),
          F("0x24 lost; Reset")
      );

      return;
    }
  }

  if (wasClosed != chamber.doorClosed ||
      wasOnline != chamber.online) {

    printChamberStatus();
  }

  if (statusIoOnline &&
      chamber.outputs(statusIoShadow) != statusIoShadow) {

    if (!writeStatusIo(statusIoShadow)) {

      if (statusIoFailCount < 255) {
        statusIoFailCount++;
      }

    } else {
      statusIoFailCount = 0;
    }
  }
}

bool chamberMotionReady(bool localSession) {

  if (localCycle.heating() && !(localSession && localCycle.canForm())) {

    return false;
  }

  if (thermal.heating(
          production)) {

    return false;
  }

  if (heaterTestActive ||
      pumpTestActive) {

    return false;
  }

  serviceChamber(true);

  if (chamber.ready()) {
    return true;
  }

  if (!chamber.online) {

    Serial.println(
        F("0x24 offline"));

  } else if (
      !chamber.doorClosed) {

    Serial.println(
        F("Door open"));

  } else if (
      chamber.unlockOn) {

    Serial.println(
        F("Unlock"));

  } else if (
      chamber.fanOn) {

    Serial.println(
        F("Fan; N/R"));
  }

  return false;
}

// FIXED: I command now prints directly to Serial.
void printChamberStatus() {

  Serial.print(
      F("0x24 door P2="));

  Serial.print(
      !chamber.online
          ? reinterpret_cast<const __FlashStringHelper *>(PB_SHARED_171)
          : chamber.doorClosed
                ? reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_CLOSED)
                : reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_OPEN));

  Serial.print(
      F("; raw P2="));

  Serial.print(
      !chamber.online
          ? reinterpret_cast<const __FlashStringHelper *>(PB_SHARED_171)
          : (lastStatusInputs &
             ChamberControl::DOOR)
                ? F("HIGH")
                : F("LOW"));

  Serial.print(
      F("; fan P5="));

  Serial.print(
      chamber.fanOn
          ? F("ON")
          : F("OFF"));

  Serial.print(
      F("; unlock P6="));

  Serial.println(
      chamber.unlockOn
          ? F("PULSE")
          : F("OFF"));

  if (chamber.online) {

    Serial.print(
        F("P0..7: "));

    uint8_t bits = lastStatusInputs;
    for (uint8_t pin = 0; pin < 8; ++pin) {
      Serial.print(char('0' + (bits & 1)));
      bits >>= 1;
    }

    Serial.println();
  }
}

// ============================================================
// CHAMBER MANUAL COMMAND
// ============================================================

bool runChamberCommand(
    const char *action) {
  if (positioningStep) return false;

  if (localCycle.active()) {
    return false;
  }

  if (heaterTestActive ||
      pumpTestActive) {

    return false;
  }

  if (diagnosticActive) {
    return false;
  }

  serviceChamber(true);

  const unsigned long nowMs =
      millis();

  const bool settled =
      softwareStopLatched &&
      !automaticProcessIsActive() &&
      runState == STATE_IDLE &&
      nowMs -
              lastMotorStoppedAtMs >=
          DIRECTION_CHANGE_PAUSE_MS &&
      nowMs -
              lastGateCommandAtMs >=
          DIRECTION_CHANGE_PAUSE_MS &&
      !statusSelfTestActive;

  bool accepted = false;

  if (strcmp(
          action,
          "FAN_OFF") == 0) {

    chamber.fanOn = false;

    accepted = true;

  } else if (
      strcmp(
          action,
          "LOCK") == 0) {

    chamber.unlockOn = false;

    accepted = true;

  } else if (
      strcmp(
          action,
          "FAN_TEST") == 0) {

    accepted =
        controlIoOnline &&
        !stopButton.stableActive &&
        !emergencyButton.stableActive &&
        chamber.startFan(
            nowMs,
            settled);

  } else if (
      strcmp(
          action,
          "UNLOCK") == 0) {

    accepted =
        controlIoOnline &&
        chamber.unlock(
            nowMs,
            settled);
  }

  if (!accepted) {

    USB_LOG.println(
        F("CHAMBER REJECTED: stop, wait, and check door/board"));

    return false;
  }

  if (!statusIoOnline ||
      !writeStatusIo(
          statusIoShadow)) {

    chamber.sample(
        false,
        false,
        nowMs);

    statusIoOnline = false;

    return false;
  }

  printChamberStatus();

  return true;
}

// ============================================================
// DIAGNOSTICS
// ============================================================

const __FlashStringHelper *diagnosticItem(
    uint8_t step) {

  switch (step) {

    case 0:
      return reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_shredder);

    case 1:
      return reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_mixer);

    case 2:
    case 3:
      return F("mixer_gate");

    case 4:
    case 5:
      return F("molder_gate");

    case 6:
    case 7:
      return reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_presser);

    case 8:
    case 9:
      return reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_pusher);

    case 10:
      return F("fan");

    case 11:
      return F("indicators");

    default:
      return F("lock");
  }
}

bool startComponentTest() {
  if (positioningStep) return false;

  if (production.active()) {
    return false;
  }

  if (!chamberMotionReady() ||
      diagnosticActive ||
      softwareStopLatched ||
      automaticProcessIsActive() ||
      runState != STATE_IDLE ||
      statusSelfTestActive ||
      !controlIoOnline ||
      stopButton.stableActive ||
      emergencyButton.stableActive) {

    return false;
  }

  diagnosticActive = true;

  diagnosticStepRunning = false;

  diagnosticStep = 0;

  diagnosticStartedAtMs =
      millis();

  diagnosticStepAtMs =
      millis();

  emitEvent(
      F("TEST_STARTED"),
      reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_info),
      reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_diagnostics),
      reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_running),
      F("Unloaded test"));

  return true;
}

void serviceComponentTest() {

  if (!diagnosticActive) {
    return;
  }

  serviceChamber(true);

  if (!diagnosticActive) {
    return;
  }

  const unsigned long nowMs =
      millis();

  if (!chamber.online ||
      !chamber.doorClosed ||
      !controlIoOnline ||
      stopButton.stableActive ||
      emergencyButton.stableActive ||
      nowMs -
              diagnosticStartedAtMs >
          180000UL ||
      nowMs -
              diagnosticStepAtMs >
          15000UL) {

    latchSoftwareStop();

    return;
  }

  if (!diagnosticStepRunning) {

    if (nowMs -
                lastMotorStoppedAtMs <
            DIRECTION_CHANGE_PAUSE_MS ||
        nowMs -
                lastGateCommandAtMs <
            DIRECTION_CHANGE_PAUSE_MS) {

      return;
    }

    if (diagnosticStep != 3 &&
        diagnosticStep != 5 &&
        diagnosticStep != 7 &&
        diagnosticStep != 9) {

      emitEvent(
          F("TEST_ITEM_STARTED"),
          reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_info),
          diagnosticItem(
              diagnosticStep),
          reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_running),
        F("Testing"));
    }

    diagnosticStepRunning = true;

    diagnosticStepAtMs =
        nowMs;

    switch (diagnosticStep) {

      case 0:

        requestPulse(
            SHREDDER_FORWARD,
            1200);

        break;

      case 1:

        requestPulse(
            MIXER_REVERSE,
            2000);

        break;

      case 2:

        setMixerGateAngle(
            MIXER_GATE_OPEN_ANGLE,
            reinterpret_cast<const __FlashStringHelper *>(PB_SHARED_192));

        break;

      case 3:

        setMixerGateAngle(
            MIXER_GATE_CLOSED_ANGLE,
            reinterpret_cast<const __FlashStringHelper *>(PB_SHARED_193));

        break;

      case 4:

        setMolderAngle(
            MOLDER_OPEN_ANGLE,
            reinterpret_cast<const __FlashStringHelper *>(PB_SHARED_192));

        break;

      case 5:

        setMolderAngle(
            MOLDER_CLOSED_ANGLE,
            reinterpret_cast<const __FlashStringHelper *>(PB_SHARED_193));

        break;

      case 6:

        requestPulse(
            PRESSER_EXTEND,
            1000);

        break;

      case 7:

        requestPulse(
            PRESSER_RETRACT,
            1000);

        break;

      case 8:

        requestPulse(
            PUSHER_FORWARD,
            1000);

        break;

      case 9:

        requestPulse(
            PUSHER_REVERSE,
            1000);

        break;

      case 10:

        if (!chamber.startFan(
                nowMs,
                true) ||
            !writeStatusIo(
                statusIoShadow)) {

          latchSoftwareStop();

          return;
        }

        break;

      case 11:

        statusSelfTestActive = true;

        statusSelfTestStep = 0;

        statusSelfTestStepStartedAtMs =
            nowMs;

        break;

      case 12:

        softwareStopLatched = true;

        softwareStopLatchedAtMs =
            nowMs;

        if (!chamber.unlock(
                nowMs,
                true) ||
            !writeStatusIo(
                statusIoShadow)) {

          latchSoftwareStop();

          return;
        }

        break;
    }

    return;
  }

  bool finished = false;

  switch (diagnosticStep) {

    case 0:
    case 1:
    case 6:
    case 7:
    case 8:
    case 9:

      finished =
          runState == STATE_IDLE;

      break;

    case 2:
    case 3:
    case 4:
    case 5:

      finished =
          nowMs -
                  diagnosticStepAtMs >=
              PROCESS_CLOSE_TRAVEL_MS;

      break;

    case 10:

      if (nowMs -
              diagnosticStepAtMs >=
          3000) {

        chamber.fanOn = false;

        if (!writeStatusIo(
                statusIoShadow)) {

          latchSoftwareStop();

          return;
        }

        finished = true;
      }

      break;

    case 11:

      finished =
          !statusSelfTestActive;

      break;

    case 12:

      finished =
          !chamber.unlockOn &&
          (statusIoShadow &
           ChamberControl::UNLOCK) != 0;

      break;
  }

  if (!finished) {
    return;
  }

  if (diagnosticStep != 2 &&
      diagnosticStep != 4 &&
      diagnosticStep != 6 &&
      diagnosticStep != 8) {

    emitEvent(
        F("TEST_ITEM_DONE"),
        reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_info),
        diagnosticItem(
            diagnosticStep),
        reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_running),
        F("Inspect run"));
  }

  diagnosticStep++;

  diagnosticStepRunning = false;

  diagnosticStepAtMs =
      nowMs;

  if (diagnosticStep > 12) {

    diagnosticActive = false;

    emitEvent(
        F("TEST_COMPLETED"),
        reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_info),
        reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_diagnostics),
        reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_stopped),
        F("Inspect tests"));
  }
}

// ============================================================
// REMOTE COMMAND ID
// ============================================================

bool validRemoteCommandId(
    const char *id) {

  const size_t length =
      strlen(id);

  if (length == 0 ||
      length >=
          sizeof(
              lastRemoteCommandId)) {

    return false;
  }

  for (size_t i = 0;
       i < length;
       ++i) {

    const char c =
        id[i];

    if (!isalnum(
            static_cast<unsigned char>(
                c)) &&
        c != '-' &&
        c != '_') {

      return false;
    }
  }

  return true;
}

void failRemoteCommand(
    const char *id,
    const char *message) {

  emitAck(
      id,
      "FAILED",
      message);

  rememberRemoteResult(
      id,
      "FAILED",
      message);
}

// ============================================================
// MACHINE HEALTH
// ============================================================

void reportMachineHealth(
    const char *id) {

  serviceProduction();

  espLink.print(F("PRO2|"));
  espLink.print(id);

  espLink.print('|');
  espLink.print(
      PAPERBRIQ_PRODUCTION_PUMP_MAX_SECONDS);

  espLink.print('|');
  espLink.print(
      PAPERBRIQ_PRODUCTION_DRYING_MAX_SECONDS);

  espLink.print('|');
  espLink.print(
      PAPERBRIQ_PUMP_VERIFIED
          ? 1
          : 0);

  espLink.print('|');
  espLink.print(
      PAPERBRIQ_HEATER_VERIFIED
          ? 1
          : 0);

  espLink.print('|');
  espLink.print(
      production.kind == 'P'
          ? 1
          : production.kind == 'D'
                ? 2
                : production.kind == 'H'
                      ? 3
                      : 0);

  espLink.print('|');
  espLink.print(
      (uint8_t)production.phase);

  espLink.print('|');
  espLink.print(
      production.run);

  espLink.print('|');
  espLink.print(
      production.total);

  espLink.print('|');
  espLink.print(
      production.elapsed(
          millis()));

  espLink.print('|');
  espLink.print(
      production.remaining(
          millis()));

  espLink.print('|');
  espLink.print(
      PAPERBRIQ_PREHEAT_MAX_SECONDS);

  espLink.print('|');
  espLink.print(
      PAPERBRIQ_CHAMBER_TARGET_C);

  espLink.print('|');
  espLink.print(
      heaterReadingValid &&
              millis() -
                      heaterReadingAt <
                  5000UL
          ? 1
          : 0);

  espLink.print('|');
  espLink.print(
      (uint8_t)thermal.stage);

  espLink.print('|');
  espLink.println(
      PAPERBRIQ_HEATER_FAN_CONFIRMED
          ? 1
          : 0);

  uint8_t inputs = 0xFF;

  const bool controls =
      readControlIo(
          inputs);

  Wire.beginTransmission(
      STATUS_IO_ADDRESS);

  const bool indicators =
      Wire.endTransmission() == 0;

  espLink.print(
      F("HEALTH|"));

  espLink.print(id);

  espLink.print('|');
  espLink.print(
      controls
          ? 1
          : 0);

  espLink.print('|');
  espLink.print(
      controls &&
              (inputs &
               STOP_BUTTON_MASK) == 0
          ? 1
          : 0);

  espLink.print('|');
  espLink.print(
      controls &&
              (inputs &
               EMERGENCY_BUTTON_MASK) == 0
          ? 1
          : 0);

  espLink.print('|');
  espLink.print(
      indicators
          ? 1
          : 0);

  espLink.print('|');
  espLink.print(
      !softwareStopLatched &&
              chamber.ready() &&
              !diagnosticActive
          ? 1
          : 0);

  espLink.print('|');
  espLink.print(
      chamber.online &&
              chamber.doorClosed
          ? 1
          : 0);

  espLink.print('|');
  espLink.print(
      chamber.fanOn
          ? 1
          : 0);

  espLink.print('|');
  espLink.print(
      chamber.unlockOn
          ? 1
          : 0);

  espLink.print('|');
  espLink.print(
      diagnosticActive
          ? 1
          : 0);

  espLink.print('|');
  espLink.print(
      PAPERBRIQ_STANDALONE_BENCH
          ? 1
          : 0);

  espLink.print('|');
  espLink.print(
      heaterTestActive
          ? 1
          : 0);

  espLink.print('|');
  espLink.print(
      pumpTestActive
          ? 1
          : 0);

  const bool idle =
      benchConditionsReady() &&
      !production.active() &&
      !heaterTestActive &&
      !pumpTestActive;

  espLink.print('|');
  espLink.print(
      idle &&
              (!heaterBenchUsed ||
               millis() -
                       heaterTestAt >=
                   60000UL)
          ? 1
          : 0);

  espLink.print('|');
  espLink.println(
      idle &&
              (!pumpHasRun ||
               millis() -
                       pumpTestAt >=
                   10000UL)
          ? 1
          : 0);
}

// ============================================================
// REMOTE COMMAND
// ============================================================

void printRejectedFrame(Print &output, const char *line, uint8_t length) {
  output.print(F("UART_BAD|"));
  for (uint8_t i = 0; i < length; ++i) {
    // strtok replaced field separators with NUL. Restore them for the log;
    // render nonprinting bytes as '?' so received data cannot inject log lines.
    const uint8_t c = line[i];
    output.print(c == 0 ? '|' : c >= 32 && c <= 126 ? char(c) : '?');
  }
  output.println();
}

void handleRemoteCommandFrame(
    char *line) {

  const uint8_t receivedLength = strlen(line);

  Serial.print(
      F("RX: "));

  Serial.println(
      line);

  char *prefix =
      strtok(
          line,
          "|");

  char *id =
      strtok(
          nullptr,
          "|");

  char *action =
      strtok(
          nullptr,
          "|");

  char *extra =
      strtok(
          nullptr,
          "|");

  if (prefix == nullptr ||
      id == nullptr ||
      action == nullptr ||
      extra != nullptr ||
      strcmp(
          prefix,
          "CMD") != 0 ||
      !validRemoteCommandId(
          id)) {

    latchSoftwareStop();

    emitEvent(
        reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_FAULT),
        reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_error),
        reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_controller),
        reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_fault),
        F("Invalid UART command"));

    printRejectedFrame(Serial, line, receivedLength);
    printRejectedFrame(espLink, line, receivedLength);

    return;
  }

  if (strcmp(
          action,
          "CHECK") == 0) {

    reportMachineHealth(id);

    return;
  }

  if (strcmp(
          id,
          lastRemoteCommandId) == 0 &&
      lastRemoteStatus[0] !=
          '\0') {

    emitAck(
        id,
        lastRemoteStatus,
        lastRemoteMessage);

    return;
  }

  emitAck(
      id,
      "DELIVERED",
      "received");

  char output;
  bool edit;
  uint32_t run;
  uint32_t productionSeconds;

  if (parseProductionCommand(
          action,
          output,
          edit,
          run,
          productionSeconds)) {

    serviceChamber(true);

    serviceControlButtons();

    serviceProduction();

    const uint32_t maximum =
        output == 'P'
            ? PAPERBRIQ_PRODUCTION_PUMP_MAX_SECONDS
            : output == 'H'
                  ? PAPERBRIQ_PREHEAT_MAX_SECONDS
                  : PAPERBRIQ_PRODUCTION_DRYING_MAX_SECONDS;

    const bool verified =
        output == 'P'
            ? PAPERBRIQ_PUMP_VERIFIED
            : PAPERBRIQ_HEATER_VERIFIED &&
                  PAPERBRIQ_HEATER_FAN_CONFIRMED &&
                  PAPERBRIQ_CHAMBER_TARGET_C >= 30 &&
                  PAPERBRIQ_CHAMBER_TARGET_C <= 50 &&
                  heaterReadingValid &&
                  millis() -
                          heaterReadingAt <
                      5000UL &&
                  heaterTemperature <
                      PAPERBRIQ_CHAMBER_TARGET_C +
                          5;

    bool accepted = false;

    if (edit) {

      accepted =
          verified &&
          production.adjust(
              output,
              run,
              productionSeconds,
              maximum,
              millis());

    } else {

      accepted =
          production.start(
              output,
              run,
              productionSeconds,
              maximum,
              verified,
              benchConditionsReady() &&
                  !pumpTestActive &&
                  !heaterTestActive &&
                  millis() -
                          lastMotorStoppedAtMs >=
                      2500UL &&
                  millis() -
                          lastGateCommandAtMs >=
                      2500UL,
              millis());

      if (accepted) {
        paperClearSeen = false;
      }

      if (accepted &&
          output != 'P') {

        thermal.begin(
            production,
            millis());
      }
    }

    serviceProduction();

    if (accepted) {

      emitAck(
          id,
          "COMPLETED",
          "production_applied");

      rememberRemoteResult(
          id,
          "COMPLETED",
          "production_applied");

    } else {

      failRemoteCommand(
          id,
          "production_blocked");
    }

    return;
  }

  const uint8_t seconds =
      timedCommandSeconds(
          action);

  if (seconds &&
      PAPERBRIQ_STANDALONE_BENCH) {

    if (heaterTestActive ||
        pumpTestActive) {

      failRemoteCommand(
          id,
          "output_busy");

      return;
    }

    const bool heater =
        action[0] == 'H';

    if (heater) {

      heaterDurationMs =
          seconds * 1000UL;

      startHeaterTest();

    } else {

      pumpDurationMs =
          seconds * 1000UL;

      startPumpTest();
    }

    if (heater
            ? heaterTestActive
            : pumpTestActive) {

      emitAck(
          id,
          "COMPLETED",
          "timed_run_started");

      rememberRemoteResult(
          id,
          "COMPLETED",
          "timed_run_started");

    } else {

      failRemoteCommand(
          id,
          "timed_run_blocked");
    }

    return;
  }

  if (strcmp(
          action,
          "SELF_TEST") == 0) {

    if (startComponentTest()) {

      emitAck(
          id,
          "COMPLETED",
          "test_started");

      rememberRemoteResult(
          id,
          "COMPLETED",
          "test_started");

    } else {

      failRemoteCommand(
          id,
          "test_rejected");
    }

    return;
  }

  if (strcmp(
          action,
          "STOP") == 0) {

    latchSoftwareStop();

    emitAck(
        id,
        "COMPLETED",
        "stopped");

    rememberRemoteResult(
        id,
        "COMPLETED",
        "stopped");

    return;
  }

  if (strcmp(
          action,
          "RESET") == 0) {

    if (armMotorTest()) {

      emitAck(
          id,
          "COMPLETED",
          "armed");

      rememberRemoteResult(
          id,
          "COMPLETED",
          "armed");

    } else {

      failRemoteCommand(
          id,
          lastArmRejectReason);
    }

    return;
  }

  if (strcmp(
          action,
          "START") == 0) {

    if (softwareStopLatched) {

      failRemoteCommand(
          id,
          "not_armed");

    } else if (
        automaticProcessIsActive() ||
        runState != STATE_IDLE) {

      failRemoteCommand(
          id,
          "busy");

    } else if (
        startAutomaticProcess()) {

      emitAck(
          id,
          "COMPLETED",
          "started");

      rememberRemoteResult(
          id,
          "COMPLETED",
          "started");

    } else {

      failRemoteCommand(
          id,
          "start_rejected");
    }

    return;
  }

  failRemoteCommand(
      id,
      "bad_command");
}

// ============================================================
// UART FAULT
// ============================================================

void bridgeProtocolFault(
    const __FlashStringHelper *message) {

  const uint8_t partialLength =
      bridgeLineLength;

  bridgeLineLength = 0;

  latchSoftwareStop();

  Serial.print(
      F("UART RX partial ("));

  Serial.print(
      static_cast<unsigned int>(
          partialLength));

  Serial.print(
      F(" bytes): ["));

  for (uint8_t i = 0;
       i < partialLength;
       ++i) {

    const uint8_t c =
        static_cast<uint8_t>(
            bridgeLine[i]);

    Serial.print(
        c >= 32 &&
                c <= 126
            ? static_cast<char>(
                  c)
            : '?');
  }

  Serial.println(
      F("]"));

  emitEvent(
      reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_FAULT),
      reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_error),
      reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_controller),
      reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_fault),
      message);
}

// ============================================================
// DHT CHECK
// ============================================================

void requestDhtCheck() {

  if (dhtCheckPending) {

    USB_LOG.println(
        F("DHT22: waiting for ESP32 reply"));

    return;
  }

  if (!thermal.heating(
          production)) {

    heaterReadingValid = false;
  }

  dhtCheckPending = true;

  dhtCheckSentAt =
      millis();

  ++dhtCheckSequence;

  espLink.print(
      reinterpret_cast<const __FlashStringHelper *>(PB_SHARED_207));

  espLink.println(
      dhtCheckSequence);

  USB_LOG.println(
      F("DHT22 GPIO27"));
}

void handleDhtReply(
    char *line) {

  strtok(
      line,
      "|");

  char *id =
      strtok(
          nullptr,
          "|");

  char *status =
      strtok(
          nullptr,
          "|");

  char *temperature =
      strtok(
          nullptr,
          "|");

  char *humidity =
      strtok(
          nullptr,
          "|");

  char *extra =
      strtok(
          nullptr,
          "|");

  if (!dhtCheckPending ||
      millis() -
              dhtCheckSentAt >=
          10000UL ||
      !id ||
      !status) {

    return;
  }

  char *end;

  const unsigned long sequence =
      strtoul(
          id,
          &end,
          10);

  if (*end ||
      sequence !=
          dhtCheckSequence) {

    return;
  }

  if (strcmp(
          status,
          "ERROR") == 0 &&
      !temperature) {

    heaterReadingValid = false;

    dhtCheckPending = false;

    USB_LOG.println(
        F("DHT22: no fresh valid reading"));

    return;
  }

  if (strcmp(
          status,
          "OK") != 0 ||
      !temperature ||
      !humidity ||
      extra) {

    return;
  }

  const double t =
      strtod(
          temperature,
          &end);

  if (*end ||
      !isfinite(t) ||
      t < -40 ||
      t > 80) {

    return;
  }

  const double h =
      strtod(
          humidity,
          &end);

  if (*end ||
      !isfinite(h) ||
      h < 0 ||
      h > 100) {

    return;
  }

  heaterTemperature =
      t;

  heaterReadingAt =
      millis();

  heaterReadingValid =
      true;

  dhtCheckPending =
      false;

  USB_LOG.print(
      F("Chamber DHT22: "));

  USB_LOG.print(
      temperature);

  USB_LOG.print(
      F(" C, "));

  USB_LOG.print(
      humidity);

  USB_LOG.println(
      F(" %RH (ESP32 reading)"));
}

// ============================================================
// ESP32 BRIDGE
// ============================================================

static bool __attribute__((noinline)) hasDualPaperLayout(const char *frame) {
  return !strncmp(frame, "IR2|", 4) && frame[5] == '|';
}

void serviceBridgeCommands() {

  // Yield to the stop checks even if UART input never goes idle.
  for (uint8_t remaining = BRIDGE_LINE_CAPACITY;
       remaining && espLink.available() > 0; --remaining) {

    const char c =
        static_cast<char>(
            espLink.read());

    bridgeLastByteAtMs =
        millis();

    if (discardOverflowedLine) {
      if (c == '\n') discardOverflowedLine = false;
      continue;
    }

    if (c == '\n') {

      bridgeLine[
          bridgeLineLength] = '\0';

      if (bridgeLineLength == 15 && hasDualPaperLayout(bridgeLine) &&
          hasDualPaperLayout(bridgeLine + 8)) {
        // Corrupted separator between two sensor reports. Neither reading is fresh.
      } else if (bridgeLineLength == 7 && hasDualPaperLayout(bridgeLine)) {
        if ((bridgeLine[4] & ~1) != '0' || (bridgeLine[6] & ~1) != '0') {
          bridgeLineLength = 0;
          continue; // Ignore damaged telemetry without refreshing the sensor heartbeat.
        }
        paperOutletDetected = bridgeLine[6] == '1';
        const unsigned long receivedAt = millis();
        if (receivedAt - dualPaperAt >= 1500UL) inletClearForDose = false;
        haveDualPaper = true;
        dualPaperAt = receivedAt;
        const bool occupied = bridgeLine[4] == '1' || paperOutletDetected;
        if (occupied || !paperBothClear) lastPaperOccupiedAt = receivedAt;
        paperBothClear = !occupied;
        handlePaperSignal(bridgeLine[4]);
        const bool inletClear = bridgeLine[4] == '0';
        if (inletClear && !inletClearForDose) inletClearedAt = receivedAt;
        if (!inletClear && !localLoadPaper &&
            localCycle.phase == StandaloneCycle::FORMING && !localCycle.fillOnly) {
          if (automaticProcessState == PROCESS_WAITING_FOR_NEXT_BATCH && inletClearForDose) {
            localLoadPaper = true;
          } else if (automaticProcessState == PROCESS_SHREDDING_AND_MIXING ||
                     automaticProcessState == PROCESS_MIXER_FINISHING) {
            localLoadPaper = true;
            localCycle.startWater(receivedAt);
          }
        }
        inletClearForDose = inletClear;
      } else if (bridgeLineLength == 4 &&
          strncmp(
              bridgeLine,
              "IR|",
              3) == 0 &&
          (bridgeLine[3] == '0' ||
           bridgeLine[3] == '1')) {

        handlePaperSignal(
            bridgeLine[3]);

      } else if (
          strncmp(
              bridgeLine,
              "DHT|",
              4) == 0) {

        handleDhtReply(
            bridgeLine);

      } else if (
          bridgeLineLength > 0) {

        handleRemoteCommandFrame(
            bridgeLine);
      }

      bridgeLineLength = 0;

    } else if (
        c != '\r') {

      if (bridgeLineLength + 1 <
          BRIDGE_LINE_CAPACITY) {

        bridgeLine[
            bridgeLineLength++] = c;

      } else {

        bridgeProtocolFault(
            F("UART frame overflow"));

        discardOverflowedLine = true;
      }
    }
  }

  if (bridgeLineLength > 0 &&
      millis() -
              bridgeLastByteAtMs >=
          BRIDGE_FRAME_TIMEOUT_MS) {

    bridgeProtocolFault(
        F("UART frame timeout"));
  }

  if (dhtCheckPending &&
      millis() -
              dhtCheckSentAt >=
          10000UL) {

    dhtCheckPending = false;

    USB_LOG.println(
        F("DHT22: ESP32 reply timed out"));
  }
}

// ============================================================
// BENCH CONDITIONS
// ============================================================

bool benchConditionsReady(bool allowArmedPump) {
  if (positioningStep) return false;

  return
      !localCycle.active() &&
      (softwareStopLatched || allowArmedPump) &&
      !diagnosticActive &&
      !statusSelfTestActive &&
      runState == STATE_IDLE &&
      !automaticProcessIsActive() &&
      statusIoOnline &&
      chamber.ready() &&
      controlIoOnline &&
      !stopButton.rawActive &&
      !stopButton.stableActive &&
      !emergencyButton.rawActive &&
      !emergencyButton.stableActive;
}

// ============================================================
// TEMPERATURE SERVICE
// ============================================================

void serviceChamberTemperature() {

  if (localCycle.active()) {
    return;
  }

  if (!PAPERBRIQ_HEATER_FAN_CONFIRMED ||
      heaterTestActive ||
      dhtCheckPending ||
      // Keep background temperature requests clear of queued command frames.
      // Let buffered input and any frame already in progress finish first.
      bridgeLineLength != 0 || espLink.available() > 0 ||
      millis() - bridgeLastByteAtMs < 20UL ||
      millis() -
              dhtCheckSentAt <
          2500UL) {

    return;
  }

  dhtCheckPending = true;

  dhtCheckSentAt =
      millis();

  ++dhtCheckSequence;

  espLink.print(
      reinterpret_cast<const __FlashStringHelper *>(PB_SHARED_207));

  espLink.println(
      dhtCheckSequence);
}

// ============================================================
// PRODUCTION SERVICE
// ============================================================

void serviceProduction() {

  if (localCycle.active()) {
    return;
  }

  if (!production.active()) {

    if (productionPumpWasOn &&
        production.phase ==
            ProductionTimer::FINISHED &&
        automaticProcessState ==
            PROCESS_MIXER_FINISHING) {

      processStateStartedAtMs =
          millis();
    }

    productionPumpWasOn = false;

    if (production.run) {

      if (!pumpTestActive) {

        digitalWrite(
            PUMP_ENABLE_PIN,
            LOW);
      }

      if (!heaterTestActive) {

        digitalWrite(
            HEATER_ENABLE_PIN,
            LOW);
      }
    }

    if (thermal.stage !=
        ThermalCycle::FAULT) {

      thermal.stage =
          ThermalCycle::IDLE;
    }

    return;
  }

  const bool safe =
      chamber.ready() &&
      statusIoOnline &&
      controlIoOnline &&
      !stopButton.rawActive &&
      !stopButton.stableActive &&
      !emergencyButton.rawActive &&
      !emergencyButton.stableActive &&
      !diagnosticActive &&
      !statusSelfTestActive &&
      !pumpTestActive &&
      !heaterTestActive;

  const bool mixing =
      automaticProcessState ==
          PROCESS_SHREDDING_AND_MIXING ||
      automaticProcessState ==
          PROCESS_MIXER_FINISHING;

  if (production.kind == 'P') {

    production.tick(
        safe &&
            (!production.on() ||
             (mixing &&
              !softwareStopLatched)),
        millis());

  } else {

    thermal.tick(
        production,
        safe &&
            PAPERBRIQ_HEATER_FAN_CONFIRMED &&
            softwareStopLatched &&
            !automaticProcessIsActive() &&
            runState == STATE_IDLE,
        heaterReadingValid &&
            millis() -
                    heaterReadingAt <
                5000UL,
        heaterTemperature,
        PAPERBRIQ_CHAMBER_TARGET_C,
        PAPERBRIQ_PREHEAT_MAX_SECONDS *
            1000UL,
        millis());
  }

  if (productionPumpWasOn &&
      production.phase ==
          ProductionTimer::FINISHED &&
      automaticProcessState ==
          PROCESS_MIXER_FINISHING) {

    processStateStartedAtMs =
        millis();
  }

  productionPumpWasOn =
      production.kind == 'P' &&
      production.on();

  digitalWrite(
      PUMP_ENABLE_PIN,
      production.kind == 'P' &&
              production.on()
          ? HIGH
          : LOW);

  digitalWrite(
      HEATER_ENABLE_PIN,
      thermal.heating(
          production)
          ? HIGH
          : LOW);
}

// ============================================================
// PAPER SIGNAL
// ============================================================

void handlePaperSignal(
    char value) {

  const unsigned long now =
      millis();

  serviceAutomaticProcess();

  const bool edge =
      value == '1' &&
      paperClearSeen &&
      now -
              paperSignalAt <
          1500UL;

  paperDetected =
      value == '1';

  paperClearSeen =
      value == '0';

  paperSignalAt =
      now;

  if (edge &&
      automaticProcessState ==
          PROCESS_WAITING_FOR_PAPER &&
      !softwareStopLatched &&
      chamberMotionReady() &&
      controlIoOnline &&
      !stopButton.rawActive &&
      !stopButton.stableActive &&
      !emergencyButton.rawActive &&
      !emergencyButton.stableActive &&
      (irShredTest || (production.kind == 'P' &&
      production.phase ==
          ProductionTimer::WAITING))) {

    if (irShredTest) {
      irShredTest = false;
      sensorControlledProcess = false;
      automaticProcessState = PROCESS_IDLE;
      requestPulse(SHREDDER_FORWARD, 3000UL);
    } else {
      startPaperProcessing();
    }
  }

  production.detectPaper(
      edge,
      automaticProcessState ==
              PROCESS_SHREDDING_AND_MIXING ||
          automaticProcessState ==
              PROCESS_MIXER_FINISHING,
      now);

  serviceProduction();

  serviceAutomaticProcess();
}

// ============================================================
// HEATER CONDITIONS
// ============================================================

bool heaterConditionsReady() {

  return
      !pumpTestActive &&
      benchConditionsReady() &&
      (PAPERBRIQ_STANDALONE_BENCH
           ? (!heaterReadingValid ||
              heaterTemperature < 40.0f)
           : (heaterReadingValid &&
              millis() -
                      heaterReadingAt <
                  5000UL &&
              heaterTemperature <
                  40.0f));
}

// ============================================================
// PUMP TEST
// ============================================================

void stopPumpTest() {

  digitalWrite(
      PUMP_ENABLE_PIN,
      LOW);

  if (pumpTestActive) {

    Serial.println(
        F("D3 OFF"));
  }

  pumpTestActive = false;
}

void servicePumpTest() {

  if (pumpTestActive &&
      (millis() -
               pumpTestAt >=
           pumpDurationMs ||
       heaterTestActive ||
       !benchConditionsReady(true))) {

    stopPumpTest();
  }
}

void startPumpTest() {

  if (production.active()) {
    return;
  }

  if (!PAPERBRIQ_PUMP_VERIFIED &&
      !PAPERBRIQ_STANDALONE_BENCH) {

    Serial.println(
        F("PUMP: wiring"));

    return;
  }

  if (pumpTestActive) {
    return;
  }

  serviceChamber(true);

  serviceControlButtons();

  if (heaterTestActive ||
      !benchConditionsReady(true) ||
      millis() -
              lastMotorStoppedAtMs <
          2500UL ||
      millis() -
              lastGateCommandAtMs <
          2500UL ||
      (pumpHasRun &&
       millis() -
               pumpTestAt <
           10000UL)) {

    Serial.println(
        F("Pump blocked"));

    return;
  }

  pumpTestAt =
      millis();

  pumpHasRun =
      true;

  pumpTestActive =
      true;

  digitalWrite(
      PUMP_ENABLE_PIN,
      HIGH);

  Serial.print(
      F("PUMP ms="));

  Serial.println(
      pumpDurationMs);
}

// ============================================================
// HEATER TEST
// ============================================================

void stopHeaterTest() {

  digitalWrite(
      HEATER_ENABLE_PIN,
      LOW);

  if (heaterTestActive) {

    heaterReadingValid =
        false;

    USB_LOG.println(
        F("HEATER: D4 OFF"));
  }

  heaterTestActive =
      false;
}

void serviceHeaterTest() {

  if (heaterTestActive &&
      (millis() -
               heaterTestAt >=
           heaterDurationMs ||
       !heaterConditionsReady())) {

    stopHeaterTest();

    heaterReadingValid =
        false;
  }
}

void startHeaterTest() {

  if (production.active()) {
    return;
  }

  if (!PAPERBRIQ_HEATER_VERIFIED &&
      !PAPERBRIQ_STANDALONE_BENCH) {

    USB_LOG.println(
        F("HEATER BLOCKED: wiring/cutoff verification pending"));

    return;
  }

  if (heaterTestActive) {
    return;
  }

  if (PAPERBRIQ_STANDALONE_BENCH &&
      heaterBenchUsed &&
      millis() -
              heaterTestAt <
          60000UL) {

    USB_LOG.println(
        F("HEATER: wait 60s between bench starts"));

    return;
  }

  serviceChamber(true);

  serviceControlButtons();

  if (!heaterConditionsReady() ||
      millis() -
              lastMotorStoppedAtMs <
          2500UL ||
      millis() -
              lastGateCommandAtMs <
          2500UL) {

    USB_LOG.println(
        F("HEATER BLOCKED: stop/door/contacts/temperature"));

    return;
  }

  heaterTestAt =
      millis();

  heaterBenchUsed =
      true;

  heaterTestActive =
      true;

  digitalWrite(
      HEATER_ENABLE_PIN,
      HIGH);

  USB_LOG.print(
      F("HEATER: D4 ON, milliseconds="));

  USB_LOG.println(
      heaterDurationMs);
}

// ============================================================
// LOCAL STANDALONE CYCLE
// ============================================================

static void __attribute__((noinline)) continueLocalBatch(unsigned long now) {
  localCycle.nextBatch();
  automaticProcessState = PROCESS_WAITING_TO_CLOSE_GATE;
  processStateStartedAtMs = now;
}

bool __attribute__((noinline)) startStandaloneCycle(bool fillOnly, uint8_t firstLoad) {

  const bool continuing = localCycle.active();
  serviceChamber(true);
  serviceControlButtons();
  serviceStandaloneCycle(); // Expire a session before accepting another Start.
  if (continuing && !localCycle.active()) return false;

  if (firstLoad && !localCycle.canForm()) return false;
  if (fillOnly && localCycle.active()) return false;

  if (localCycle.phase == StandaloneCycle::FORMING &&
      automaticProcessState == PROCESS_WAITING_FOR_NEXT_BATCH) {
    const unsigned long now = millis();
    if (now - lastMotorStoppedAtMs < DIRECTION_CHANGE_PAUSE_MS) return false;
    // Safety was re-sampled above. Session deadline remains unchanged.
    continueLocalBatch(now);
    return true;
  }

  if (!PAPERBRIQ_PUMP_VERIFIED ||
      (!fillOnly && (!PAPERBRIQ_HEATER_VERIFIED ||
                     !PAPERBRIQ_HEATER_FAN_CONFIRMED ||
                     PAPERBRIQ_LOCAL_HEAT_MS == 0 ||
                     PAPERBRIQ_LOCAL_HEAT_MS > 600000UL))) {

    Serial.println(
        F("Fan/cutoff"));

    return false;
  }

  if (!startAutomaticProcess(
          true, true, fillOnly)) {

    Serial.println(
        reinterpret_cast<const __FlashStringHelper *>(PB_BLOCKED));

    return false;
  }

  localCycle.batch = firstLoad;
  serviceStandaloneCycle();
  return localCycle.active();
}

void serviceStandaloneCycle() {

  if (!localCycle.active()) {
    return;
  }

  const unsigned long now =
      millis();

  const bool fillWater = localCycle.fillOnly;
  if (fillWater && localCycle.pumping() && now - localCycle.pumpAt >= FILL_WATER_TIMEOUT_MS) {
    latchSoftwareStop();
    Serial.println(F("Water timeout; R"));
    return;
  }

  const bool safe =
      controlIoOnline &&
      statusIoOnline &&
      chamber.ready() &&
      !stopButton.rawActive &&
      !stopButton.stableActive &&
      !emergencyButton.rawActive &&
      !emergencyButton.stableActive;

  if (!safe || softwareStopLatched ||
      (localCycle.phase ==
           StandaloneCycle::FORMING &&
       !automaticProcessIsActive()) ||
      (localCycle.heating() &&
       (automaticProcessIsActive() ||
        runState != STATE_IDLE))) {

    // Capture the cause before stopping resets the session and chamber outputs.
    const __FlashStringHelper *reason =
        !controlIoOnline ? F("Buttons 0x20 lost") :
        !statusIoOnline || !chamber.online ? F("Chamber 0x24 lost") :
        !chamber.doorClosed ? F("Door P2 open") :
        stopButton.rawActive || stopButton.stableActive ? F("Stop contact open") :
        emergencyButton.rawActive || emergencyButton.stableActive ? F("E-stop contact open") :
        chamber.fanOn || chamber.unlockOn ? F("Fan/unlock test active") :
        softwareStopLatched ? F("Stop latched") : F("Session state mismatch");
    latchSoftwareStop();
    emitEvent(reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_FAULT), reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_error), reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_controller), reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_fault), reason);

    return;
  }

  if (localCycle.expired(now)) {
    latchSoftwareStop();
    Serial.println(F("HEAT TIMEOUT; R"));
    return;
  }

  const bool paperLinkFresh = now - dualPaperAt < 1500UL;
  if (automaticProcessState == PROCESS_WAITING_FOR_NEXT_BATCH && localLoadPaper) {
    if (!paperLinkFresh) localLoadPaper = false;
    else if (now - lastMotorStoppedAtMs >= DIRECTION_CHANGE_PAUSE_MS) continueLocalBatch(now);
  }

  if (localCycle.phase == StandaloneCycle::HEATING &&
      now - localCycle.heatAt >= PAPERBRIQ_LOCAL_HEAT_MS) {
    localCycle.phase = StandaloneCycle::READY;
    localCycle.heatAt = now;
    Serial.println(F("WARM; W/X"));
  }

  if (!fillWater) {
    const unsigned long waterElapsed = now - localCycle.pumpAt;
    if (!paperLinkFresh || waterElapsed >= LOCAL_MIX_WATER_MS ||
        (!localMixWater && waterElapsed >= LOCAL_FEED_WATER_MS)) {
      localCycle.stopWater();
    }
    if (!localMixWater && localLoadPaper && paperLinkFresh && inletClearForDose &&
        now - inletClearedAt >= PAPER_CLEAR_MS &&
        automaticProcessState >= PROCESS_MIXER_FINISHING &&
        automaticProcessState <= PROCESS_WAITING_TO_OPEN_GATE) {
      localMixWater = true;
      localCycle.pumpStarted = true;
      localCycle.pumpAt = now;
    }
  }
  const bool pumpOn = localCycle.pumping() && (fillWater || localMixWater || !inletClearForDose);

  if (localCycle.pumpWasOn != pumpOn) {
    Serial.println(pumpOn ? F("PUMP ON") : F("PUMP OFF"));
  }

  localCycle.pumpWasOn =
      pumpOn;

  digitalWrite(
      PUMP_ENABLE_PIN,
      pumpOn
          ? HIGH
          : LOW);

  digitalWrite(
      HEATER_ENABLE_PIN,
      localCycle.heating()
          ? HIGH
          : LOW);
}

// ============================================================
// SERIAL COMMANDS
// ============================================================

void advancePositioning() {
  if (softwareStopLatched || !chamberMotionReady() || !controlIoOnline ||
      stopButton.rawActive || emergencyButton.rawActive ||
      stopButton.stableActive || emergencyButton.stableActive ||
      localCycle.active() || production.active() || diagnosticActive ||
      statusSelfTestActive || automaticProcessIsActive()) {
    Serial.println(reinterpret_cast<const __FlashStringHelper *>(PB_BLOCKED)); return;
  }
  if (runState != STATE_IDLE ||
      millis() - lastMotorStoppedAtMs < DIRECTION_CHANGE_PAUSE_MS ||
      millis() - lastGateCommandAtMs < DIRECTION_CHANGE_PAUSE_MS) {
    Serial.println(F("Z: 2.5s")); return;
  }
  ++positioningStep;
  Serial.print(F("Z ")); Serial.println(positioningStep);
  switch (positioningStep) {
    case 1: setMolderAngle(MOLDER_CLOSED_ANGLE, reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_CLOSED)); break;
    case 2: requestPulse(PRESSER_EXTEND, PROCESS_PRESSER_EXTEND_MS); break;
    case 3: requestPulse(PRESSER_RETRACT, PROCESS_PRESSER_RETRACT_MS); break;
    case 4: setMolderAngle(MOLDER_OPEN_ANGLE, reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_OPEN)); break;
    case 5: requestPulse(PUSHER_FORWARD, PROCESS_PUSHER_EXTEND_MS); break;
    case 6: requestPulse(PUSHER_REVERSE, PROCESS_PUSHER_RETRACT_MS); break;
    case 7: setMolderAngle(MOLDER_CLOSED_ANGLE, reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_CLOSED)); break;
    default: positioningStep = 0; break;
  }
}

void servicePositioning() {
  if (positioningStep == 7 && millis() - lastGateCommandAtMs >= MOLDER_TRAVEL_MS) {
    positioningStep = 0;
    softwareStopLatched = true;
    softwareStopLatchedAtMs = millis();
    Serial.println(F("Z done"));
  }
}

void serviceSerialCommands() {

  for (uint8_t remaining = 16; remaining && Serial.available() > 0; --remaining) {

    char command =
        Serial.read();

    if (command == '1' || command == '5' || command == '6') {
      Serial.print(F("USB RX: "));
      Serial.println(command);
    }

    if (positioningStep && strchr("zZxXhHiI\r\n \t", command) == nullptr) {
      Serial.println(F("Z=next X=off"));
      continue;
    }

    uint8_t firstLoad = 0;
    if (command == '8' || command == '9') {
      firstLoad = command - '7';
      command = 'W';
    }

    if (localCycle.active() &&
        !((localCycle.canForm() || automaticProcessState == PROCESS_WAITING_FOR_NEXT_BATCH) &&
          (command == 'w' || command == 'W')) &&
        strchr(
            "xXhHiIsS\r\n \t",
            command) == nullptr) {

      Serial.println(
          F("Busy"));

      continue;
    }

    switch (command) {
      case 'a':
      case 'A':
        if (PAPERBRIQ_ESP_LINK_ENABLED && startAutomaticProcess(true)) {
          irShredTest = true;
          sensorControlledProcess = true;
        }
        break;
      case 'z': case 'Z': advancePositioning(); break;

      case 'j':
      case 'J':

        if (!heaterTestActive) {

          heaterDurationMs =
              HEATER_TEST_PULSE_MS;

          startHeaterTest();
        }

        break;

      // FIXED:
      // Y does NOT overwrite pumpDurationMs anymore.
      case 'y':
      case 'Y':

        if (!pumpTestActive) {

          startPumpTest();
        }

        break;

      case 's':
      case 'S':

        if (PAPERBRIQ_ESP_LINK_ENABLED) requestDhtCheck();
        else Serial.println(reinterpret_cast<const __FlashStringHelper *>(PB_SHARED_247));

        break;

      case 'q':
      case 'Q':

        if (!startComponentTest()) {

          USB_LOG.println(
              F("TEST REJECTED: close door, Reset, and stop other actions"));
        }

        break;

      case 'i':
      case 'I':

        serviceChamber(true);

        printChamberStatus();

        break;

      case 'v':
      case 'V':

        runChamberCommand(
            "FAN_TEST");

        break;

      case 'n':
      case 'N':

        runChamberCommand(
            "FAN_OFF");

        break;

      case 'u':
      case 'U':

        runChamberCommand(
            "UNLOCK");

        break;

      case 'k':
      case 'K':

        runChamberCommand(
            "LOCK");

        break;

      case '1':

        requestManualPulse(
            SHREDDER_FORWARD);

        break;

      case '2':

        requestManualPulse(
            SHREDDER_REVERSE);

        break;

      case '3':

        requestManualPulse(
            MIXER_FORWARD);

        break;

      case '4':

        requestManualPulse(
            MIXER_REVERSE);

        break;

      case 'e':
      case 'E':

        requestManualPulse(
            PRESSER_EXTEND);

        break;

      case 't':
      case 'T':

        requestManualPulse(
            PRESSER_RETRACT);

        break;

      case 'f':
      case 'F':

        requestManualPulse(
            PUSHER_FORWARD);

        break;

      case 'b':
      case 'B':

        requestManualPulse(
            PUSHER_REVERSE);

        break;

      case '5':

        moveMolder(
            MOLDER_OPEN_ANGLE,
            reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_OPEN));

        break;

      case '6':

        moveMolder(
            MOLDER_CLOSED_ANGLE,
            reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_CLOSED));

        break;

      case '7':

        if (automaticProcessIsActive() ||
            diagnosticActive) {

          USB_LOG.println(
              F("MOLDER RELEASE REJECTED: process is running"));

        } else if (
            runState == STATE_IDLE) {

          releaseMolderServo(
              true);

        } else {

          USB_LOG.println(
              F("MOLDER RELEASE REJECTED: stop the motor first"));
        }

        break;

      case 'p':
      case 'P':

        startAutomaticProcess();

        break;

      // G:
      // Full forming movement without ESP32/IR wait.
      // Pump and heater remain off.
      case 'g':
      case 'G':

        if (startAutomaticProcess(
                true)) {

          Serial.println(
              F("G: unloaded"));

        } else {

          Serial.println(
              reinterpret_cast<const __FlashStringHelper *>(PB_BLOCKED));
        }

        break;

      case 'w':
      case 'W':
        startStandaloneCycle(false, firstLoad);
        break;

      case 'm':
      case 'M':
        startStandaloneCycle(true);
        break;

      case 'o':
      case 'O':

        moveMixerGate(
            MIXER_GATE_OPEN_ANGLE,
            reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_OPEN));

        break;

      case 'c':
      case 'C':

        moveMixerGate(
            MIXER_GATE_CLOSED_ANGLE,
            reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_CLOSED));

        break;

      case 'd':
      case 'D':

        if (automaticProcessIsActive() ||
            diagnosticActive) {

          USB_LOG.println(
              F("SERVO RELEASE REJECTED: process is running"));

        } else if (
            runState == STATE_IDLE) {

          releaseMixerGateServo(
              true);

        } else {

          USB_LOG.println(
              F("SERVO RELEASE REJECTED: stop the motor first"));
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

        break;

      default:

        USB_LOG.print(
            F("Unknown command: "));

        USB_LOG.println(
            command);

        USB_LOG.println(
            F("Send H to show the command list."));

        break;
    }
  }
}

// ============================================================
// SETUP
// ============================================================

void setup() {

  // ----------------------------------------------------------
  // Set safe LOW states before switching pins to OUTPUT.
  // ----------------------------------------------------------

  digitalWrite(
      HEATER_ENABLE_PIN,
      LOW);

  pinMode(
      HEATER_ENABLE_PIN,
      OUTPUT);

  digitalWrite(
      PUMP_ENABLE_PIN,
      LOW);

  pinMode(
      PUMP_ENABLE_PIN,
      OUTPUT);

  digitalWrite(
      SHREDDER_RPWM,
      LOW);

  digitalWrite(
      SHREDDER_LPWM,
      LOW);

  digitalWrite(
      MIXER_RPWM,
      LOW);

  digitalWrite(
      MIXER_LPWM,
      LOW);

  digitalWrite(
      PRESSER_RPWM,
      LOW);

  digitalWrite(
      PRESSER_LPWM,
      LOW);

  digitalWrite(
      PUSHER_IN3,
      LOW);

  digitalWrite(
      PUSHER_IN4,
      LOW);

  digitalWrite(
      MIXER_GATE_SERVO_PIN,
      LOW);

  digitalWrite(
      MOLDER_SERVO_PIN,
      LOW);

  // ----------------------------------------------------------
  // Motor outputs
  // ----------------------------------------------------------

  pinMode(
      SHREDDER_RPWM,
      OUTPUT);

  pinMode(
      SHREDDER_LPWM,
      OUTPUT);

  pinMode(
      MIXER_RPWM,
      OUTPUT);

  pinMode(
      MIXER_LPWM,
      OUTPUT);

  pinMode(
      PRESSER_RPWM,
      OUTPUT);

  pinMode(
      PRESSER_LPWM,
      OUTPUT);

  pinMode(
      PUSHER_IN3,
      OUTPUT);

  pinMode(
      PUSHER_IN4,
      OUTPUT);

  pinMode(
      MIXER_GATE_SERVO_PIN,
      OUTPUT);

  pinMode(
      MOLDER_SERVO_PIN,
      OUTPUT);

  // ----------------------------------------------------------
  // Ensure everything is stopped.
  // ----------------------------------------------------------

  stopAllMotors(false);

  releaseMixerGateServo(false);

  releaseMolderServo(false);

  // ----------------------------------------------------------
  // Serial
  // ----------------------------------------------------------

  Serial.begin(9600);

  if (PAPERBRIQ_ESP_LINK_ENABLED) espLink.begin(ESP_LINK_BAUD);
  else {
    if (PAPERBRIQ_FILL_IR_ENABLED) {
      pinMode(FILL_IR_IN_PIN, INPUT_PULLUP);
      pinMode(FILL_IR_OUT_PIN, INPUT_PULLUP);
    }
    Serial.println(reinterpret_cast<const __FlashStringHelper *>(PB_SHARED_247));
  }

  USB_LOG.println(
      F("Uno link diagnostic: RX=A2, TX=A3, baud=9600"));

  // ----------------------------------------------------------
  // I2C
  // ----------------------------------------------------------

  Wire.begin();

  Wire.setWireTimeout(
      25000,
      true);

  delay(500);

  initializeControlIo();

  initializeStatusIo();

  serviceChamber(true);

  // ----------------------------------------------------------
  // User information
  // ----------------------------------------------------------

  printHelp();

  emitEvent(
      F("CONTROLLER_READY"),
      reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_info),
      reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_controller),
      reinterpret_cast<const __FlashStringHelper *>(PB_TEXT_stopped),
      F("Ready"));
}

// ============================================================
// LOOP
// ============================================================

static void __attribute__((noinline)) serviceProcessPass() {
  serviceChamber();

  serviceControlButtons();

  servicePumpTest();

  serviceHeaterTest();

  serviceStatusLights();

  if (PAPERBRIQ_ESP_LINK_ENABLED) serviceBridgeCommands();

  serviceMotorState();

  serviceComponentTest();

  serviceAutomaticProcess();

  serviceStandaloneCycle();
}

void loop() {
  serviceControlButtons();
  serviceSerialCommands();

  servicePositioning();
  serviceStandaloneCycle();

  if (PAPERBRIQ_ESP_LINK_ENABLED) serviceChamberTemperature();
  serviceProduction();
  servicePumpTest();
  serviceHeaterTest();
  serviceProcessPass();
  serviceProduction();
  serviceProcessPass();
}
