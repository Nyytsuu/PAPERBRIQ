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
unsigned long lastCommandPollAt = 0;
const unsigned long SAMPLE_INTERVAL_MS = 2500;
const unsigned long POST_INTERVAL_MS = 5000;
const unsigned long COMMAND_POLL_INTERVAL_MS = 2000;
const unsigned long UNO_ACK_TIMEOUT_MS = 5000;
const unsigned long EVENT_RETRY_INTERVAL_MS = 5000;

String unoLine;
String pendingCommandId;
String pendingCommandAction;
unsigned long pendingCommandSentAt = 0;
String lastCommandId;
String lastCommandStatus;
String lastCommandMessage;
String processCommandId;

struct QueuedEvent {
  String eventId;
  String type;
  String severity;
  String component;
  String processState;
  String message;
  String commandId;
};

const uint8_t EVENT_QUEUE_CAPACITY = 8;
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
  Serial.println("WARNING: HTTPS certificate verification is disabled");
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

  tft.drawRoundRect(12, 49, 6, 17, 3, PB_ORANGE);
  tft.fillCircle(15, 66, 5, PB_ORANGE);
  tft.drawLine(15, 52, 15, 64, PB_ORANGE);
  tft.fillTriangle(96, 48, 90, 58, 102, 58, PB_CYAN);
  tft.fillCircle(96, 58, 6, PB_CYAN);
}

void drawValues(bool dhtOK, bool detected, bool waterOK) {
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
  payload += "{\"name\":\"Temperature\",\"value\":" + String(lastTemp, 1) + ",\"unit\":\"C\",\"status\":\"" + String(dhtOK ? "Normal" : "Warning") + "\"},";
  payload += "{\"name\":\"Humidity\",\"value\":" + String(lastHumidity, 1) + ",\"unit\":\"%RH\",\"status\":\"" + String(dhtOK ? "Normal" : "Warning") + "\"},";
  payload += "{\"name\":\"IR Sensor\",\"value\":" + String(detected ? 1 : 0) + ",\"unit\":\"digital\",\"status\":\"Normal\"},";
  payload += "{\"name\":\"Rainwater Level\",\"value\":" + String(waterOK ? 100 : 0) + ",\"unit\":\"%\",\"status\":\"" + String(waterOK ? "Normal" : "Warning") + "\"}";
  payload += "],\"hardware\":{";
  payload += "\"dht22\":\"" + String(dhtOK ? "Online" : "Attention") + "\",";
  payload += "\"irSensor\":\"Online\",\"tftDisplay\":\"Online\",\"floatSwitch\":\"Online\"}}";

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
  if (WiFi.status() != WL_CONNECTED || !isSafeCommandId(id)) return false;
  const String base = apiBaseUrl();
  if (base.length() == 0) return false;
  String payload = "{\"deviceId\":\"" + jsonEscape(DEVICE_ID) + "\",";
  payload += "\"status\":\"" + jsonEscape(status) + "\",";
  payload += "\"message\":\"" + jsonEscape(message.substring(0, 240)) + "\"}";
  String response;
  const int code = httpPostJson(base + "/api/device/commands/" + id + "/ack",
                                payload, response);
  Serial.print("Command ACK ");
  Serial.print(status);
  Serial.print(": ");
  Serial.println(code);
  return code >= 200 && code < 300;
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
      (action != "start" && action != "stop" && action != "reset")) {
    postCommandAck(id, "failed", "unsupported command");
    cacheFinalCommand(id, "failed", "unsupported command");
    enqueueDeviceEvent("command_rejected", "warning", "controller", "stopped",
                       "Unsupported command", id);
    return;
  }

  pendingCommandId = id;
  pendingCommandAction = action;
  pendingCommandSentAt = millis();
  if (action == "start") processCommandId = id;
  String protocolAction = action;
  protocolAction.toUpperCase();
  unoLink.print("CMD|");
  unoLink.print(id);
  unoLink.print('|');
  unoLink.println(protocolAction);
  Serial.print("Sent command to Uno: ");
  Serial.println(protocolAction);
}

void pollNextCommand() {
  if (pendingCommandId.length() > 0 || WiFi.status() != WL_CONNECTED) return;
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
  postCommandAck(pendingCommandId, status, message);
  if (status == "completed" || status == "failed") {
    cacheFinalCommand(pendingCommandId, status, message);
    if (status == "failed" && pendingCommandAction == "start" &&
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
  if (id != pendingCommandId) {
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
  if ((eventType == "STOPPED" || eventType == "ARMED") &&
      pendingCommandId.length() > 0) {
    commandId = pendingCommandId;
  } else {
    commandId = processCommandId;
  }
  enqueueDeviceEvent(fields[0], fields[1], fields[2], fields[3], fields[4],
                     commandId);
  if (eventType == "PROCESS_COMPLETED" || eventType == "STOPPED" ||
      eventType == "FAULT") {
    processCommandId = "";
  }
}

void serviceUnoLink() {
  while (unoLink.available() > 0) {
    const char c = static_cast<char>(unoLink.read());
    if (c == '\n') {
      unoLine.trim();
      if (unoLine.startsWith("ACK|")) handleUnoAck(unoLine);
      else if (unoLine.startsWith("EVENT|")) handleUnoEvent(unoLine);
      else if (unoLine.length() > 0) {
        Serial.print("Uno: ");
        Serial.println(unoLine);
      }
      unoLine = "";
    } else if (c != '\r') {
      if (unoLine.length() < 159) unoLine += c;
      else {
        unoLine = "";
        enqueueDeviceEvent("uart_frame_overflow", "error", "controller", "fault",
                           "Uno link frame overflow");
      }
    }
  }

  if (pendingCommandId.length() > 0 &&
      millis() - pendingCommandSentAt >= UNO_ACK_TIMEOUT_MS) {
    const String failedId = pendingCommandId;
    const String failedAction = pendingCommandAction;
    postCommandAck(failedId, "failed", "Arduino Uno acknowledgement timeout");
    cacheFinalCommand(failedId, "failed", "Arduino Uno acknowledgement timeout");
    pendingCommandId = "";
    pendingCommandAction = "";
    pendingCommandSentAt = 0;
    if (failedAction == "start") processCommandId = "";
    enqueueDeviceEvent("controller_link_timeout", "critical", "controller", "fault",
                       "Arduino Uno did not acknowledge command", failedId);

    // If Uno RX works but its ACK path failed, force a latched stop anyway.
    unoLink.println("CMD|failsafe-stop|STOP");
  }
}

void setup() {
  Serial.begin(115200);
  unoLink.begin(UNO_LINK_BAUD, SERIAL_8N1, UNO_LINK_RX, UNO_LINK_TX);
  bootToken = String(static_cast<uint32_t>(ESP.getEfuseMac()), HEX) + "-" +
              String(millis(), HEX);
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
  Serial.println("ESP32/Uno command bridge ready; Uno remains stopped until RESET");
}

void loop() {
  serviceUnoLink();
  const unsigned long now = millis();
  if (WiFi.status() != WL_CONNECTED) connectWifi();

  if (now - lastCommandPollAt >= COMMAND_POLL_INTERVAL_MS) {
    lastCommandPollAt = now;
    pollNextCommand();
  }

  serviceUnoLink();
  serviceEventQueue();
  serviceUnoLink();

  if (now - lastSampleAt >= SAMPLE_INTERVAL_MS) {
    lastSampleAt = now;
    const float temperature = dht.readTemperature();
    const float humidity = dht.readHumidity();
    const bool dhtOK = !isnan(temperature) && !isnan(humidity);
    if (dhtOK) {
      lastTemp = temperature;
      lastHumidity = humidity;
      haveDhtReading = true;
    }

    const bool detected = digitalRead(IR_PIN) == IR_DETECTED;
    const bool waterOK = digitalRead(FLOAT_PIN) == LOW;
    drawValues(dhtOK, detected, waterOK);
    if (now - lastPostAt >= POST_INTERVAL_MS && haveDhtReading) {
      lastPostAt = now;
      postTelemetry(dhtOK, detected, waterOK);
    }
  }
  serviceUnoLink();
}
