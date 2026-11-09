#include <cassert>
#include <cstdio>
#include "../firmware/PaperBriqMonitor/PaperInput.h"
int main() {
  PaperInput input;
  input.sample(false, 0); input.sample(false, 49); assert(!input.ready);
  input.sample(false, 50); assert(input.ready && !input.detected);
  input.sample(true, 60); input.sample(false, 80); input.sample(false, 130);
  assert(!input.detected); // Short noise pulse is not a paper arrival.
  input.sample(true, 140); input.sample(true, 189); assert(!input.detected);
  input.sample(true, 190); assert(input.detected);
  input.sample(false, 200); input.sample(false, 250); assert(!input.detected);
  input.sample(true, 2000); assert(!input.ready); // Gap requires a fresh stable sample.
  input.sample(true, 2050); assert(input.ready && input.detected);
  PaperInput rollover;
  rollover.sample(false, 0xfffffff0U); rollover.sample(false, 40);
  assert(rollover.ready && !rollover.detected);
  puts("Paper inputs: debounce, clear/detected transition, stale gap and clock wrap passed");
}
