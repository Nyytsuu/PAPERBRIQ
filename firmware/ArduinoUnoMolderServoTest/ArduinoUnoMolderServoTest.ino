#include <Arduino.h>
#include <Servo.h>

// PaperBriq molder-servo calibration test.
// The servo never moves automatically. Use Serial Monitor commands only.

const uint8_t MOLDER_SERVO_PIN = A1;
const uint8_t CENTER_ANGLE = 90;
const uint8_t MIN_TEST_ANGLE = 80;
const uint8_t MAX_TEST_ANGLE = 120;
const uint8_t STEP_DEGREES = 5;

Servo molderServo;
uint8_t currentAngle = CENTER_ANGLE;
bool testArmed = false;

void releaseServo() {
  if (molderServo.attached()) {
    molderServo.detach();
  }
  pinMode(MOLDER_SERVO_PIN, OUTPUT);
  digitalWrite(MOLDER_SERVO_PIN, LOW);
}

void printHelp() {
  Serial.println();
  Serial.println(F("PaperBriq molder-servo test ready"));
  Serial.println(F("R = arm test (does not move servo)"));
  Serial.println(F("C = move to center, 90 degrees"));
  Serial.println(F("D = move DOWN by 5 degrees"));
  Serial.println(F("U = move UP by 5 degrees"));
  Serial.println(F("X = release servo and disarm"));
  Serial.println(F("H = show commands"));
  Serial.println(F("No movement occurs automatically"));
}

void moveTo(uint8_t angle) {
  if (!testArmed) {
    Serial.println(F("COMMAND REJECTED: send R first"));
    return;
  }

  currentAngle = constrain(angle, MIN_TEST_ANGLE, MAX_TEST_ANGLE);

  if (!molderServo.attached()) {
    // Store the angle before attaching to avoid a center-position twitch.
    molderServo.write(currentAngle);
    molderServo.attach(MOLDER_SERVO_PIN);
  } else {
    molderServo.write(currentAngle);
  }

  Serial.print(F("MOLDER SERVO: "));
  Serial.print(currentAngle);
  Serial.println(F(" degrees"));
}

void setup() {
  digitalWrite(MOLDER_SERVO_PIN, LOW);
  pinMode(MOLDER_SERVO_PIN, OUTPUT);
  releaseServo();

  Serial.begin(115200);
  delay(500);
  printHelp();
}

void loop() {
  if (Serial.available() <= 0) {
    return;
  }

  const char command = Serial.read();

  switch (command) {
    case 'r':
    case 'R':
      testArmed = true;
      Serial.println(F("MOLDER SERVO TEST ARMED"));
      break;

    case 'c':
    case 'C':
      moveTo(CENTER_ANGLE);
      break;

    case 'd':
    case 'D':
      moveTo(currentAngle - STEP_DEGREES);
      break;

    case 'u':
    case 'U':
      moveTo(currentAngle + STEP_DEGREES);
      break;

    case 'x':
    case 'X':
      releaseServo();
      testArmed = false;
      currentAngle = CENTER_ANGLE;
      Serial.println(F("MOLDER SERVO: released and disarmed"));
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
      Serial.println(F("Unknown command; send H for help"));
      break;
  }
}
