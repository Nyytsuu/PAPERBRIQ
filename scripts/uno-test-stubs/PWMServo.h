#pragma once
#include <assert.h>
#include "Servo.h"
struct PWMServo : Servo {
  void attach(int pin) {
    assert(pin == 9 || pin == 10);
    Servo::attach(pin);
  }
  void write(int value) {
    if (attached()) Servo::write(value);
  }
};
