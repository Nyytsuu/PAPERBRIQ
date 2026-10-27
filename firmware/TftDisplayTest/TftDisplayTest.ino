#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>

// PaperBriq ESP32 -> 1.8-inch ST7735 wiring.
const uint8_t TFT_CS = 33;
const uint8_t TFT_DC = 21;
const uint8_t TFT_RST = 22;
const uint8_t TFT_SCK = 18;
const uint8_t TFT_MOSI = 23;

Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_RST);

void showColor(uint16_t color, const char *name) {
  Serial.print("TFT test: ");
  Serial.println(name);
  tft.fillScreen(color);
  delay(1000);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("PaperBriq TFT-only test starting");

  // Put chip select in its safe inactive state before starting SPI.
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
  SPI.begin(TFT_SCK, -1, TFT_MOSI, TFT_CS);

  tft.initR(INITR_BLACKTAB);
  tft.setRotation(3);

  showColor(ST77XX_RED, "RED");
  showColor(ST77XX_GREEN, "GREEN");
  showColor(ST77XX_BLUE, "BLUE");


  tft.fillScreen(ST77XX_BLACK);
  tft.setTextWrap(false);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.setCursor(28, 35);
  tft.println("TFT OK");
  tft.setTextSize(1);
  tft.setCursor(18, 70);
  tft.println("PaperBriq ESP32");
  Serial.println("TFT test complete; the screen should say TFT OK");
}

void loop() {
  // Keep the final diagnostic screen visible.
}
