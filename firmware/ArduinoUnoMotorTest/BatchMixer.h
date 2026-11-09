#pragma once
#include <stdint.h>

// Local loads start with a forward lead (stage 8), then run stages 0–6:
// F10 / OFF2 / R2 / OFF2 / F10 / OFF2 / R2. Stage 7 is done.
// Timed OFF is not measured zero speed. A delayed loop never skips a pause.
struct BatchMixer {
  uint32_t stageAt = 0;
  uint8_t stage = 7;
  void begin(uint32_t now, bool forwardLead = false) {
    stage = forwardLead ? 8 : 0; stageAt = now;
  }
  bool done() const { return stage == 7; }
  bool tick(uint32_t now, uint32_t forwardMs, uint32_t reverseMs, uint32_t pauseMs,
            uint32_t finalReverseExtraMs = 0) {
    if (done()) return false;
    const uint32_t duration = (stage & 1) ? pauseMs : ((stage & 2) ? reverseMs : forwardMs);
    if (uint32_t(now - stageAt) < duration + (stage == 6 ? finalReverseExtraMs : 0)) return false;
    stage = stage == 8 ? 0 : stage + 1;
    stageAt = now;
    return true;
  }
};
