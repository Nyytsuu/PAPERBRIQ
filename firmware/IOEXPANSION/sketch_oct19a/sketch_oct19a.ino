#include <Wire.h>

void setup() {
  Serial.begin(9600);
  delay(500);

  Wire.begin();
  Wire.setClock(100000);

  Serial.println("Scanning I2C bus...");

  byte deviceCount = 0;

  for (byte address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    byte result = Wire.endTransmission();

    if (result == 0) {
      Serial.print("Found device at 0x");

      if (address < 16) {
        Serial.print("0");
      }

      Serial.println(address, HEX);
      deviceCount++;
    }
  }

  if (deviceCount == 0) {
    Serial.println("No I2C devices found");
  }

  Serial.println("Scan finished");
}

void loop() {
}