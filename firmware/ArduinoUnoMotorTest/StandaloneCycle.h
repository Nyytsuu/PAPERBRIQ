#pragma once
#include <stdint.h>

// W/physical Start uses an inlet-controlled water window per detected load.
// Y remains a separate check. Neither run is a calibrated water dose.
#ifndef PAPERBRIQ_LOCAL_HEAT_MS
#define PAPERBRIQ_LOCAL_HEAT_MS 600000UL
#endif
#ifndef PAPERBRIQ_LOCAL_PREHEAT_MAX_MS
#define PAPERBRIQ_LOCAL_PREHEAT_MAX_MS 600000UL
#endif
#ifndef PAPERBRIQ_LOCAL_WARM_IDLE_MS
#define PAPERBRIQ_LOCAL_WARM_IDLE_MS 120000UL
#endif
#ifndef PAPERBRIQ_LOCAL_SESSION_MAX_MS
#define PAPERBRIQ_LOCAL_SESSION_MAX_MS 1800000UL
#endif
static_assert(PAPERBRIQ_LOCAL_PREHEAT_MAX_MS > 0 && PAPERBRIQ_LOCAL_PREHEAT_MAX_MS <= 600000UL,
              "Preheat timeout must be 1..600000 ms");
static_assert(PAPERBRIQ_LOCAL_WARM_IDLE_MS > 0 && PAPERBRIQ_LOCAL_WARM_IDLE_MS <= 120000UL,
              "Warm idle timeout must be 1..120000 ms");
static_assert(PAPERBRIQ_LOCAL_SESSION_MAX_MS > 0 && PAPERBRIQ_LOCAL_SESSION_MAX_MS <= 1800000UL,
              "Session timeout must be 1..1800000 ms");

// Timed standby estimates; no position feedback or automatic homing.
// Start with presser fully retracted and pusher at the chosen halfway park.
// Presser return is operator-selected at 19s. W doubles the 12s base pusher outward time.
// Timed travel does not establish full retraction or a repeatable park.
// Check actual position before another cycle; adjust out/back independently.
#ifndef PAPERBRIQ_PRESSER_RETURN_MS
#define PAPERBRIQ_PRESSER_RETURN_MS 19000UL
#endif
#ifndef PAPERBRIQ_PUSHER_FROM_PARK_MS
#define PAPERBRIQ_PUSHER_FROM_PARK_MS 12000UL
#endif
#ifndef PAPERBRIQ_PUSHER_RETURN_TO_PARK_MS
#define PAPERBRIQ_PUSHER_RETURN_TO_PARK_MS 12000UL
#endif

// External thermostat controls temperature; no temperature/airflow/dryness
// feedback exists here. Start again during PREHEATING confirms the operator
// has checked its display. READY requires checking downstream capacity.
// Heater is disabled throughout FORMING: shared-supply capacity is unverified.
// Session deadline never resets when another batch is requested.
struct StandaloneCycle {
  enum Phase { OFF, PREHEATING, FORMING, HEATING, READY };
  Phase phase = OFF;
  uint8_t batch = 0; // 0..2: three material loads before ejection.
  bool fillOnly = false;
  bool repressed = false;
  bool pumpStarted = false;
  bool pumpWasOn = false;
  uint32_t pumpAt = 0, heatAt = 0, sessionAt = 0;
  bool active() const { return phase != OFF; }
  bool canForm() const { return phase == PREHEATING || phase == READY; }
  bool heating() const { return active() && phase != FORMING; }
  bool expired(uint32_t now) const {
    return active() && ((uint32_t)(now - sessionAt) >= PAPERBRIQ_LOCAL_SESSION_MAX_MS ||
      (phase == PREHEATING && (uint32_t)(now - heatAt) >= PAPERBRIQ_LOCAL_PREHEAT_MAX_MS) ||
      (phase == READY && (uint32_t)(now - heatAt) >= PAPERBRIQ_LOCAL_WARM_IDLE_MS));
  }
  bool pumping() const { return phase == FORMING && pumpStarted; }
  void preheat(uint32_t now) { phase = PREHEATING; sessionAt = heatAt = now; }
  void begin() { phase = FORMING; batch = 0; repressed = pumpStarted = pumpWasOn = false; }
  void nextBatch() { ++batch; pumpStarted = pumpWasOn = false; }
  void startWater(uint32_t now) {
    if (phase == FORMING && !pumpStarted) { pumpStarted = true; pumpAt = now; }
  }
  void stopWater() { pumpStarted = false; }
  void heat(uint32_t now) { phase = HEATING; heatAt = now; }
  void cancel() { phase = OFF; pumpStarted = pumpWasOn = false; }
};
