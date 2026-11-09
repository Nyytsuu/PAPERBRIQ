#pragma once
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <deque>
#include <sstream>
#include <string>
#define F(x) (x)
#define PROGMEM
typedef char __FlashStringHelper;
#define _BV(x) (1U << (x))
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define A0 14
#define A1 15
#define A2 16
#define A3 17
#define constrain(x, a, b) ((x) < (a) ? (a) : ((x) > (b) ? (b) : (x)))
extern unsigned long testMillis;
extern int testOutputs[32];
inline unsigned long millis() { return testMillis; }
inline void delay(unsigned long ms) { testMillis += ms; }
inline int &testInput(int pin) {
  static int values[32] = {};
  return values[pin];
}
inline void pinMode(int pin, int mode) { if (mode == INPUT_PULLUP) testInput(pin) = HIGH; }
inline int digitalRead(int pin) { return testInput(pin); }
inline void digitalWrite(int pin, int value) { testOutputs[pin] = value; }
inline void analogWrite(int pin, int value) { testOutputs[pin] = value; }
struct Print {
  std::string output;
  std::deque<char> input;
  template<typename T> void print(T value) { std::ostringstream s; s << value; output += s.str(); }
  template<typename T> void println(T value) { print(value); output += '\n'; }
  void println() { output += '\n'; }
  void begin(unsigned long) {}
  int available() { return (int)input.size(); }
  int read() { if (input.empty()) return -1; char c = input.front(); input.pop_front(); return c; }
};
extern Print Serial;
