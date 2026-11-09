#pragma once
#include <stdint.h>

// A clear-to-detected transition must remain stable before it is published.
// GPIO sampling runs independently of blocking Wi-Fi/HTTP work.
struct PaperInput {
  bool initialized = false, raw = false, detected = false, ready = false;
  uint32_t changedAt = 0, sampledAt = 0;
  void sample(bool value, uint32_t now) {
    if (!initialized || uint32_t(now - sampledAt) > 1000U) {
      initialized = true; ready = false; raw = value; changedAt = now;
    } else if (value != raw) {
      raw = value; changedAt = now;
    }
    sampledAt = now;
    if (uint32_t(now - changedAt) >= 50U) { detected = raw; ready = true; }
  }
};
