#pragma once
#include <stdint.h>

// One transition per tick: a late loop still gives a full OFF interval before
// reversing. Never skip straight across a pause to catch up with wall time.
struct MixerAlternation {
  uint32_t stageAt = 0;
  uint8_t stage = 0; // 0=forward, 1=off, 2=reverse, 3=off

  void begin(uint32_t now) { stage = 0; stageAt = now; }
  bool tick(uint32_t now, uint32_t runMs, uint32_t pauseMs) {
    if (uint32_t(now - stageAt) < ((stage & 1) ? pauseMs : runMs)) return false;
    stage = (stage + 1) & 3;
    stageAt = now;
    return true;
  }
};
