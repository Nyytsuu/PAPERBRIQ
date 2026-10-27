#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <time.h>
#include "secrets.h"

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
// The chamber sensor and app work independently of the disconnected TFT.
// Set to 1 in secrets.h when the display is connected again.
#ifndef PAPERBRIQ_TFT_ENABLED
#define PAPERBRIQ_TFT_ENABLED 0
#endif
#define IR_PIN 25
#define IR_DETECTED LOW
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
bool healthChamberSupported = false;
bool healthDoorClosed = false;
bool healthFanOn = false;
bool healthUnlockOn = false;
bool healthTestActive = false;
const unsigned long HEALTH_PROBE_INTERVAL_MS = 7000;
const unsigned long HEALTH_REPLY_TIMEOUT_MS = 5000;
const unsigned long HEALTH_FRESH_MS = 20000;
const unsigned long SAMPLE_INTERVAL_MS = 2500;
const unsigned long POST_INTERVAL_MS = 5000;
const unsigned long COMMAND_POLL_INTERVAL_MS = 2000;
const unsigned long UNO_ACK_TIMEOUT_MS = 5000;
const unsigned long EVENT_RETRY_INTERVAL_MS = 1000;
const unsigned long DISPLAY_STATUS_BLINK_MS = 400;

enum MachineDisplayState : uint8_t {
  DISPLAY_GETTING_READY,
  DISPLAY_STOPPED,
  DISPLAY_READY,
  DISPLAY_RUNNING,
  DISPLAY_COMPLETE,
  DISPLAY_FAULT
};

MachineDisplayState machineDisplayState = DISPLAY_GETTING_READY;
unsigned long lastDisplayStatusBlinkAt = 0;
bool displayStatusBlinkOn = true;

String unoLine;
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

bool machineDisplayShouldBlink() {
  return machineDisplayState == DISPLAY_GETTING_READY ||
         machineDisplayState == DISPLAY_COMPLETE ||
         machineDisplayState == DISPLAY_FAULT;
}

void drawMachineStatusHeader() {
  if (!PAPERBRIQ_TFT_ENABLED) return;
  const uint16_t color = machineDisplayColor();
  tft.fillCircle(15, 13, 6,
                 !machineDisplayShouldBlink() || displayStatusBlinkOn
                     ? color
                     : PB_CARD);
  tft.fillRect(27, 14, 108, 9, PB_CARD);
  tft.setTextSize(1);
  tft.setTextColor(color);
  tft.setCursor(27, 15);
  tft.print(machineDisplayLabel());
}

void setMachineDisplayState(MachineDisplayState nextState) {
  machineDisplayState = nextState;
  displayStatusBlinkOn = true;
  lastDisplayStatusBlinkAt = millis();
  drawMachineStatusHeader();
}

void serviceMachineDisplayStatus() {
  if (!machineDisplayShouldBlink()) return;
  const unsigned long now = millis();
  if (now - lastDisplayStatusBlinkAt < DISPLAY_STATUS_BLINK_MS) return;
  lastDisplayStatusBlinkAt = now;
  displayStatusBlinkOn = !displayStatusBlinkOn;
  drawMachineStatusHeader();
}

void drawStaticDashboard() {
  if (!PAPERBRIQ_TFT_ENABLED) return;
  tft.fillScreen(PB_BG);
  tft.fillRoundRect(4, 3, 152, 21, 5, PB_CARD);
  tft.setTextSize(1);
  tft.setTextColor(PB_WHITE);
  tft.setCursor(27, 7);
  tft.print("PAPERBRIQ");
  drawMachineStatusHeader();

  tft.fillRoundRect(4, 28, 74, 51, 5, PB_CARD);
  tft.fillRoundRect(82, 28, 74, 51, 5, PB_CARD);
  tft.fillRoundRect(4, 83, 74, 38, 5, PB_CARD);
  tft.fillRoundRect(82, 83, 74, 38, 5, PB_CARD);
  tft.setTextColor(PB_GRAY);
  tft.setCursor(10, 33); tft.print("TEMPERATURE");
  tft.setCursor(88, 33); tft.print("HUMIDITY");
  tft.setCursor(10, 88); tft.print("BRIQUETTE");
  tft.setCursor(88, 88); tft.print("WATER LEVEL");

  tft.drawRoundRect(12, 49, 6, 17, 3, PB_ORANGE);
  tft.fillCircle(15, 66, 5, PB_ORANGE);
  tft.drawLine(15, 52, 15, 64, PB_ORANGE);
  tft.fillTriangle(96, 48, 90, 58, 102, 58, PB_CYAN);
  tft.fillCircle(96, 58, 6, PB_CYAN);
}

void drawValues(bool dhtOK, bool detected, bool waterOK) {
  if (!PAPERBRIQ_TFT_ENABLED) return;
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
  payload += "{\"name\":\"Rainwater Level\",\"value\":" + String(waterOK ? 100 : 0) + ",\"unit\":\"%\",\"status\":\"" + String(waterOK ? "Normal" : "Warning") + "\"}";
  payload += "],\"hardware\":{";
  payload += "\"dht22\":\"" + String(dhtOK ? "Online" : "Attention") + "\",";
  payload += "\"irSensor\":\"Manual check\",\"tftDisplay\":\"" + String(PAPERBRIQ_TFT_ENABLED ? "Manual check" : "Disabled") + "\",\"floatSwitch\":\"Manual check\"},";
  const bool linkFresh = haveHealthReply && millis() - healthReplyAt < HEALTH_FRESH_MS;
  payload += "\"checks\":{\"version\":" + String(healthChamberSupported ? 2 : 1) + ",\"unoLink\":" + String(linkFresh ? "true" : "false");
  payload += ",\"controlIo\":" + String(linkFresh && healthControlIo ? "true" : "false");
  payload += ",\"stopClosed\":" + String(linkFresh && healthStopClosed ? "true" : "false");
  payload += ",\"emergencyClosed\":" + String(linkFresh && healthEmergencyClosed ? "true" : "false");
  payload += ",\"indicatorIo\":" + String(linkFresh && healthIndicatorIo ? "true" : "false");
  payload += ",\"armed\":" + String(linkFresh && healthArmed ? "true" : "false");
  payload += ",\"doorClosed\":" + String(linkFresh && healthDoorClosed ? "true" : "false");
  payload += ",\"fanOn\":" + String(linkFresh && healthFanOn ? "true" : "false");
  payload += ",\"unlockOn\":" + String(linkFresh && healthUnlockOn ? "true" : "false");
  payload += ",\"testActive\":" + String(linkFresh && healthTestActive ? "true" : "false");
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

void dispatchCommand(const String &id, String action) {
  action.toLowerCase();
  if (!isSafeCommandId(id) ||
      (action != "start" && action != "stop" && action != "reset" && action != "self_test")) {
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
  String protocolAction = action;
  protocolAction.toUpperCase();
  unoLink.print("CMD|");
  unoLink.print(id);
  unoLink.print('|');
  unoLink.println(protocolAction);
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
  if (flags.length() != 9 && flags.length() != 17) return;
  for (uint8_t i = 0; i < flags.length(); ++i) {
    if (i % 2 == 1 ? flags[i] != '|' : (flags[i] != '0' && flags[i] != '1')) return;
  }
  healthControlIo = flags[0] == '1';
  healthStopClosed = flags[2] == '1';
  healthEmergencyClosed = flags[4] == '1';
  healthIndicatorIo = flags[6] == '1';
  healthArmed = flags[8] == '1';
  healthChamberSupported = flags.length() == 17;
  healthDoorClosed = healthChamberSupported && flags[10] == '1';
  healthFanOn = healthChamberSupported && flags[12] == '1';
  healthUnlockOn = healthChamberSupported && flags[14] == '1';
  healthTestActive = healthChamberSupported && flags[16] == '1';
  healthReplyAt = millis();
  haveHealthReply = true;
  healthProbeId = "";
}

void serviceHealthProbe() {
  const unsigned long now = millis();
  if (pendingCommandId.length() > 0 || now - lastHealthProbeAt < HEALTH_PROBE_INTERVAL_MS) return;
  lastHealthProbeAt = now;
  healthProbeId = "health-" + String(now, HEX);
  unoLink.println("CMD|" + healthProbeId + "|CHECK");
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
    if (c == 's' || c == 'S') {
      sampleChamberSensor();
      printChamberSensor();
    }
  }
}

void replyDhtCheck(const String &request) {
  const String id = request.substring(10); // DHT_CHECK|<request number>
  if (id.length() == 0 || id.length() > 10) return;
  for (size_t i = 0; i < id.length(); i++) if (!isDigit(id[i])) return;
  sampleChamberSensor();
  const bool fresh = haveDhtReading && millis() - lastSampleAt < 10000UL;
  unoLink.print("DHT|"); unoLink.print(id);
  if (!fresh) {
    unoLink.println("|ERROR");
    return;
  }
  unoLink.print("|OK|"); unoLink.print(lastTemp, 1);
  unoLink.print('|'); unoLink.println(lastHumidity, 1);
}

void serviceUnoLink() {
  while (unoLink.available() > 0) {
    const char c = static_cast<char>(unoLink.read());
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
        else if (unoLine.startsWith("HEALTH|")) handleUnoHealth(unoLine);
        else if (unoLine.startsWith("DHT_CHECK|")) replyDhtCheck(unoLine);
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
    unoLink.println("CMD|failsafe-stop|STOP");
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("PaperBriq monitor starting - automatic Wi-Fi and app connection");
  Serial.println("Firmware: ack-diagnostics-v4 | DHT22 GPIO27 | USB S and Uno S supported");

  if (PAPERBRIQ_TFT_ENABLED) {
  // Match the working TFT-only test before initializing other peripherals.
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
  SPI.begin(TFT_SCK, -1, TFT_MOSI, TFT_CS);
  Serial.println("Initializing TFT");
  tft.initR(INITR_BLACKTAB);
  // Keep the conservative drawing speed used during TFT troubleshooting.
  tft.setSPISpeed(1000000);
  // Flip the previous landscape rotation (1) by 180 degrees.
  tft.setRotation(3);
  Serial.print("TFT drawing SPI: 1 MHz; rotation: ");
  Serial.println(tft.getRotation());
  tft.setTextWrap(false);
  drawStaticDashboard();
  Serial.println("TFT dashboard draw complete");
  } else {
    Serial.println("TFT disabled; chamber DHT22 and app telemetry remain active");
  }

  // Preserve bursts of ACK/EVENT frames during unavoidable network requests.
  unoLink.setRxBufferSize(2048);
  unoLink.begin(UNO_LINK_BAUD, SERIAL_8N1, UNO_LINK_RX, UNO_LINK_TX);
  bootToken = String(static_cast<uint32_t>(ESP.getEfuseMac()), HEX) + "-" +
              String(millis(), HEX);
  dht.begin();
  pinMode(IR_PIN, INPUT);
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
  serviceMachineDisplayStatus();
  // Keep reading UART until the final ACK or the existing 5-second deadline.
  // No Wi-Fi reconnect, telemetry, or event upload may consume that window.
  if (pendingCommandId.length() > 0) { delay(1); return; }
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
    drawValues(dhtOK, detected, waterOK);
    postTelemetry(dhtOK, detected, waterOK);
  }
  serviceUnoLink();
  serviceMachineDisplayStatus();
  serviceHealthProbe();
}
