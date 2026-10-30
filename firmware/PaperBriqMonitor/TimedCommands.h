// Mirrored in both sketches because Arduino builds copy sketch-local headers.
#pragma once
#include <stdint.h>
#include <string.h>

// Canonical UART actions; reject signs, fractions, padding and suffixes.
inline uint8_t timedCommandSeconds(const char *action) {
  const bool heater = strncmp(action, "HEATER:", 7) == 0;
  const bool pump = strncmp(action, "PUMP:", 5) == 0;
  if (!heater && !pump) return 0;
  const char *n = action + (heater ? 7 : 5);
  if (heater && strcmp(n, "10") == 0) return 10;
  if (n[0] >= '1' && n[0] <= (heater ? '9' : '3') && n[1] == '\0') return n[0] - '0';
  return 0;
}
