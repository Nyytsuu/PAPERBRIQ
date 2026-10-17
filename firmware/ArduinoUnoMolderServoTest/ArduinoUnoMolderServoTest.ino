
#include <Servo.h>

Servo gateServo;

const int SERVO_PIN = A1;

void setup() {
  gateServo.attach(SERVO_PIN);
  gateServo.write(120);
  delay(2000);
}

void loop() {
  gateServo.write(0);  // Test opposite direction
  delay(3000);

  gateServo.write(120);   // Return to start
  delay(3000);
}
