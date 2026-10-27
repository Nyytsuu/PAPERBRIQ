#include <assert.h>
#include <stdio.h>
#include "../firmware/ArduinoUnoMotorTest/ChamberControl.h"

int main() {
  ChamberControl c;
  assert(!c.ready());
  assert(!c.startFan(0, true));
  assert(!c.unlock(0, true));
  c.sample(true, true, 100);
  c.sample(true, true, 149); assert(!c.ready());
  c.sample(true, true, 150); assert(c.ready());
  assert(!c.startFan(151, false));
  assert(c.startFan(151, true));
  assert(!c.unlock(152, true));
  assert(!c.ready());
  for (int lights = 0; lights < 256; ++lights) {
    const uint8_t out = c.outputs(lights);
    assert(out & ChamberControl::DOOR);
    assert(out & (1 << 7));
    assert(!(out & ChamberControl::FAN));
    assert(out & ChamberControl::UNLOCK);
    assert((out & 0x1B) == (lights & 0x1B));
  }
  c.tick(10150); assert(c.fanOn);
  c.tick(10151); assert(!c.fanOn);
  assert(!c.unlock(10152, false));
  assert(c.unlock(10152, true));
  assert(!c.startFan(10153, true));
  assert(!c.unlock(10153, true));
  c.tick(11151); assert(c.unlockOn);
  c.tick(11152); assert(!c.unlockOn);
  assert(!c.unlock(11153, true));
  assert(c.unlock(15152, true));
  c.sample(false, false, 15153);
  assert(!c.online && !c.doorClosed && !c.fanOn && !c.unlockOn);
  c.sample(true, true, 16000); c.sample(true, true, 16050);
  assert(c.startFan(16051, true));
  c.sample(true, false, 16052);
  assert(!c.doorClosed && !c.fanOn); // Immediate opening, no debounce delay.
  c.sample(true, true, 16053); assert(!c.ready());
  c.sample(true, false, 16054); c.sample(true, true, 16055);
  c.sample(true, true, 16104); assert(!c.ready());
  c.sample(true, true, 16105); assert(c.ready());
  ChamberControl rollover;
  rollover.sample(true, true, UINT32_MAX - 200);
  rollover.sample(true, true, UINT32_MAX - 100);
  assert(rollover.unlock(UINT32_MAX - 10, true));
  rollover.tick(988); assert(rollover.unlockOn);
  rollover.tick(989); assert(!rollover.unlockOn);
  assert(rollover.startFan(990, true));
  rollover.cancel(); assert(!rollover.fanOn && !rollover.unlockOn);
  puts("Chamber control: input preservation, door faults, pulse limits, replay cooldown, and timer rollover passed.");
}
