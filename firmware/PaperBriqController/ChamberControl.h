#pragma once
#include <stdint.h>

// Hardware-free state logic used by the Uno sketch and native tests.
// User confirmed P2/P5/P6 are all on LED PCF8574 0x24.
struct ChamberControl {
  static const uint8_t DOOR = 1 << 2;
  static const uint8_t FAN = 1 << 5;
  static const uint8_t UNLOCK = 1 << 6;
  bool online = false, doorClosed = false, fanOn = false, unlockOn = false;
  bool closing = false, hasUnlocked = false;
  uint32_t closingAt = 0, fanAt = 0, unlockAt = 0;

  void sample(bool available, bool closed, uint32_t now) {
    online = available;
    if (!available || !closed) {
      doorClosed = false; closing = false; fanOn = false;
      if (!available) unlockOn = false;
    } else {
      if (!closing) { closing = true; closingAt = now; }
      doorClosed = (uint32_t)(now - closingAt) >= 50;
    }
    tick(now);
  }
  void tick(uint32_t now) {
    if (fanOn && (uint32_t)(now - fanAt) >= 10000) fanOn = false;
    if (unlockOn && (uint32_t)(now - unlockAt) >= 1000) unlockOn = false;
  }
  bool ready() const { return online && doorClosed && !unlockOn && !fanOn; }
  bool startFan(uint32_t now, bool stopped) {
    if (!stopped || !ready()) return false;
    fanOn = true; fanAt = now; return true;
  }
  bool unlock(uint32_t now, bool stoppedAndSettled) {
    if (!stoppedAndSettled || !online || fanOn || unlockOn ||
        (hasUnlocked && (uint32_t)(now - unlockAt) < 5000)) return false;
    unlockOn = true; unlockAt = now; hasUnlocked = true; return true;
  }
  void cancel() { fanOn = false; unlockOn = false; }
  uint8_t outputs(uint8_t lights) const {
    // Only P0/P1/P3/P4 are status outputs. P2 stays released; P7 stays off.
    uint8_t value = (lights & 0x1B) | 0xE4;
    if (online && doorClosed && fanOn) value &= (uint8_t)~FAN;
    if (online && unlockOn) value &= (uint8_t)~UNLOCK;
    return value;
  }
};
