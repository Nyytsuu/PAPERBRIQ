#pragma once
#include <stdint.h>
#include <assert.h>
struct TwoWire {
  uint8_t address = 0, pending = 0xFF, received = 0xFF;
  uint8_t inputs[128], outputs[128];
  bool missing[128] = {};
  TwoWire() {
    for (int i=0;i<128;i++) inputs[i] = outputs[i] = 0xFF;
    inputs[0x20] = 0xF3; // Stop/E-stop NC closed, Start/Reset released.
    inputs[0x24] = 0xFB; // Door NC closed.
  }
  void begin() {}
  void setWireTimeout(unsigned long, bool) {}
  void beginTransmission(uint8_t a) { address=a; pending=outputs[a]; }
  void write(uint8_t value) { pending=value; }
  int endTransmission() {
    if(missing[address]) return 4;
    if(address==0x24) { assert(pending & 4); assert(pending & 128); }
    outputs[address]=pending; return 0;
  }
  int requestFrom(uint8_t a, uint8_t) {
    if(missing[a]) return 0;
    received = inputs[a] & outputs[a]; return 1;
  }
  int read() { return received; }
};
extern TwoWire Wire;
