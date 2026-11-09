#pragma once
#include "ProductionTimer.h"

// The external thermostat and independent cutoff remain in the power circuit.
// DHT is supervisory only. A fresh reading must remain near target for 10 s.
struct ThermalCycle {
  enum Stage : uint8_t { IDLE, WARMING, READY, DRYING, FAULT };
  Stage stage = IDLE;
  uint32_t warmStarted = 0, nearSince = 0;
  bool nearTarget = false;
  void begin(ProductionTimer &timer, uint32_t now) {
    stage = WARMING; warmStarted = now; nearTarget = false;
    if (timer.kind == 'D') timer.phase = ProductionTimer::WAITING;
  }
  bool heating(const ProductionTimer &timer) const {
    return timer.active() && (timer.kind == 'D' || timer.kind == 'H');
  }
  void tick(ProductionTimer &timer, bool safe, bool fresh, float temperature,
            uint8_t target, uint32_t warmupLimitMs, uint32_t now) {
    if (!heating(timer)) { if (stage != FAULT) stage = IDLE; return; }
    if (!safe || !fresh || temperature >= target + 5 ||
        (timer.kind == 'D' && stage == WARMING && now - warmStarted >= warmupLimitMs)) {
      timer.stop(true, now); stage = FAULT; return;
    }
    if (stage != DRYING) {
      if (temperature < target - 1) { nearTarget = false; stage = WARMING; }
      else {
        if (!nearTarget) { nearTarget = true; nearSince = now; }
        if (now - nearSince >= 10000UL) {
          if (timer.kind == 'D') {
            timer.startedAt = now; timer.phase = ProductionTimer::RUNNING; stage = DRYING;
          } else stage = READY;
        }
      }
    }
    timer.tick(true, now);
    if (!timer.active()) stage = IDLE;
  }
};
