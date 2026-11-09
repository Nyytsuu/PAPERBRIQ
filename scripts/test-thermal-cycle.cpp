#include <cassert>
#include <iostream>
#include "../firmware/PaperBriqController/ThermalCycle.h"
int main() {
  ProductionTimer p; ThermalCycle h;
  assert(p.start('H',1,60,600,true,true,0)); h.begin(p,0);
  h.tick(p,true,true,45,45,600000,0);
  h.tick(p,true,true,43,45,600000,9000); // A dip restarts qualification.
  h.tick(p,true,true,45,45,600000,10000);
  h.tick(p,true,true,45,45,600000,19999); assert(h.stage==ThermalCycle::WARMING);
  h.tick(p,true,true,45,45,600000,20000); assert(h.stage==ThermalCycle::READY);
  h.tick(p,true,true,45,45,600000,60000); assert(!p.active() && !h.heating(p));
  assert(p.start('D',2,60,600,true,true,0xFFFFFF00UL)); h.begin(p,0xFFFFFF00UL);
  h.tick(p,true,true,25,45,600000,0xFFFFFF00UL);
  assert(p.remaining(0)==60 && p.elapsed(0)==0);
  h.tick(p,true,true,45,45,600000,1000);
  assert(p.adjust('D',2,30,600,2000));
  h.tick(p,true,true,45,45,600000,11000);
  assert(p.on() && h.stage==ThermalCycle::DRYING && p.elapsed(11000)==0);
  h.tick(p,true,true,45,45,600000,41000); assert(!p.active());
  assert(p.start('D',3,60,600,true,true,100)); h.begin(p,100);
  h.tick(p,true,true,20,45,600000,600100); assert(p.phase==ProductionTimer::FAULT);
  assert(p.start('H',4,60,600,true,true,0)); h.begin(p,0);
  h.tick(p,true,false,45,45,600000,1); assert(!h.heating(p));
  char kind; bool edit; uint32_t run,seconds;
  assert(parseProductionCommand("HH:1234ABCD:600",kind,edit,run,seconds));
  assert(kind=='H' && !edit && seconds==600);
  assert(!parseProductionCommand("HT:1234ABCD:600",kind,edit,run,seconds));
  std::cout << "Thermal cycle: stable target, dip, bounded holding, clock wrap, drying edits/warmup timeout and invalid sensor passed\n";
}
