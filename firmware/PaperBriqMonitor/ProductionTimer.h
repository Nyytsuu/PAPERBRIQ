#pragma once
#include <stdint.h>
#include <string.h>

// Local controller clock owns the deadline. Neither an app refresh nor an IR
// detection may restart a running deadline. All values are whole seconds.
struct ProductionTimer {
  enum Phase : uint8_t { IDLE, WAITING, RUNNING, FINISHED, STOPPED, FAULT };
  Phase phase = IDLE;
  char kind = '-';
  uint32_t run = 0, total = 0, startedAt = 0, elapsedAtEnd = 0;
  bool active() const { return phase == WAITING || phase == RUNNING; }
  bool on() const { return phase == RUNNING; }
  uint32_t elapsed(uint32_t now) const {
    return phase == RUNNING ? (uint32_t)(now - startedAt) / 1000UL : elapsedAtEnd;
  }
  uint32_t remaining(uint32_t now) const {
    const uint32_t used = elapsed(now);
    return active() && total > used ? total - used : 0;
  }
  bool start(char output, uint32_t id, uint32_t seconds, uint32_t maximum,
             bool verified, bool inputsReady, uint32_t now) {
    if (active() || !verified || !inputsReady || !id || id == run ||
        (output != 'P' && output != 'D') || !seconds || !maximum ||
        seconds > maximum || maximum > 2147483UL) return false;
    kind = output; run = id; total = seconds; startedAt = now; elapsedAtEnd = 0;
    phase = output == 'P' ? WAITING : RUNNING;
    return true;
  }
  bool adjust(char output, uint32_t id, uint32_t seconds, uint32_t maximum, uint32_t now) {
    if (!active() || kind != output || id != run || !seconds || !maximum ||
        seconds > maximum || maximum > 2147483UL) return false;
    total = seconds;
    tick(true, now);
    return true;
  }
  void detectPaper(bool freshRisingEdge, bool mixing, uint32_t now) {
    if (kind == 'P' && phase == WAITING && freshRisingEdge && mixing) {
      startedAt = now; phase = RUNNING;
    }
  }
  void stop(bool fault, uint32_t now) {
    if (!active()) return;
    elapsedAtEnd = elapsed(now); phase = fault ? FAULT : STOPPED;
  }
  void tick(bool inputsReady, uint32_t now) {
    if (!active()) return;
    if (!inputsReady) { stop(true, now); return; }
    if (on() && (uint32_t)(now - startedAt) >= total * 1000UL) {
      elapsedAtEnd = elapsed(now); phase = FINISHED;
    }
  }
};

// PP=arm paper-triggered pump, DD=start drying, PT/DT=edit current total.
// Eight hex digits identify the run independently from command delivery IDs.
inline bool parseProductionCommand(const char *s, char &kind, bool &edit,
                                   uint32_t &run, uint32_t &seconds) {
  if (strlen(s) < 13 || (s[0] != 'P' && s[0] != 'D') ||
      (s[1] != s[0] && s[1] != 'T') || s[2] != ':' || s[11] != ':') return false;
  run = 0;
  for (uint8_t i = 3; i < 11; ++i) {
    char c = s[i];
    uint8_t v = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 255;
    if (v > 15) return false;
    run = (run << 4) | v;
  }
  if (!run || s[12] < '1' || s[12] > '9') return false;
  seconds = 0;
  for (uint8_t i = 12; s[i]; ++i) {
    if (i > 18 || s[i] < '0' || s[i] > '9') return false;
    seconds = seconds * 10 + s[i] - '0';
  }
  if (seconds > 2147483UL) return false;
  kind = s[0]; edit = s[1] == 'T';
  return true;
}
