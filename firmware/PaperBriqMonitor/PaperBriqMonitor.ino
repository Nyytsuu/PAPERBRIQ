#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <esp_system.h>
#include <time.h>
#include "secrets.h"
#include "TimedCommands.h"
#include "ProductionTimer.h"
#include "PaperInput.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
// Arduino generates function declarations before the first function body.
enum MachineDisplayState : uint8_t;
struct TftPageReadings;
String productionJson;
unsigned long productionReplyAt = 0;
SemaphoreHandle_t unoTxMutex = nullptr;
portMUX_TYPE paperSnapshotMux = portMUX_INITIALIZER_UNLOCKED;
bool inletDetected = false, outletDetected = false, paperInputsReady = false;
uint32_t paperInputsAt = 0;

// Backward-compatible defaults let an existing secrets.h keep compiling.
#ifndef PAPERBRIQ_API_BASE_URL
#define PAPERBRIQ_API_BASE_URL ""
#endif
#ifndef PAPERBRIQ_IOT_API_KEY
#define PAPERBRIQ_IOT_API_KEY ""
#endif
#ifndef PAPERBRIQ_DEVICE_ID
#define PAPERBRIQ_DEVICE_ID "paperbriq-esp32"
#endif
#ifndef PAPERBRIQ_TLS_ROOT_CA
#define PAPERBRIQ_TLS_ROOT_CA ""
#endif
#ifndef PAPERBRIQ_ALLOW_INSECURE_TLS
#define PAPERBRIQ_ALLOW_INSECURE_TLS 0
#endif

const char *IOT_API_KEY = PAPERBRIQ_IOT_API_KEY;
const char *DEVICE_ID = PAPERBRIQ_DEVICE_ID;

WiFiMulti wifiMulti;

#define DHTPIN 27
#define DHTTYPE DHT22
// TFT restored for reconnection. The chamber sensor and app still work
// independently; define PAPERBRIQ_TFT_ENABLED 0 in secrets.h to disable it.
#ifndef PAPERBRIQ_TFT_ENABLED
#define PAPERBRIQ_TFT_ENABLED 1
#endif
#define IR_PIN 25
#define IR_DETECTED LOW
#define SHREDDER_IN_PIN 26
#define SHREDDER_OUT_PIN 19
#define FLOAT_PIN 32

#define TFT_CS 33
#define TFT_DC 21
#define TFT_RST 22
#define TFT_SCK 18
#define TFT_MOSI 23

// UART2 link to the Uno. Uno TX is 5 V and MUST be level shifted before GPIO16.
#define UNO_LINK_RX 16
#define UNO_LINK_TX 17
const uint32_t UNO_LINK_BAUD = 9600;
HardwareSerial unoLink(2);

// Send an entire UART frame under one lock; the sensor task must never insert
// its heartbeat in the middle of a command or DHT reply.
void sendUnoFrame(const String &frame) {
  if (!unoTxMutex || xSemaphoreTake(unoTxMutex, portMAX_DELAY) != pdTRUE) return;
  unoLink.println(frame);
  xSemaphoreGive(unoTxMutex);
}

bool sendPaperFrame(bool detected, bool outlet) {
  if (!unoTxMutex || xSemaphoreTake(unoTxMutex, portMAX_DELAY) != pdTRUE) return false;
  unoLink.println(String("IR2|") + (detected ? "1" : "0") + "|" + (outlet ? "1" : "0"));
  xSemaphoreGive(unoTxMutex);
  return true;
}

void paperSensorTask(void *) {
  PaperInput inlet, outlet;
  uint32_t sentAt = 0;
  bool sent = false, lastSent = false, lastOutlet = false;
  for (;;) {
    const uint32_t now = millis();
    inlet.sample(digitalRead(SHREDDER_IN_PIN) == IR_DETECTED, now);
    outlet.sample(digitalRead(SHREDDER_OUT_PIN) == IR_DETECTED, now);
    portENTER_CRITICAL(&paperSnapshotMux);
    inletDetected = inlet.detected; outletDetected = outlet.detected;
    paperInputsReady = inlet.ready && outlet.ready; paperInputsAt = now;
    portEXIT_CRITICAL(&paperSnapshotMux);
    if (inlet.ready && outlet.ready) {
      if (!sent || lastSent != inlet.detected || lastOutlet != outlet.detected || uint32_t(now - sentAt) >= 250U) {
        sent = sendPaperFrame(inlet.detected, outlet.detected);
        if (sent) { lastSent = inlet.detected; lastOutlet = outlet.detected; sentAt = now; }
      }
    } else sent = false;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

bool readPaperInputs(bool &inlet, bool &outlet) {
  portENTER_CRITICAL(&paperSnapshotMux);
  inlet = inletDetected; outlet = outletDetected;
  const bool fresh = paperInputsReady && uint32_t(millis() - paperInputsAt) < 1000U;
  portEXIT_CRITICAL(&paperSnapshotMux);
  return fresh;
}

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
unsigned long lastWifiAttemptAt = 0;
bool wifiAttempted = false;
unsigned long lastCommandPollAt = 0;
unsigned long lastHealthProbeAt = 0;
unsigned long healthReplyAt = 0;
String healthProbeId;
bool haveHealthReply = false;
bool healthControlIo = false;
bool healthStopClosed = false;
bool healthEmergencyClosed = false;
bool healthIndicatorIo = false;
bool healthArmed = false;
int8_t healthBatchMask = -1;
bool healthChamberSupported = false;
bool healthDoorClosed = false;
bool healthFanOn = false;
bool healthUnlockOn = false;
bool healthTestActive = false;
bool healthTimedVersion = false, healthTimedSupported = false;
bool healthHeaterOn = false, healthPumpOn = false;
bool healthHeaterReady = false, healthPumpReady = false;
const unsigned long HEALTH_PROBE_INTERVAL_MS = 7000;
const unsigned long HEALTH_REPLY_TIMEOUT_MS = 5000;
const unsigned long HEALTH_FRESH_MS = 20000;
const unsigned long SAMPLE_INTERVAL_MS = 2500;
const unsigned long POST_INTERVAL_MS = 5000;
const unsigned long COMMAND_POLL_INTERVAL_MS = 2000;
const unsigned long UNO_ACK_TIMEOUT_MS = 5000;
const unsigned long EVENT_RETRY_INTERVAL_MS = 1000;

enum MachineDisplayState : uint8_t {
  DISPLAY_GETTING_READY,
  DISPLAY_STOPPED,
  DISPLAY_READY,
  DISPLAY_RUNNING,
  DISPLAY_COMPLETE,
  DISPLAY_FAULT
};

MachineDisplayState machineDisplayState = DISPLAY_GETTING_READY;
bool tftFrozen = false;
bool tftNextPageRequested = false;

String unoLine;
String pendingDhtRequest;
unsigned long lastUnoByteAt = 0, dhtRequestAt = 0;
String pendingCommandId;
String pendingCommandAction;
unsigned long pendingCommandSentAt = 0;
String lastCommandId;
String lastCommandStatus;
String lastCommandMessage;
String processCommandId;

// One command is in flight at a time. Keep its latest acknowledgement local
// until the UART exchange finishes; HTTPS must not interrupt receipt of the
// final ACK. A final result supersedes the intermediate DELIVERED report.
String ackReportId;
String ackReportStatus;
String ackReportMessage;
unsigned long lastAckReportAttemptAt = 0;

struct QueuedEvent {
  String eventId;
  String type;
  String severity;
  String component;
  String processState;
  String message;
  String commandId;
};

const uint8_t EVENT_QUEUE_CAPACITY = 32;
QueuedEvent eventQueue[EVENT_QUEUE_CAPACITY];
uint8_t eventQueueHead = 0;
uint8_t eventQueueCount = 0;
uint32_t eventSequence = 0;
unsigned long lastEventPostAttemptAt = 0;
String bootToken;

String jsonEscape(const String &value) {
  String escaped;
  escaped.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value.charAt(i);
    switch (c) {
      case '\\': escaped += "\\\\"; break;
      case '"': escaped += "\\\""; break;
      case '\n': escaped += "\\n"; break;
      case '\r': escaped += "\\r"; break;
      case '\t': escaped += "\\t"; break;
      default:
        if (static_cast<uint8_t>(c) >= 0x20) escaped += c;
        break;
    }
  }
  return escaped;
}

bool isSafeCommandId(const String &value) {
  if (value.length() == 0 || value.length() > 64) return false;
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value.charAt(i);
    if (!isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') {
      return false;
    }
  }
  return true;
}

bool extractJsonString(const String &json, const char *key, String &value) {
  const String needle = "\"" + String(key) + "\"";
  int searchFrom = 0;
  while (true) {
    const int keyAt = json.indexOf(needle, searchFrom);
    if (keyAt < 0) return false;
    const int colonAt = json.indexOf(':', keyAt + needle.length());
    if (colonAt < 0) return false;
    int quoteAt = colonAt + 1;
    while (quoteAt < static_cast<int>(json.length()) &&
           isspace(static_cast<unsigned char>(json.charAt(quoteAt)))) {
      ++quoteAt;
    }
    if (quoteAt < static_cast<int>(json.length()) &&
        json.charAt(quoteAt) == '"') {
      String decoded;
      bool escaped = false;
      for (int i = quoteAt + 1; i < static_cast<int>(json.length()); ++i) {
        const char c = json.charAt(i);
        if (escaped) {
          decoded += c;
          escaped = false;
        } else if (c == '\\') {
          escaped = true;
        } else if (c == '"') {
          value = decoded;
          return true;
        } else {
          decoded += c;
        }
      }
      return false;
    }
    searchFrom = colonAt + 1;
  }
}

String apiBaseUrl() {
  String configured = PAPERBRIQ_API_BASE_URL;
  configured.trim();
  while (configured.endsWith("/")) configured.remove(configured.length() - 1);
  if (configured.length() > 0) return configured;

  String ssid = WiFi.SSID();
  String host = HOME_SERVER_HOST;
  if (ssid == HOTSPOT_ONE_SSID) host = HOTSPOT_ONE_SERVER_HOST;
  if (ssid == HOTSPOT_TWO_SSID) host = HOTSPOT_TWO_SERVER_HOST;
  if (host.length() == 0 || host == "0.0.0.0") return "";
  return "http://" + host + ":4173";
}

void addApiHeaders(HTTPClient &http) {
  http.addHeader("Content-Type", "application/json");
  if (strlen(IOT_API_KEY) > 0) http.addHeader("X-API-Key", IOT_API_KEY);
}

bool prepareTlsClient(WiFiClientSecure &client) {
  if (strlen(PAPERBRIQ_TLS_ROOT_CA) > 0) {
    if (time(nullptr) < 1704067200) {
      configTime(0, 0, "pool.ntp.org", "time.nist.gov");
      const unsigned long started = millis();
      while (time(nullptr) < 1704067200 && millis() - started < 8000) {
        delay(100);
      }
    }
    if (time(nullptr) < 1704067200) {
      Serial.println("HTTPS blocked: NTP clock synchronization failed");
      return false;
    }
    client.setCACert(PAPERBRIQ_TLS_ROOT_CA);
    return true;
  }

#if PAPERBRIQ_ALLOW_INSECURE_TLS
  // Report the configured TLS mode once per boot, not on every API request.
  static bool tlsWarningShown = false;
  if (!tlsWarningShown) {
    Serial.println("WARNING: HTTPS certificate verification is disabled");
    tlsWarningShown = true;
  }
  client.setInsecure();
  return true;
#else
  Serial.println("HTTPS blocked: set PAPERBRIQ_TLS_ROOT_CA in secrets.h");
  return false;
#endif
}

int httpGetJson(const String &url, String &response) {
  HTTPClient http;
  http.setConnectTimeout(4000);
  http.setTimeout(5000);
  int code = -1;
  if (url.startsWith("https://")) {
    WiFiClientSecure client;
    if (!prepareTlsClient(client) || !http.begin(client, url)) return -1;
    addApiHeaders(http);
    code = http.GET();
    if (code > 0) response = http.getString();
    http.end();
    return code;
  }

  WiFiClient client;
  if (!http.begin(client, url)) return -1;
  addApiHeaders(http);
  code = http.GET();
  if (code > 0) response = http.getString();
  http.end();
  return code;
}

int httpPostJson(const String &url, const String &payload, String &response) {
  HTTPClient http;
  http.setConnectTimeout(4000);
  http.setTimeout(5000);
  int code = -1;
  if (url.startsWith("https://")) {
    WiFiClientSecure client;
    if (!prepareTlsClient(client) || !http.begin(client, url)) return -1;
    addApiHeaders(http);
    code = http.POST(payload);
    if (code > 0) response = http.getString();
    http.end();
    return code;
  }

  WiFiClient client;
  if (!http.begin(client, url)) return -1;
  addApiHeaders(http);
  code = http.POST(payload);
  if (code > 0) response = http.getString();
  http.end();
  return code;
}

void connectWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  const unsigned long now = millis();
  if (wifiAttempted && now - lastWifiAttemptAt < 15000UL) return;
  wifiAttempted = true;
  lastWifiAttemptAt = now;
  WiFi.mode(WIFI_STA);
  Serial.print("Trying a saved Wi-Fi network");
  // One attempt per interval; keep returning to sensors and the Uno link.
  // Scanning can still take time, but do not loop here for 15+ seconds.
  wifiMulti.run(1000);
  Serial.println(WiFi.status() == WL_CONNECTED ? " connected" : " failed");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("ESP32 IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("Connected SSID: ");
    Serial.println(WiFi.SSID());
  }
}

const char *machineDisplayLabel() {
  switch (machineDisplayState) {
    case DISPLAY_STOPPED: return "STOPPED";
    case DISPLAY_READY: return "READY / CHECK";
    case DISPLAY_RUNNING: return "PROCESS RUNNING";
    case DISPLAY_COMPLETE: return "PROCESS COMPLETE";
    case DISPLAY_FAULT: return "FAULT / CHECK";
    case DISPLAY_GETTING_READY:
    default: return "GETTING READY";
  }
}

uint16_t machineDisplayColor() {
  switch (machineDisplayState) {
    case DISPLAY_READY:
    case DISPLAY_COMPLETE:
      return PB_GREEN;
    case DISPLAY_RUNNING:
      return PB_CYAN;
    case DISPLAY_STOPPED:
    case DISPLAY_FAULT:
      return PB_RED;
    case DISPLAY_GETTING_READY:
    default:
      return PB_ORANGE;
  }
}

void drawMachineStatusHeader() {
  if (!PAPERBRIQ_TFT_ENABLED) return;
  static int8_t drawnState = -1;
  if (drawnState == machineDisplayState) return;
  drawnState = machineDisplayState;

  const uint16_t color = machineDisplayColor();
  tft.fillCircle(15, 13, 6, color);
  char label[19];
  snprintf(label, sizeof(label), "%-18s", machineDisplayLabel());
  tft.setTextSize(1);
  tft.setTextColor(color, PB_CARD);
  tft.setCursor(27, 15);
  tft.print(label);
}

void setMachineDisplayState(MachineDisplayState nextState) {
  machineDisplayState = nextState;
}

void serviceDisplayHeader() {
  if (!PAPERBRIQ_TFT_ENABLED) return;
  drawMachineStatusHeader();
  static int8_t drawnWifi = -1;
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (drawnWifi != connected) {
    tft.fillCircle(143, 13, 3, connected ? PB_GREEN : PB_RED);
    drawnWifi = connected;
  }
}

void drawDashboardHeader() {
  if (!PAPERBRIQ_TFT_ENABLED) return;
  tft.fillScreen(PB_BG);
  tft.fillRoundRect(4, 3, 152, 21, 5, PB_CARD);
  tft.setTextSize(1);
  tft.setTextColor(PB_WHITE);
  tft.setCursor(27, 7);
  tft.print("PAPERBRIQ");
  drawMachineStatusHeader();
}

constexpr uint32_t TFT_PAGE_MS = 7000;
constexpr uint32_t TFT_READING_MS = 500;
constexpr int16_t TFT_PAGE_Y = 28;
constexpr int16_t TFT_PAGE_HEIGHT = 96;
constexpr int16_t TFT_STRIP_WIDTH = 19;
constexpr uint8_t TFT_STRIP_COUNT = 8;
constexpr uint8_t TFT_PAGE_COUNT = 2;
constexpr uint8_t TFT_ROW_COUNT = 3;

struct TftPageReadings {
  char text[TFT_ROW_COUNT][11];
  uint16_t color[TFT_ROW_COUNT];
};

void readTftPage(uint8_t page, TftPageReadings &readings) {
  if (page == 0) {
    const bool dhtOK = haveDhtReading && millis() - lastSampleAt < 10000UL;
    if (dhtOK) {
      // The built-in GFX font uses code page 437 for the degree symbol.
      snprintf(readings.text[0], sizeof(readings.text[0]), "%.1f %cC", lastTemp, 248);
      snprintf(readings.text[1], sizeof(readings.text[1]), "%.1f%%", lastHumidity);
    } else {
      strcpy(readings.text[0], "--");
      strcpy(readings.text[1], "--");
    }
    readings.color[0] = dhtOK ? PB_WHITE : PB_GRAY;
    readings.color[1] = dhtOK ? PB_CYAN : PB_GRAY;
    const bool waterHigh = digitalRead(FLOAT_PIN) == LOW;
    strcpy(readings.text[2], waterHigh ? "HIGH" : "LOW");
    readings.color[2] = waterHigh ? PB_CYAN : PB_RED;
  } else {
    bool inlet, outlet;
    const bool fresh = readPaperInputs(inlet, outlet);
    const bool detected[] = {inlet, outlet, digitalRead(IR_PIN) == IR_DETECTED};
    for (uint8_t row = 0; row < TFT_ROW_COUNT; ++row) {
      const bool available = row == 2 || fresh;
      const char *state = detected[row] ? "DETECTED" : "CLEAR";
      strcpy(readings.text[row], available ? state : "--");
      readings.color[row] = available && detected[row] ? PB_GREEN : PB_GRAY;
    }
  }
}

int16_t tftRowY(uint8_t row) {
  return 40 + 22 * row;
}

void drawTftReading(Adafruit_GFX &target, int16_t originX, int16_t originY,
                    uint8_t row, const TftPageReadings &readings) {
  char padded[11];
  snprintf(padded, sizeof(padded), "%10s", readings.text[row]);
  target.setTextSize(1);
  target.setTextColor(readings.color[row], PB_CARD);
  target.setCursor(88 - originX, tftRowY(row) + 6 - originY);
  target.print(padded);
}

void drawTftPageStrip(GFXcanvas16 &strip, uint8_t page, uint8_t index,
                      const TftPageReadings &readings) {
  static const char *const titles[TFT_PAGE_COUNT] = {"CHAMBER / WATER", "PAPER SENSORS"};
  static const char *const labels[TFT_PAGE_COUNT][TFT_ROW_COUNT] = {
    {"Temperature", "Humidity", "Water level"},
    {"Shredder in", "Shredder out", "Briquette"}
  };
  const int16_t x = 4 + index * TFT_STRIP_WIDTH;
  strip.fillScreen(PB_BG);
  strip.cp437(true);
  strip.setTextWrap(false);
  strip.setTextSize(1);
  strip.setTextColor(PB_CYAN);
  strip.setCursor(11 - x, 1);
  strip.print(titles[page]);
  for (uint8_t row = 0; row < TFT_ROW_COUNT; ++row) {
    const int16_t y = tftRowY(row) - TFT_PAGE_Y;
    strip.fillRoundRect(4 - x, y, TFT_STRIP_WIDTH * TFT_STRIP_COUNT, 20, 4, PB_CARD);
    strip.setTextColor(PB_GRAY);
    strip.setCursor(11 - x, y + 6);
    strip.print(labels[page][row]);
    drawTftReading(strip, x, TFT_PAGE_Y, row, readings);
  }
  strip.fillCircle(73 - x, 87, 2, page == 0 ? PB_CYAN : PB_GRAY);
  strip.fillCircle(87 - x, 87, 2, page == 1 ? PB_CYAN : PB_GRAY);
  tft.drawRGBBitmap(x, TFT_PAGE_Y, strip.getBuffer(), strip.width(), strip.height());
}

// Draw one strip per loop so UART input can be serviced during the slide.
bool serviceTft() {
  if (!PAPERBRIQ_TFT_ENABLED || tftFrozen) return false;
  serviceDisplayHeader();
  static GFXcanvas16 strip(TFT_STRIP_WIDTH, TFT_PAGE_HEIGHT);
  if (!strip.getBuffer()) return false;

  static TftPageReadings shown;
  static uint8_t page = 0, nextStrip = 0;
  static uint32_t pageSince = 0, lastReadingAt = 0;
  const uint32_t now = millis();
  if (nextStrip == TFT_STRIP_COUNT &&
      (tftNextPageRequested || now - pageSince >= TFT_PAGE_MS)) {
    page = (page + 1) % TFT_PAGE_COUNT;
    nextStrip = 0;
    tftNextPageRequested = false;
  }
  if (nextStrip < TFT_STRIP_COUNT) {
    if (nextStrip == 0) readTftPage(page, shown);
    drawTftPageStrip(strip, page, nextStrip++, shown);
    if (nextStrip == TFT_STRIP_COUNT) pageSince = lastReadingAt = millis();
    return nextStrip < TFT_STRIP_COUNT;
  }
  if (now - lastReadingAt >= TFT_READING_MS) {
    lastReadingAt = now;
    TftPageReadings readings{};
    readTftPage(page, readings);
    for (uint8_t row = 0; row < TFT_ROW_COUNT; ++row) {
      if (strcmp(readings.text[row], shown.text[row]) || readings.color[row] != shown.color[row]) {
        drawTftReading(tft, 0, 0, row, readings);
      }
    }
    shown = readings;
  }
  return false;
}

void postTelemetry(bool dhtOK, bool detected, bool waterOK) {
  if (WiFi.status() != WL_CONNECTED) return;
  const String base = apiBaseUrl();
  if (base.length() == 0) {
    Serial.println("Set PAPERBRIQ_API_BASE_URL or the local server host in secrets.h");
    return;
  }

  String payload = "{\"deviceId\":\"" + jsonEscape(DEVICE_ID) + "\",\"readings\":[";
  if (dhtOK) {
    payload += "{\"name\":\"Temperature\",\"value\":" + String(lastTemp, 1) + ",\"unit\":\"C\",\"status\":\"Normal\"},";
    payload += "{\"name\":\"Humidity\",\"value\":" + String(lastHumidity, 1) + ",\"unit\":\"%RH\",\"status\":\"Normal\"},";
  }
  payload += "{\"name\":\"IR Sensor\",\"value\":" + String(detected ? 1 : 0) + ",\"unit\":\"digital\",\"status\":\"Normal\"},";
  bool inlet, outlet;
  if (readPaperInputs(inlet, outlet)) {
    payload += "{\"name\":\"Shredder Inlet\",\"value\":" + String(inlet ? 1 : 0) + ",\"unit\":\"digital\",\"status\":\"Normal\"},";
    payload += "{\"name\":\"Shredder Outlet\",\"value\":" + String(outlet ? 1 : 0) + ",\"unit\":\"digital\",\"status\":\"Normal\"},";
  }
  payload += "{\"name\":\"Rainwater Level\",\"value\":" + String(waterOK ? 100 : 0) + ",\"unit\":\"%\",\"status\":\"" + String(waterOK ? "Normal" : "Warning") + "\"}";
  payload += "],\"hardware\":{";
  payload += "\"dht22\":\"" + String(dhtOK ? "Online" : "Attention") + "\",";
  payload += "\"irSensor\":\"Manual check\",\"tftDisplay\":\"" + String(PAPERBRIQ_TFT_ENABLED ? "Manual check" : "Disabled") + "\",\"floatSwitch\":\"Manual check\"},";
  const bool linkFresh = haveHealthReply && millis() - healthReplyAt < HEALTH_FRESH_MS;
  payload += "\"checks\":{\"version\":" + String(healthTimedVersion ? 3 : healthChamberSupported ? 2 : 1) + ",\"unoLink\":" + String(linkFresh ? "true" : "false");
  payload += ",\"controlIo\":" + String(linkFresh && healthControlIo ? "true" : "false");
  payload += ",\"stopClosed\":" + String(linkFresh && healthStopClosed ? "true" : "false");
  payload += ",\"emergencyClosed\":" + String(linkFresh && healthEmergencyClosed ? "true" : "false");
  payload += ",\"indicatorIo\":" + String(linkFresh && healthIndicatorIo ? "true" : "false");
  payload += ",\"armed\":" + String(linkFresh && healthArmed ? "true" : "false");
  if (linkFresh && healthBatchMask >= 0)
    payload += ",\"batchMask\":" + String(healthBatchMask);
  payload += ",\"doorClosed\":" + String(linkFresh && healthDoorClosed ? "true" : "false");
  payload += ",\"fanOn\":" + String(linkFresh && healthFanOn ? "true" : "false");
  payload += ",\"unlockOn\":" + String(linkFresh && healthUnlockOn ? "true" : "false");
  payload += ",\"testActive\":" + String(linkFresh && healthTestActive ? "true" : "false");
  payload += ",\"timedSupported\":" + String(linkFresh && healthTimedSupported ? "true" : "false");
  payload += ",\"heaterOn\":" + String(linkFresh && healthHeaterOn ? "true" : "false");
  payload += ",\"pumpOn\":" + String(linkFresh && healthPumpOn ? "true" : "false");
  payload += ",\"heaterReady\":" + String(linkFresh && healthHeaterReady ? "true" : "false");
  payload += ",\"pumpReady\":" + String(linkFresh && healthPumpReady ? "true" : "false");
  if (linkFresh && productionJson.length() && millis() - productionReplyAt < HEALTH_FRESH_MS)
    payload += ",\"production\":" + productionJson.substring(0, productionJson.length() - 1)
        + ",\"sampleAgeMs\":" + String(millis() - productionReplyAt) + "}";
  payload += ",\"dht\":" + String(dhtOK ? "true" : "false") + "}}";

  String response;
  const int code = httpPostJson(base + "/api/iot/readings", payload, response);
  Serial.print("PaperBriq telemetry POST: ");
  Serial.println(code);
}

void enqueueDeviceEvent(String type, String severity, String component,
                        String processState, String message,
                        const String &commandId = "") {
  type.toLowerCase();
  severity.toLowerCase();
  component.toLowerCase();
  processState.toLowerCase();
  message.replace('_', ' ');

  if (eventQueueCount == EVENT_QUEUE_CAPACITY) {
    Serial.println("Device event queue full; dropping oldest event");
    eventQueueHead = (eventQueueHead + 1) % EVENT_QUEUE_CAPACITY;
    --eventQueueCount;
  }
  const uint8_t index = (eventQueueHead + eventQueueCount) % EVENT_QUEUE_CAPACITY;
  QueuedEvent &event = eventQueue[index];
  event.eventId = bootToken + "-" + String(++eventSequence);
  event.type = type;
  event.severity = severity;
  event.component = component;
  event.processState = processState;
  event.message = message.substring(0, 160);
  event.commandId = commandId;
  ++eventQueueCount;
}

void serviceEventQueue() {
  if (eventQueueCount == 0 || WiFi.status() != WL_CONNECTED) return;
  const unsigned long now = millis();
  if (now - lastEventPostAttemptAt < EVENT_RETRY_INTERVAL_MS) return;
  lastEventPostAttemptAt = now;

  const String base = apiBaseUrl();
  if (base.length() == 0) return;
  QueuedEvent &event = eventQueue[eventQueueHead];
  String payload = "{\"deviceId\":\"" + jsonEscape(DEVICE_ID) + "\",";
  payload += "\"eventId\":\"" + jsonEscape(event.eventId) + "\",";
  payload += "\"type\":\"" + jsonEscape(event.type) + "\",";
  payload += "\"severity\":\"" + jsonEscape(event.severity) + "\",";
  payload += "\"component\":\"" + jsonEscape(event.component) + "\",";
  payload += "\"message\":\"" + jsonEscape(event.message) + "\",";
  payload += "\"processState\":\"" + jsonEscape(event.processState) + "\"";
  if (event.commandId.length() > 0) {
    payload += ",\"commandId\":\"" + jsonEscape(event.commandId) + "\"";
  }
  payload += ",\"metadata\":{}}";

  String response;
  const int code = httpPostJson(base + "/api/device/events", payload, response);
  Serial.print("Device event POST: ");
  Serial.println(code);
  if (code >= 200 && code < 300) {
    event = QueuedEvent();
    eventQueueHead = (eventQueueHead + 1) % EVENT_QUEUE_CAPACITY;
    --eventQueueCount;
  }
}

bool postCommandAck(const String &id, const String &status,
                    const String &message) {
  Serial.print("Command result: "); Serial.print(status);
  Serial.print(" | "); Serial.print(message);
  Serial.print(" | id="); Serial.println(id);
  if (WiFi.status() != WL_CONNECTED || !isSafeCommandId(id)) return false;
  const String base = apiBaseUrl();
  if (base.length() == 0) return false;
  String payload = "{\"deviceId\":\"" + jsonEscape(DEVICE_ID) + "\",";
  payload += "\"status\":\"" + jsonEscape(status) + "\",";
  payload += "\"message\":\"" + jsonEscape(message.substring(0, 240)) + "\"}";
  String response;
  const int code = httpPostJson(base + "/api/device/commands/" + id + "/ack",
                                payload, response);
  Serial.print("ACK report HTTP: ");
  Serial.println(code);
  return code >= 200 && code < 300;
}

void serviceAckReport() {
  if (pendingCommandId.length() > 0 || ackReportId.length() == 0 ||
      WiFi.status() != WL_CONNECTED ||
      millis() - lastAckReportAttemptAt < 1000UL) return;
  lastAckReportAttemptAt = millis();
  // The cached final result is also retried if the backend returns the same
  // command again. Do not block all later commands on a rejected HTTP report.
  postCommandAck(ackReportId, ackReportStatus, ackReportMessage);
  ackReportId = "";
  ackReportStatus = "";
  ackReportMessage = "";
}

void cacheFinalCommand(const String &id, const String &status,
                       const String &message) {
  lastCommandId = id;
  lastCommandStatus = status;
  lastCommandMessage = message;
}

bool isBatchCommand(const String &action) {
  return action.length() == 7 && action.startsWith("batch_") && action[6] >= '1' && action[6] <= '3';
}

void dispatchCommand(const String &id, String action) {
  action.toLowerCase();
  String timedAction = action; timedAction.toUpperCase();
  const bool timed = timedCommandSeconds(timedAction.c_str()) > 0;
  const bool batch = isBatchCommand(action);
  char kind; bool edit; uint32_t run, duration;
  const bool productionCommand = parseProductionCommand(timedAction.c_str(), kind, edit, run, duration);
  if (!isSafeCommandId(id) ||
      (action != "start" && action != "stop" && action != "reset" && action != "self_test" && !batch && !timed && !productionCommand)) {
    postCommandAck(id, "failed", "unsupported command");
    cacheFinalCommand(id, "failed", "unsupported command");
    enqueueDeviceEvent("command_rejected", "warning", "controller", "stopped",
                       "Unsupported command", id);
    return;
  }

  pendingCommandId = id;
  pendingCommandAction = action;
  pendingCommandSentAt = millis();
  if (action == "start" || action == "self_test") processCommandId = id;
  if (action == "start" || action == "reset") {
    setMachineDisplayState(DISPLAY_GETTING_READY);
  } else if (action == "stop") {
    setMachineDisplayState(DISPLAY_STOPPED);
  }
  String protocolAction = batch ? "B" + action.substring(6) : action;
  protocolAction.toUpperCase();
  sendUnoFrame("CMD|" + id + "|" + protocolAction);
  Serial.print("Sent command to Uno: ");
  Serial.print(protocolAction);
  Serial.print(" | id="); Serial.println(id);
}

void pollNextCommand() {
  if (pendingCommandId.length() > 0 || ackReportId.length() > 0 ||
      WiFi.status() != WL_CONNECTED) return;
  const String base = apiBaseUrl();
  if (base.length() == 0) return;
  String response;
  const int code = httpGetJson(base + "/api/device/commands/next?deviceId=" +
                               String(DEVICE_ID), response);
  if (code != 200) {
    Serial.print("Command poll failed: ");
    Serial.println(code);
    return;
  }
  if (response.indexOf("\"command\":null") >= 0) return;

  String id;
  String action;
  if (!extractJsonString(response, "id", id) ||
      !extractJsonString(response, "command", action)) {
    Serial.println("Command poll returned malformed JSON");
    enqueueDeviceEvent("command_response_invalid", "error", "network", "fault",
                       "Malformed command response");
    return;
  }

  if (id == lastCommandId && lastCommandStatus.length() > 0) {
    // Retry only the cached acknowledgement; never execute an ID twice.
    postCommandAck(id, lastCommandStatus, lastCommandMessage);
    return;
  }
  dispatchCommand(id, action);
}

void finishPendingCommand(const String &status, String message) {
  message.replace('_', ' ');
  Serial.print("Command status: "); Serial.print(status);
  Serial.print(" | "); Serial.println(message);
  ackReportId = pendingCommandId;
  ackReportStatus = status;
  ackReportMessage = message;
  if (status == "completed" || status == "failed") {
    if (status == "failed") setMachineDisplayState(DISPLAY_FAULT);
    cacheFinalCommand(pendingCommandId, status, message);
    if (status == "failed" && (pendingCommandAction == "start" || pendingCommandAction == "self_test") &&
        processCommandId == pendingCommandId) {
      processCommandId = "";
    }
    pendingCommandId = "";
    pendingCommandAction = "";
    pendingCommandSentAt = 0;
  }
}

void handleUnoAck(const String &line) {
  const int first = line.indexOf('|');
  const int second = line.indexOf('|', first + 1);
  const int third = line.indexOf('|', second + 1);
  if (first < 0 || second < 0 || third < 0) return;
  const String id = line.substring(first + 1, second);
  String status = line.substring(second + 1, third);
  String message = line.substring(third + 1);
  status.toLowerCase();
  if (pendingCommandId.length() == 0 || id != pendingCommandId) {
    Serial.println("Ignoring ACK for a non-pending command");
    return;
  }
  if (status == "delivered" || status == "completed" || status == "failed") {
    finishPendingCommand(status, message);
  }
}

void handleUnoEvent(const String &line) {
  String fields[5];
  int from = 6; // Skip "EVENT|".
  for (uint8_t i = 0; i < 4; ++i) {
    const int separator = line.indexOf('|', from);
    if (separator < 0) return;
    fields[i] = line.substring(from, separator);
    from = separator + 1;
  }
  fields[4] = line.substring(from);

  String commandId;
  String eventType = fields[0];
  eventType.toUpperCase();
  String severity = fields[1];
  severity.toLowerCase();
  String processState = fields[3];
  processState.toLowerCase();

  if (eventType == "PROCESS_STARTED" && isBatchCommand(pendingCommandAction)) {
    processCommandId = pendingCommandId;
  }

  if (eventType == "FAULT" || severity == "error" ||
      severity == "critical") {
    setMachineDisplayState(DISPLAY_FAULT);
  } else if (eventType == "STOPPED" || eventType == "CONTROLLER_READY") {
    setMachineDisplayState(DISPLAY_STOPPED);
  } else if (eventType == "ARMED") {
    setMachineDisplayState(DISPLAY_READY);
  } else if (eventType == "PROCESS_STARTED") {
    setMachineDisplayState(DISPLAY_GETTING_READY);
  } else if (eventType == "PROCESS_COMPLETED" || eventType == "TEST_COMPLETED") {
    setMachineDisplayState(DISPLAY_COMPLETE);
  } else if (processState == "running") {
    setMachineDisplayState(DISPLAY_RUNNING);
  }

  if ((eventType == "STOPPED" || eventType == "ARMED") &&
      pendingCommandId.length() > 0) {
    commandId = pendingCommandId;
  } else {
    commandId = processCommandId;
  }
  enqueueDeviceEvent(fields[0], fields[1], fields[2], fields[3], fields[4],
                     commandId);
  if (eventType == "PROCESS_COMPLETED" || eventType == "TEST_COMPLETED" || eventType == "TEST_ABORTED" || eventType == "STOPPED" ||
      eventType == "FAULT") {
    processCommandId = "";
  }
}

void handleUnoHealth(const String &line) {
  const int separator = line.indexOf('|', 7);
  if (separator < 0 || healthProbeId.length() == 0 ||
      line.substring(7, separator) != healthProbeId ||
      millis() - lastHealthProbeAt > HEALTH_REPLY_TIMEOUT_MS) return;
  const String flags = line.substring(separator + 1);
  if (flags.length() != 9 && flags.length() != 17 && flags.length() != 27 && flags.length() != 29) return;
  for (uint8_t i = 0; i < flags.length(); ++i) {
    const char maximum = i == 28 ? '7' : '1';
    if (i % 2 == 1 ? flags[i] != '|' : (flags[i] < '0' || flags[i] > maximum)) return;
  }
  healthBatchMask = flags.length() == 29 ? flags[28] - '0' : -1;
  healthControlIo = flags[0] == '1';
  healthStopClosed = flags[2] == '1';
  healthEmergencyClosed = flags[4] == '1';
  healthIndicatorIo = flags[6] == '1';
  healthArmed = flags[8] == '1';
  healthChamberSupported = flags.length() >= 17;
  healthDoorClosed = healthChamberSupported && flags[10] == '1';
  healthFanOn = healthChamberSupported && flags[12] == '1';
  healthUnlockOn = healthChamberSupported && flags[14] == '1';
  healthTestActive = healthChamberSupported && flags[16] == '1';
  healthTimedVersion = flags.length() >= 27;
  healthTimedSupported = healthTimedVersion && flags[18] == '1';
  healthHeaterOn = healthTimedVersion && flags[20] == '1';
  healthPumpOn = healthTimedVersion && flags[22] == '1';
  healthHeaterReady = healthTimedVersion && flags[24] == '1';
  healthPumpReady = healthTimedVersion && flags[26] == '1';
  healthReplyAt = millis();
  haveHealthReply = true;
  healthProbeId = "";
}

void handleProductionReport(const String &line) {
  int from = line.indexOf('|', 5);
  if (from < 0 || !healthProbeId.length() || line.substring(5, from) != healthProbeId ||
      millis() - lastHealthProbeAt > HEALTH_REPLY_TIMEOUT_MS) return;
  const bool thermalReport = line.startsWith("PRO2|");
  const uint8_t count = thermalReport ? 15 : 10;
  uint32_t values[15];
  for (uint8_t i = 0; i < count; ++i) {
    int end = line.indexOf('|', from + 1);
    if ((i < count - 1 && end < 0) || (i == count - 1 && end >= 0)) return;
    String part = end < 0 ? line.substring(from + 1) : line.substring(from + 1, end);
    if (!part.length() || part.length() > 10) return;
    uint64_t value = 0;
    for (size_t j = 0; j < part.length(); ++j) {
      if (part[j] < '0' || part[j] > '9') return;
      value = value * 10 + part[j] - '0';
    }
    if (value > 0xFFFFFFFFULL) return;
    values[i] = (uint32_t)value; from = end;
  }
  if (values[0] > 2147483 || values[1] > 2147483 || values[2] > 1 || values[3] > 1 ||
      values[4] > (thermalReport ? 3 : 2) || values[5] > 5 || values[7] > 2147483 || values[9] > values[7]) return;
  if (thermalReport && (values[10] > 600 || values[11] < 30 || values[11] > 50 ||
      values[12] > 1 || values[13] > 4 || values[14] > 1)) return;
  const char *names[] = {"pumpMaxSeconds", "dryingMaxSeconds", "pumpVerified", "dryingVerified",
      "kind", "phase", "runId", "totalSeconds", "elapsedSeconds", "remainingSeconds",
      "preheatMaxSeconds", "targetC", "temperatureFresh", "thermalStage", "heaterFanConfirmed"};
  productionJson = thermalReport ? "{\"version\":2" : "{\"version\":1";
  for (uint8_t i = 0; i < count; ++i) {
    productionJson += ",\"" + String(names[i]) + "\":" + String(values[i]);
  }
  productionJson += '}'; productionReplyAt = millis();
}

void serviceHealthProbe() {
  const unsigned long now = millis();
  if (pendingCommandId.length() > 0 || now - lastHealthProbeAt < HEALTH_PROBE_INTERVAL_MS) return;
  lastHealthProbeAt = now;
  healthProbeId = "health-" + String(now, HEX);
  sendUnoFrame("CMD|" + healthProbeId + "|CHECK");
}

void sampleChamberSensor() {
  const unsigned long now = millis();
  if (now - lastSampleAt < SAMPLE_INTERVAL_MS) return;
  lastSampleAt = now;
  const float temperature = dht.readTemperature();
  const float humidity = dht.readHumidity();
  haveDhtReading = isfinite(temperature) && isfinite(humidity) &&
      temperature >= -40 && temperature <= 80 && humidity >= 0 && humidity <= 100;
  if (haveDhtReading) { lastTemp = temperature; lastHumidity = humidity; }
  printChamberSensor();
}

void printChamberSensor() {
  Serial.print("Chamber DHT22 GPIO27: ");
  if (!haveDhtReading || millis() - lastSampleAt >= 10000UL) {
    Serial.println("no fresh valid reading; check sensor connection");
    return;
  }
  Serial.print(lastTemp, 1); Serial.print(" C, ");
  Serial.print(lastHumidity, 1); Serial.println(" %RH");
}

void serviceSensorSerial() {
  while (Serial.available() > 0) {
    const char c = Serial.read();
    if (PAPERBRIQ_TFT_ENABLED && c == '>') {
      tftNextPageRequested = true;
    } else if (PAPERBRIQ_TFT_ENABLED && c == '~') {
      tftFrozen = !tftFrozen;
      Serial.println(tftFrozen ? "TFT frozen; Wi-Fi and Uno link still active" : "TFT updates resumed");
    } else if (c == 's' || c == 'S') {
      sampleChamberSensor();
      printChamberSensor();
      bool inlet, outlet;
      if (readPaperInputs(inlet, outlet)) {
        Serial.print("Shredder IN GPIO26: "); Serial.println(inlet ? "DETECTED" : "CLEAR");
        Serial.print("Shredder OUT GPIO19: "); Serial.println(outlet ? "DETECTED" : "CLEAR");
      } else Serial.println("Shredder sensors: no fresh sample");
    }
  }
}

void replyDhtCheck(const String &request) {
  const String id = request.substring(10); // DHT_CHECK|<request number>
  if (id.length() == 0 || id.length() > 10) return;
  for (size_t i = 0; i < id.length(); i++) if (!isDigit(id[i])) return;
  sampleChamberSensor();
  const bool fresh = haveDhtReading && millis() - lastSampleAt < 3000UL;
  if (!fresh) {
    sendUnoFrame("DHT|" + id + "|ERROR");
    return;
  }
  sendUnoFrame("DHT|" + id + "|OK|" + String(lastTemp, 1) + "|" + String(lastHumidity, 1));
}

void serviceUnoLink() {
  while (unoLink.available() > 0) {
    const char c = static_cast<char>(unoLink.read());
    lastUnoByteAt = millis();
    if (c == '\n') {
      unoLine.trim();
      if (unoLine.length() > 0) {
        // Keep the UART link observable in the USB Serial Monitor. Previously
        // valid ACK/EVENT frames were handled silently, which made a working
        // Uno-to-ESP connection look disconnected during troubleshooting.
        Serial.print("Uno RX: ");
        Serial.println(unoLine);

        if (unoLine.startsWith("ACK|")) handleUnoAck(unoLine);
        else if (unoLine.startsWith("EVENT|")) handleUnoEvent(unoLine);
        else if (unoLine.startsWith("PROD|") || unoLine.startsWith("PRO2|")) handleProductionReport(unoLine);
        else if (unoLine.startsWith("HEALTH|")) handleUnoHealth(unoLine);
        else if (unoLine.startsWith("DHT_CHECK|")) {
          pendingDhtRequest = unoLine;
          dhtRequestAt = millis();
        }
      }
      unoLine = "";
    } else if (c != '\r') {
      if (unoLine.length() < 159) unoLine += c;
      else {
        unoLine = "";
        setMachineDisplayState(DISPLAY_FAULT);
        enqueueDeviceEvent("uart_frame_overflow", "error", "controller", "fault",
                           "Uno link frame overflow");
      }
    }
  }

  if (pendingCommandId.length() > 0 &&
      millis() - pendingCommandSentAt >= UNO_ACK_TIMEOUT_MS) {
    const String failedId = pendingCommandId;
    const String failedAction = pendingCommandAction;
    const bool delivered = ackReportId == failedId && ackReportStatus == "delivered";
    const String reason = delivered ? "Uno received command but final acknowledgement timed out" :
                                      "No matching Uno acknowledgement within 5 seconds";
    Serial.print("UART ACK TIMEOUT: "); Serial.println(reason);
    finishPendingCommand("failed", reason);
    if (failedAction == "start" || failedAction == "self_test") processCommandId = "";
    setMachineDisplayState(DISPLAY_FAULT);
    enqueueDeviceEvent("controller_link_timeout", "critical", "controller", "fault",
                       "Arduino Uno did not acknowledge command", failedId);

    // If Uno RX works but its ACK path failed, force a latched stop anyway.
    sendUnoFrame("CMD|failsafe-stop|STOP");
    return;
  }

  // The Uno cannot receive while sending a burst of ACKs or events.
  if (pendingDhtRequest.length() > 0) {
    if (millis() - dhtRequestAt >= 10000UL) {
      pendingDhtRequest = "";
    } else if (!pendingCommandId.length() && !unoLine.length() &&
               !unoLink.available() && millis() - lastUnoByteAt >= 100UL) {
      replyDhtCheck(pendingDhtRequest);
      pendingDhtRequest = "";
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("PaperBriq monitor starting - automatic Wi-Fi and app connection");
  Serial.println("Firmware: sensor-pages-v10 | USB S: sensors, >: next page, ~: freeze TFT");
  const esp_reset_reason_t resetReason = esp_reset_reason();
  if (resetReason == ESP_RST_BROWNOUT) Serial.println("ESP32 reset reason: brownout");
  else Serial.printf("ESP32 reset reason: %d\n", static_cast<int>(resetReason));

  if (PAPERBRIQ_TFT_ENABLED) {
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
    SPI.begin(TFT_SCK, -1, TFT_MOSI, TFT_CS);
    Serial.println("Initializing TFT");
    tft.initR(INITR_BLACKTAB);
    tft.setSPISpeed(1000000);
    tft.setRotation(1);
    Serial.print("TFT drawing SPI: 1 MHz; rotation: ");
    Serial.println(tft.getRotation());
    tft.setTextWrap(false);
    tft.cp437(true);
    drawDashboardHeader();
    Serial.println("TFT dashboard draw complete");
  } else {
    Serial.println("TFT disabled; chamber DHT22 and app telemetry remain active");
  }

  // Preserve bursts of ACK/EVENT frames during unavoidable network requests.
  unoLink.setRxBufferSize(2048);
  unoLink.begin(UNO_LINK_BAUD, SERIAL_8N1, UNO_LINK_RX, UNO_LINK_TX);
  unoTxMutex = xSemaphoreCreateMutex();
  bootToken = String(static_cast<uint32_t>(ESP.getEfuseMac()), HEX) + "-" +
              String(millis(), HEX);
  dht.begin();
  pinMode(IR_PIN, INPUT);
  pinMode(SHREDDER_IN_PIN, INPUT_PULLUP);
  pinMode(SHREDDER_OUT_PIN, INPUT_PULLUP);
  if (!unoTxMutex || xTaskCreate(paperSensorTask, "paper-inputs", 3072, nullptr, 1, nullptr) != pdPASS) {
    Serial.println("Paper input task failed; automatic paper trigger unavailable");
  }
  pinMode(FLOAT_PIN, INPUT_PULLUP);
  Serial.println("Sensors and Uno link initialized");
  wifiMulti.addAP(HOME_WIFI_SSID, HOME_WIFI_PASSWORD);
  wifiMulti.addAP(HOTSPOT_ONE_SSID, HOTSPOT_ONE_PASSWORD);
  wifiMulti.addAP(HOTSPOT_TWO_SSID, HOTSPOT_TWO_PASSWORD);
  Serial.println("ESP32/Uno command bridge ready; Uno remains stopped until RESET");
}

void loop() {
  // Sample and service local diagnostics before potentially slow networking.
  sampleChamberSensor();
  serviceSensorSerial();
  serviceUnoLink();
  // Keep reading UART until the final ACK or the existing 5-second deadline.
  // No Wi-Fi reconnect, telemetry, or event upload may consume that window.
  if (pendingCommandId.length() > 0) { delay(1); return; }
  if (serviceTft()) return;
  const unsigned long now = millis();
  if (WiFi.status() != WL_CONNECTED) connectWifi();
  sampleChamberSensor();
  serviceSensorSerial();
  serviceUnoLink();

  serviceAckReport();
  serviceUnoLink();

  if (now - lastCommandPollAt >= COMMAND_POLL_INTERVAL_MS) {
    lastCommandPollAt = now;
    pollNextCommand();
  }

  serviceUnoLink();
  if (pendingCommandId.length() > 0) { delay(1); return; }
  serviceEventQueue();
  serviceUnoLink();

  sampleChamberSensor();
  const unsigned long telemetryNow = millis();
  if (telemetryNow - lastPostAt >= POST_INTERVAL_MS) {
    lastPostAt = telemetryNow;
    const bool dhtOK = haveDhtReading && telemetryNow - lastSampleAt < 10000UL;
    const bool detected = digitalRead(IR_PIN) == IR_DETECTED;
    const bool waterOK = digitalRead(FLOAT_PIN) == LOW;
    postTelemetry(dhtOK, detected, waterOK);
  }
  serviceUnoLink();
  serviceHealthProbe();
}
