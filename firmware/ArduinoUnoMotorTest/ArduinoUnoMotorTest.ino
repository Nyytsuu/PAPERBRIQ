#include <Arduino.h>
#include <Servo.h>

// PaperBriq Arduino Uno shredder, mixer, and mixer-gate bench test.
// Test only unloaded, securely clamped motors with a fused 12 V supply and
// a reachable physical emergency disconnect.

// BTS7960 signal pins used by this test.
const uint8_t SHREDDER_RPWM = 5;  // Hardware PWM
const uint8_t SHREDDER_LPWM = 6;  // Hardware PWM
const uint8_t MIXER_RPWM = 3;     // Hardware PWM; move from D8 to D3
const uint8_t MIXER_LPWM = 11;    // Hardware PWM; move from D9 to D11

// Mixer gate servo signal. A0 works as digital pin 14 on an Arduino Uno.
const uint8_t MIXER_GATE_SERVO_PIN = A0;
// Molder servo signal. A1 works as digital pin 15 on an Arduino Uno.
const uint8_t MOLDER_SERVO_PIN = A1;

// Adjust gradually to match the physical gate linkage. Swap the values if
// the movement is reversed. Stop if the servo buzzes or hits a hard stop.
const uint8_t MIXER_GATE_CLOSED_ANGLE = 52;
const uint8_t MIXER_GATE_OPEN_ANGLE = 130;

// Initial conservative molder angles. Adjust gradually for its linkage and
// swap the values if its physical open/close direction is reversed.
const uint8_t MOLDER_CLOSED_ANGLE = 90;
const uint8_t MOLDER_OPEN_ANGLE = 120;
// Independent motor speeds. PWM range: 0-255.
// 77 = 30%, 102 = 40%, 128 = 50%, 191 = 75%, 255 = 100%.
const uint8_t SHREDDER_FORWARD_DUTY = 102;  // 40%
const uint8_t SHREDDER_REVERSE_DUTY = 77;   // 30%
const uint8_t MIXER_FORWARD_DUTY = 102;     // 40%
const uint8_t MIXER_REVERSE_DUTY = 77;      // 30%

// Bench-test time limits. Every motor pulse stops automatically.
const unsigned long SHREDDER_PULSE_MS = 1200;
const unsigned long MIXER_PULSE_MS = 1200;

// Automatic demonstration process timings. Start with P after arming with R.
// Increase these only after the complete unloaded sequence works safely.
const unsigned long PROCESS_SHREDDER_RUN_MS = 5000;
const unsigned long PROCESS_MIXER_RUN_MS = 10000;
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
  MIXER_REVERSE
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
  PROCESS_SHREDDING,
  PROCESS_MIXING,
  PROCESS_WAITING_TO_OPEN_GATE,
  PROCESS_WAITING_FOR_GATE_TO_OPEN
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
      return MIXER_FORWARD_DUTY;
    case MIXER_REVERSE:
      return MIXER_REVERSE_DUTY;
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

void moveMixerGate(uint8_t angle, const __FlashStringHelper *positionName) {
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
  stopAllMotors(false);
  releaseMixerGateServo(false);
  releaseMolderServo(false);
  automaticProcessState = PROCESS_IDLE;
  softwareStopLatched = true;
  softwareStopLatchedAtMs = millis();
  Serial.println(F("SOFTWARE STOP LATCHED: motors stopped, servos released"));
  Serial.println(F("Inspect the machine, then send R to arm again."));
}

void armMotorTest() {
  if (automaticProcessIsActive()) {
    Serial.println(F("ARM REJECTED: automatic process is already running"));
    return;
  }

  if (millis() - softwareStopLatchedAtMs < SOFTWARE_STOP_REARM_DELAY_MS) {
    Serial.println(F("ARM REJECTED: wait 1 second after X, then send R again"));
    return;
  }

  stopAllMotors(false);
  softwareStopLatched = false;
  Serial.println(F("MOTOR TEST ARMED: motor and gate commands enabled"));
}

void printHelp() {
  Serial.println();
  Serial.println(F("PaperBriq Arduino Uno actuator test ready"));
  Serial.println(F("R = arm/reset after a software stop"));
  Serial.println(F("1 = shredder forward pulse"));
  Serial.println(F("2 = shredder reverse pulse"));
  Serial.println(F("3 = mixer forward pulse"));
  Serial.println(F("4 = mixer reverse pulse"));
  Serial.println(F("P = run full process: close, shred, mix, open"));
  Serial.println(F("O = open mixer gate servo"));
  Serial.println(F("C = close mixer gate servo"));
  Serial.println(F("D = release mixer gate servo"));
  Serial.println(F("5 = open molder servo"));
  Serial.println(F("6 = close molder servo"));
  Serial.println(F("7 = release molder servo"));
  Serial.println(F("X = latch software stop and cancel movement"));
  Serial.println(F("H = show commands"));
  Serial.println(F("No actuator starts automatically; send R first"));
}

void requestPulse(Motion motion, unsigned long runDurationMs) {
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
  if (automaticProcessIsActive()) {
    Serial.println(F("MANUAL COMMAND REJECTED: automatic process is running"));
    return;
  }

  requestPulse(motion, motionDurationMs(motion));
}

void startRequestedMotion() {
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
      analogWrite(MIXER_RPWM, MIXER_FORWARD_DUTY);
      break;

    case MIXER_REVERSE:
      digitalWrite(MIXER_RPWM, LOW);
      analogWrite(MIXER_LPWM, MIXER_REVERSE_DUTY);
      break;

    default:
      stopAllMotors(false);
      return;
  }

  Serial.print(motionName(activeMotion));
  Serial.print(F(": running for "));
  Serial.print(activeRunDurationMs);
  Serial.println(F(" ms"));
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
  }
}

void startAutomaticProcess() {
  if (softwareStopLatched) {
    Serial.println(F("PROCESS REJECTED: send R to arm first"));
    return;
  }

  if (automaticProcessIsActive()) {
    Serial.println(F("PROCESS REJECTED: a process is already running"));
    return;
  }

  if (runState != STATE_IDLE) {
    Serial.println(F("PROCESS REJECTED: wait for the manual motor test to stop"));
    return;
  }

  stopAllMotors(false);
  automaticProcessState = PROCESS_WAITING_TO_CLOSE_GATE;
  processStateStartedAtMs = millis();

  Serial.println();
  Serial.println(F("AUTOMATIC PROCESS STARTED"));
  Serial.println(F("1/3 Waiting for motors, then closing mixer gate and molder"));
  Serial.print(F("Shred time: "));
  Serial.print(PROCESS_SHREDDER_RUN_MS);
  Serial.print(F(" ms | Mix time: "));
  Serial.print(PROCESS_MIXER_RUN_MS);
  Serial.println(F(" ms"));
}

void serviceAutomaticProcess() {
  if (softwareStopLatched) {
    automaticProcessState = PROCESS_IDLE;
    return;
  }

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
        Serial.println(F("2/3 Starting shredder"));
        requestPulse(SHREDDER_FORWARD, PROCESS_SHREDDER_RUN_MS);
        automaticProcessState = PROCESS_SHREDDING;
      }
      break;

    case PROCESS_SHREDDING:
      if (runState == STATE_IDLE) {
        Serial.println(F("Shredding complete; starting mixer after safety pause"));
        requestPulse(MIXER_FORWARD, PROCESS_MIXER_RUN_MS);
        automaticProcessState = PROCESS_MIXING;
      }
      break;

    case PROCESS_MIXING:
      if (runState == STATE_IDLE) {
        Serial.println(F("Mixing complete; waiting for mixer to stop"));
        automaticProcessState = PROCESS_WAITING_TO_OPEN_GATE;
        processStateStartedAtMs = nowMs;
      }
      break;

    case PROCESS_WAITING_TO_OPEN_GATE:
      if (nowMs - lastMotorStoppedAtMs >= DIRECTION_CHANGE_PAUSE_MS) {
        Serial.println(F("3/3 Opening mixer gate"));
        setMixerGateAngle(MIXER_GATE_OPEN_ANGLE, F("OPEN"));
        automaticProcessState = PROCESS_WAITING_FOR_GATE_TO_OPEN;
        processStateStartedAtMs = nowMs;
      }
      break;

    case PROCESS_WAITING_FOR_GATE_TO_OPEN:
      if (nowMs - processStateStartedAtMs >= MIXER_GATE_TRAVEL_MS) {
        automaticProcessState = PROCESS_IDLE;
        Serial.println(F("PROCESS COMPLETE: mixer gate open, molder closed"));
      }
      break;

    case PROCESS_IDLE:
    default:
      break;
  }
}

void serviceSerialCommands() {
  while (Serial.available() > 0) {
    const char command = Serial.read();

    switch (command) {
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
      case '5':
        moveMolder(MOLDER_OPEN_ANGLE, F("OPEN"));
        break;
      case '6':
        moveMolder(MOLDER_CLOSED_ANGLE, F("CLOSED"));
        break;
      case '7':
        if (automaticProcessIsActive()) {
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
        if (automaticProcessIsActive()) {
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
  digitalWrite(MIXER_GATE_SERVO_PIN, LOW);
  digitalWrite(MOLDER_SERVO_PIN, LOW);

  pinMode(SHREDDER_RPWM, OUTPUT);
  pinMode(SHREDDER_LPWM, OUTPUT);
  pinMode(MIXER_RPWM, OUTPUT);
  pinMode(MIXER_LPWM, OUTPUT);
  pinMode(MIXER_GATE_SERVO_PIN, OUTPUT);
  pinMode(MOLDER_SERVO_PIN, OUTPUT);
  stopAllMotors(false);
  releaseMixerGateServo(false);
  releaseMolderServo(false);

  Serial.begin(115200);
  delay(500);
  printHelp();
}

void loop() {
  serviceMotorState();
  serviceAutomaticProcess();
  serviceSerialCommands();
  serviceMotorState();
  serviceAutomaticProcess();
}
