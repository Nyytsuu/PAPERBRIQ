#pragma once
#include "Arduino.h"
struct SoftwareSerial : Print { SoftwareSerial(int, int) {} };
