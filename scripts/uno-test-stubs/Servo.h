#pragma once
struct Servo {
  bool connected = false;
  int angle = 0, writes = 0, detaches = 0;
  bool attached() { return connected; }
  void attach(int) { connected = true; }
  void detach() { connected = false; detaches++; }
  void write(int value) { angle = value; writes++; }
};
