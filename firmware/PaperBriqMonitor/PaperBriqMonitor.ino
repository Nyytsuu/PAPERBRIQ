#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include "secrets.h"

const char *IOT_API_KEY = ""; // Match IOT_API_KEY in .env, or leave both unset for local testing.
const char *DEVICE_ID = "PBQ-ESP32-01";

WiFiMulti wifiMulti;

#define DHTPIN 27
#define DHTTYPE DHT22
#define IR_PIN 25
#define IR_DETECTED LOW
#define FLOAT_PIN 32

#define TFT_CS 33
#define TFT_DC 21
#define TFT_RST 22
#define TFT_SCK 18
#define TFT_MOSI 23

#define PB_BG 0x0841
#define PB_CARD 0x10A2
#define PB_GREEN 0x5E66
#define PB_CYAN 0x04FF
#define PB_ORANGE 0xFD20
#define PB_RED 0xF800
#define PB_WHITE 0xFFFF
#define PB_GRAY 0x9CD3

DHT dht(DHTPIN, DHTTYPE);
Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_RST);

float lastTemp = 0;
float lastHumidity = 0;
bool haveDhtReading = false;
unsigned long lastSampleAt = 0;
unsigned long lastPostAt = 0;
const unsigned long SAMPLE_INTERVAL_MS = 2500;
const unsigned long POST_INTERVAL_MS = 5000;

void connectWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.mode(WIFI_STA);
  Serial.print("Connecting to a saved Wi-Fi network");
  unsigned long started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 15000) {
    wifiMulti.run();
    delay(300);
    Serial.print(".");
  }
  Serial.println(WiFi.status() == WL_CONNECTED ? " connected" : " failed");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("ESP32 IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("Connected SSID: ");
    Serial.println(WiFi.SSID());
  }
}

String paperBriqUrl() {
  String ssid = WiFi.SSID();
  String host = HOME_SERVER_HOST;
  if (ssid == HOTSPOT_ONE_SSID) host = HOTSPOT_ONE_SERVER_HOST;
  if (ssid == HOTSPOT_TWO_SSID) host = HOTSPOT_TWO_SERVER_HOST;
  return "http://" + host + ":4173/api/iot/readings";
}

void drawStaticDashboard() {
  tft.fillScreen(PB_BG);
  tft.fillRoundRect(4, 3, 152, 21, 5, PB_CARD);
  tft.fillCircle(15, 13, 6, PB_GREEN);
  tft.setTextSize(1);
  tft.setTextColor(PB_WHITE);
  tft.setCursor(27, 7);
  tft.print("PAPERBRIQ");
  tft.setTextColor(PB_GREEN);
  tft.setCursor(27, 15);
  tft.print("LIVE MONITOR");

  tft.fillRoundRect(4, 28, 74, 51, 5, PB_CARD);
  tft.fillRoundRect(82, 28, 74, 51, 5, PB_CARD);
  tft.fillRoundRect(4, 83, 74, 38, 5, PB_CARD);
  tft.fillRoundRect(82, 83, 74, 38, 5, PB_CARD);
  tft.setTextColor(PB_GRAY);
  tft.setCursor(10, 33); tft.print("TEMPERATURE");
  tft.setCursor(88, 33); tft.print("HUMIDITY");
  tft.setCursor(10, 88); tft.print("BRIQUETTE");
  tft.setCursor(88, 88); tft.print("WATER LEVEL");

  // Thermometer icon.
  tft.drawRoundRect(12, 49, 6, 17, 3, PB_ORANGE);
  tft.fillCircle(15, 66, 5, PB_ORANGE);
  tft.drawLine(15, 52, 15, 64, PB_ORANGE);

  // Humidity droplet icon.
  tft.fillTriangle(96, 48, 90, 58, 102, 58, PB_CYAN);
  tft.fillCircle(96, 58, 6, PB_CYAN);
}

void drawValues(bool dhtOK, bool detected, bool waterOK) {
  // Clear only the numeric regions so the icons remain visible.
  tft.fillRect(24, 48, 49, 25, PB_CARD);
  tft.fillRect(106, 48, 46, 25, PB_CARD);
  tft.fillRect(10, 101, 64, 15, PB_CARD);
  tft.fillRect(88, 101, 64, 15, PB_CARD);

  tft.setTextSize(2);
  tft.setTextColor(dhtOK ? PB_WHITE : PB_RED);
  tft.setCursor(25, 53);
  if (dhtOK) { tft.print(lastTemp, 1); tft.setTextSize(1); tft.print(" C"); }
  else { tft.setTextSize(1); tft.print("ERROR"); }

  tft.setTextSize(2);
  tft.setTextColor(dhtOK ? PB_CYAN : PB_RED);
  tft.setCursor(112, 53);
  if (dhtOK) { tft.print(lastHumidity, 1); tft.setTextSize(1); tft.print("%"); }
  else { tft.setTextSize(1); tft.print("ERROR"); }

  tft.setTextSize(1);
  tft.setTextColor(detected ? PB_GREEN : PB_GRAY);
  tft.setCursor(10, 105); tft.print(detected ? "DETECTED" : "WAITING");
  tft.setTextColor(waterOK ? PB_CYAN : PB_RED);
  tft.setCursor(91, 105); tft.print(waterOK ? "NORMAL" : "LOW");

  tft.fillCircle(143, 13, 3, WiFi.status() == WL_CONNECTED ? PB_GREEN : PB_RED);
}

void postTelemetry(bool dhtOK, bool detected, bool waterOK) {
  if (WiFi.status() != WL_CONNECTED) {
    connectWifi();
    if (WiFi.status() != WL_CONNECTED) return;
  }

  String payload = "{\"deviceId\":\"" + String(DEVICE_ID) + "\",\"readings\":[";
  payload += "{\"name\":\"Temperature\",\"value\":" + String(lastTemp, 1) + ",\"unit\":\"C\",\"status\":\"" + String(dhtOK ? "Normal" : "Warning") + "\"},";
  payload += "{\"name\":\"Humidity\",\"value\":" + String(lastHumidity, 1) + ",\"unit\":\"%RH\",\"status\":\"" + String(dhtOK ? "Normal" : "Warning") + "\"},";
  payload += "{\"name\":\"IR Sensor\",\"value\":" + String(detected ? 1 : 0) + ",\"unit\":\"digital\",\"status\":\"Normal\"},";
  payload += "{\"name\":\"Rainwater Level\",\"value\":" + String(waterOK ? 100 : 0) + ",\"unit\":\"%\",\"status\":\"" + String(waterOK ? "Normal" : "Warning") + "\"}";
  payload += "],\"hardware\":{";
  payload += "\"dht22\":\"" + String(dhtOK ? "Online" : "Attention") + "\",";
  payload += "\"irSensor\":\"Online\",\"tftDisplay\":\"Online\",\"floatSwitch\":\"Online\"}}";

  WiFiClient client;
  HTTPClient http;
  String endpoint = paperBriqUrl();
  if (endpoint.indexOf("0.0.0.0") >= 0) {
    Serial.println("Set the laptop IP for this Wi-Fi network in secrets.h");
    return;
  }
  http.begin(client, endpoint);
  http.addHeader("Content-Type", "application/json");
  if (strlen(IOT_API_KEY) > 0) http.addHeader("X-API-Key", IOT_API_KEY);
  int responseCode = http.POST(payload);
  Serial.print("PaperBriq POST: ");
  Serial.println(responseCode);
  if (responseCode > 0) Serial.println(http.getString());
  http.end();
}

void setup() {
  Serial.begin(115200);
  dht.begin();
  pinMode(IR_PIN, INPUT);
  pinMode(FLOAT_PIN, INPUT_PULLUP);
  SPI.begin(TFT_SCK, -1, TFT_MOSI, TFT_CS);
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);
  drawStaticDashboard();
  wifiMulti.addAP(HOME_WIFI_SSID, HOME_WIFI_PASSWORD);
  wifiMulti.addAP(HOTSPOT_ONE_SSID, HOTSPOT_ONE_PASSWORD);
  wifiMulti.addAP(HOTSPOT_TWO_SSID, HOTSPOT_TWO_PASSWORD);
  connectWifi();
}

void loop() {
  unsigned long now = millis();
  if (now - lastSampleAt < SAMPLE_INTERVAL_MS) return;
  lastSampleAt = now;

  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  bool dhtOK = !isnan(temperature) && !isnan(humidity);
  if (dhtOK) {
    lastTemp = temperature;
    lastHumidity = humidity;
    haveDhtReading = true;
  }

  bool detected = digitalRead(IR_PIN) == IR_DETECTED;
  bool waterOK = digitalRead(FLOAT_PIN) == LOW;
  drawValues(dhtOK, detected, waterOK);

  if (now - lastPostAt >= POST_INTERVAL_MS && haveDhtReading) {
    lastPostAt = now;
    postTelemetry(dhtOK, detected, waterOK);
  }
}
