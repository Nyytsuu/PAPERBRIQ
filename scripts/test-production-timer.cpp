#include <cassert>
#include <iostream>
#include "../firmware/ArduinoUnoMotorTest/ProductionTimer.h"
int main() {
  ProductionTimer t;
  assert(!t.start('P', 1, 60, 0, true, true, 0));
  assert(!t.start('P', 1, 60, 120, false, true, 0));
  assert(t.start('P', 1, 60, 120, true, true, 100));
  t.detectPaper(true, false, 500); assert(!t.on());
  t.detectPaper(true, true, 1000); assert(t.on());
  t.detectPaper(true, true, 5000); assert(t.startedAt == 1000);
  assert(!t.adjust('P', 2, 80, 120, 10000));
  assert(!t.adjust('P', 1, 121, 120, 10000));
  assert(t.adjust('P', 1, 80, 120, 11000)); assert(t.remaining(11000) == 70);
  assert(t.adjust('P', 1, 80, 120, 21000)); assert(t.remaining(21000) == 60);
  assert(t.adjust('P', 1, 15, 120, 21000)); assert(t.phase == ProductionTimer::FINISHED);
  assert(!t.start('P', 1, 20, 120, true, true, 22000));
  assert(t.start('D', 2, 3600, 7200, true, true, 23000));
  t.tick(false, 25000); assert(t.phase == ProductionTimer::FAULT && !t.on());
  assert(!t.adjust('D', 2, 3600, 7200, 26000));
  assert(t.start('D', 3, 2, 7200, true, true, 0xFFFFFF00UL));
  t.tick(true, 1743); assert(t.on()); t.tick(true, 1744); assert(!t.on());
  char kind; bool edit; uint32_t id, seconds;
  assert(parseProductionCommand("PP:1234ABCD:120", kind, edit, id, seconds));
  assert(kind == 'P' && !edit && seconds == 120);
  assert(parseProductionCommand("DT:1234ABCD:3600", kind, edit, id, seconds));
  assert(kind == 'D' && edit && seconds == 3600);
  for (const char *bad : {"PP:1234ABCD:0", "PP:00000000:1", "PP:1234ABCD:01", "PP:1234ABCD:1.5", "PP:1234ABCD:2147484", "PP:1234ABCD:-1", "PP:1234ABCD:99999999999"})
    assert(!parseProductionCommand(bad, kind, edit, id, seconds));
  std::cout << "Production timers: commissioning gates, IR one-shot, run identity, live edits, deadline, faults and clock wrap passed\n";
}
