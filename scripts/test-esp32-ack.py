"""Exercise the real ESP32 ACK handlers with simulated UART, HTTP and time."""
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parents[1]
source = (root / "firmware/PaperBriqMonitor/PaperBriqMonitor.ino").read_text()
timed_header = (root / "firmware/PaperBriqMonitor/TimedCommands.h").read_text()
assert timed_header == (root / "firmware/ArduinoUnoMotorTest/TimedCommands.h").read_text()
production_header = (root / "firmware/PaperBriqMonitor/ProductionTimer.h").read_text()
paper_input_header = (root / "firmware/PaperBriqMonitor/PaperInput.h").read_text()
assert production_header == (root / "firmware/PaperBriqController/ProductionTimer.h").read_text()
assert production_header == (root / "firmware/ArduinoUnoMotorTest/ProductionTimer.h").read_text()
names = ["sendPaperFrame", "paperSensorTask", "cacheFinalCommand", "postCommandAck", "serviceAckReport",
         "dispatchCommand", "finishPendingCommand", "handleUnoAck",
         "handleUnoEvent", "handleUnoHealth", "handleProductionReport", "serviceUnoLink", "loop"]
functions = []
for name in names:
    # Top-level function closing braces are unindented in the actual sketch.
    start = source.index(f" {name}(")
    start = source.rfind("\n", 0, start) + 1
    end = source.index("\n}", start) + 2
    functions.append(source[start:end])

stub = r'''
#include <cassert>
#include <cstdint>
#include <deque>
#include <string>
#include <algorithm>
#include <sstream>
#include <iostream>
struct String : std::string {
  using std::string::string;
  using std::string::operator=;
  String(const std::string &s) : std::string(s) {}
  String(uint32_t n) : std::string(std::to_string(n)) {}
  String substring(size_t a, size_t b = std::string::npos) const {
    return substr(a, b == std::string::npos ? b : b-a);
  }
  int indexOf(char c, size_t a=0) const { auto n=find(c,a); return n==npos ? -1 : int(n); }
  void toLowerCase() { for(char &c:*this) c=char(tolower((unsigned char)c)); }
  void toUpperCase() { for(char &c:*this) c=char(toupper((unsigned char)c)); }
  void replace(char a,char b) { std::replace(begin(),end(),a,b); }
  bool startsWith(const char *p) const { return rfind(p,0)==0; }
  void trim() {
    auto a=find_first_not_of(" \r\n\t");
    if(a==npos) clear(); else *this=substr(a,find_last_not_of(" \r\n\t")-a+1);
  }
};
struct Port {
  String output; std::deque<char> input;
  template<class T> void print(const T &v) { std::ostringstream s; s<<v; output+=s.str(); }
  void print(const String &v) { output+=v; }
  template<class T> void println(const T &v) { print(v); output+='\n'; }
  int available() { return int(input.size()); }
  int read() { char c=input.front(); input.pop_front(); return c; }
} Serial, unoLink;
void sendUnoFrame(const String &frame) { unoLink.println(frame); }
uint32_t clockMs=10000;
unsigned long millis() { return clockMs; }
void delay(unsigned long n) { clockMs+=n; }
const int WL_CONNECTED=1;
struct Wifi { int status() { return WL_CONNECTED; } } WiFi;
const char *DEVICE_ID="test-device";
String unoLine,pendingCommandId,pendingCommandAction,lastCommandId,lastCommandStatus;
String lastCommandMessage,processCommandId,ackReportId,ackReportStatus,ackReportMessage;
unsigned long pendingCommandSentAt=0,lastAckReportAttemptAt=0,lastCommandPollAt=0,lastPostAt=0;
const unsigned long UNO_ACK_TIMEOUT_MS=5000,COMMAND_POLL_INTERVAL_MS=2000,POST_INTERVAL_MS=5000;
bool haveDhtReading=false; unsigned long lastSampleAt=0;
const int IR_PIN=25,FLOAT_PIN=32,IR_DETECTED=0,LOW=0;
enum { DISPLAY_FAULT,DISPLAY_GETTING_READY,DISPLAY_STOPPED,DISPLAY_READY,DISPLAY_COMPLETE,DISPLAY_RUNNING };
int displayState=-1,httpCalls=0,otherNetworkCalls=0,timeoutEvents=0;
String httpPayload;
void setMachineDisplayState(int n) { displayState=n; }
bool isSafeCommandId(const String &id) { return !id.empty(); }
String apiBaseUrl() { return "https://test.invalid"; }
String jsonEscape(const String &s) { return s; }
int httpPostJson(const String &,const String &body,String &) {
  ++httpCalls; httpPayload=body; clockMs+=6000; return 200;
}
void enqueueDeviceEvent(const String &type,const String &,const String &,const String &,
                        const String &,const String & = "") {
  if(type=="controller_link_timeout") ++timeoutEvents;
}
String healthProbeId;
unsigned long lastHealthProbeAt=0,healthReplyAt=0;
const unsigned long HEALTH_REPLY_TIMEOUT_MS=5000;
bool haveHealthReply=false,healthControlIo=false,healthStopClosed=false,healthEmergencyClosed=false;
bool healthIndicatorIo=false,healthArmed=false,healthChamberSupported=false,healthDoorClosed=false;
bool healthFanOn=false,healthUnlockOn=false,healthTestActive=false,healthTimedVersion=false;
bool healthTimedSupported=false,healthHeaterOn=false,healthPumpOn=false,healthHeaterReady=false,healthPumpReady=false;
String productionJson; unsigned long productionReplyAt=0;
int unoTxMutex=1;
const int portMAX_DELAY=0, pdTRUE=1, SHREDDER_IN_PIN=26, SHREDDER_OUT_PIN=19;
int xSemaphoreTake(int,int) { return pdTRUE; }
void xSemaphoreGive(int) {}
#define pdMS_TO_TICKS(n) (n)
bool inletDetected=false,outletDetected=false,paperInputsReady=false;
uint32_t paperInputsAt=0,taskEnd=0,taskStart=0;
void vTaskDelay(int ms) { clockMs+=ms; if(clockMs>=taskEnd) throw 1; }
int paperSnapshotMux=0;
void portENTER_CRITICAL(int *) {}
void portEXIT_CRITICAL(int *) {}
void servicePaperSignal() {}
String pendingDhtRequest, lastDhtReply;
unsigned long lastUnoByteAt=0, dhtRequestAt=0;
void replyDhtCheck(const String &request) { lastDhtReply=request; }
void sampleChamberSensor() {}
void serviceSensorSerial() {}
void serviceMachineDisplayStatus() {}
void connectWifi() { ++otherNetworkCalls; }
void pollNextCommand() { ++otherNetworkCalls; }
void serviceEventQueue() { ++otherNetworkCalls; }
void serviceHealthProbe() { ++otherNetworkCalls; }
int digitalRead(int pin) { return pin==SHREDDER_IN_PIN && clockMs-taskStart<400 ? 1 : 0; }
void drawValues(bool,bool,bool) {}
void postTelemetry(bool,bool,bool) { ++otherNetworkCalls; }
'''

tests = r'''
void inject(const char *s) { while(*s) unoLink.input.push_back(*s++); }
void resetTest() {
  pendingCommandId=""; pendingCommandAction=""; unoLine="";
  ackReportId=""; ackReportStatus=""; ackReportMessage="";
  lastCommandId=""; lastCommandStatus=""; lastCommandMessage="";
  processCommandId=""; pendingCommandSentAt=0; lastAckReportAttemptAt=0;
  Serial.output.clear(); unoLink.output.clear(); unoLink.input.clear();
  httpCalls=otherNetworkCalls=timeoutEvents=0; clockMs=10000;
  pendingDhtRequest=""; lastDhtReply="";
}
int main() {
  resetTest();
  inject("DHT_CHECK|137\n"); serviceUnoLink();
  clockMs+=80;
  inject("EVENT|STOPPED|warning|controller|stopped|Stopped\n"); serviceUnoLink();
  clockMs+=99; serviceUnoLink(); assert(lastDhtReply.empty());
  ++clockMs; serviceUnoLink(); assert(lastDhtReply=="DHT_CHECK|137");
  resetTest();
  inject("DHT_CHECK|138\n"); serviceUnoLink();
  clockMs+=10000; serviceUnoLink(); assert(lastDhtReply.empty() && pendingDhtRequest.empty());
  resetTest(); healthProbeId="prod-test"; lastHealthProbeAt=clockMs;
  // Background heartbeat starts without a health report or WAIT event.
  taskStart=clockMs; taskEnd=clockMs+1200;
  try { paperSensorTask(nullptr); } catch(int) {}
  assert(paperInputsReady && inletDetected);
  assert(unoLink.output.find("IR2|0|1\n")!=std::string::npos);
  assert(unoLink.output.find("IR2|1|1\n")!=std::string::npos);
  assert(unoLink.output.find("CMD|")==std::string::npos);
  std::cout<<"Background IR: clear/detected and heartbeat available without health/event processing.\n";
  resetTest(); healthProbeId="prod-test"; lastHealthProbeAt=clockMs;
  handleProductionReport("PROD|prod-test|120|3600|1|1|1|2|305441741|60|20|40");
  assert(productionJson.find("\"remainingSeconds\":40")!=std::string::npos);
  String validProduction = productionJson;
  handleProductionReport("PROD|wrong|120|3600|1|1|1|2|305441741|60|20|40");
  handleProductionReport("PROD|prod-test|120|3600|1|1|1|2|305441741|60|20|999");
  assert(productionJson==validProduction);
  handleProductionReport("PRO2|prod-test|120|600|1|1|3|2|305441741|600|20|580|600|45|1|2|1");
  assert(productionJson.find("\"version\":2")!=std::string::npos);
  assert(productionJson.find("\"thermalStage\":2")!=std::string::npos);
  validProduction=productionJson;
  handleProductionReport("PRO2|prod-test|120|600|1|1|3|2|305441741|600|20|580|600|99|1|2|1");
  assert(productionJson==validProduction);
  resetTest(); dispatchCommand("preheat-start","HH:1234ABCD:600");
  assert(unoLink.output=="CMD|preheat-start|HH:1234ABCD:600\n");
  inject("ACK|preheat-start|COMPLETED|production_applied\n"); serviceUnoLink();
  assert(lastCommandStatus=="completed");
  resetTest(); dispatchCommand("prod-arm","PP:1234ABCD:120");
  assert(unoLink.output=="CMD|prod-arm|PP:1234ABCD:120\n");
  inject("ACK|prod-arm|COMPLETED|production_applied\n"); serviceUnoLink();
  assert(lastCommandStatus=="completed");
  resetTest(); healthProbeId="health-test"; lastHealthProbeAt=clockMs;
  handleUnoHealth("HEALTH|health-test|1|1|1|1|0|1|0|0|0|1|0|0|1|1");
  assert(haveHealthReply && healthTimedVersion && healthTimedSupported);
  assert(healthHeaterReady && healthPumpReady && !healthHeaterOn && !healthPumpOn);
  healthProbeId="health-test";
  handleUnoHealth("HEALTH|health-test|1|1|1|1|0|1|0|0|0|1|1|0|0|0");
  assert(healthHeaterOn && !healthHeaterReady && !healthPumpReady);
  healthProbeId="health-test";
  handleUnoHealth("HEALTH|health-test|1|1|1|1|0|1|0|0|0|1|x|0|1|1");
  assert(healthProbeId=="health-test" && healthHeaterOn); // Malformed frame ignored.
  handleUnoHealth("HEALTH|wrong-id|1|1|1|1|0");
  assert(healthTimedVersion && healthProbeId=="health-test");
  handleUnoHealth("HEALTH|health-test|1|1|1|1|0|1|0|0|0");
  assert(healthChamberSupported && !healthTimedVersion && !healthTimedSupported);
  assert(!healthHeaterOn && !healthHeaterReady && !healthPumpReady);
  for (const char *action : {"heater:1", "heater:10", "pump:1", "pump:3"}) {
    resetTest();
    dispatchCommand("timed", action);
    String upper(action); upper.toUpperCase();
    assert(unoLink.output == String("CMD|timed|") + upper + "\n");
    inject("ACK|timed|COMPLETED|timed_run_started\n"); serviceUnoLink();
    assert(lastCommandStatus == "completed" && pendingCommandId.empty());
  }
  for (const char *action : {"heater:0", "heater:11", "pump:4", "pump:-1", "heater:1.5", "pump:01"}) {
    resetTest(); dispatchCommand("invalid", action);
    assert(unoLink.output.empty() && pendingCommandId.empty());
  }
  resetTest();
  dispatchCommand("r1","reset");
  assert(unoLink.output=="CMD|r1|RESET\n");
  inject("ACK|r1|DELIVERED|received\nEVENT|ARMED|info|controller|stopped|Controller armed\nACK|r1|COMPLETED|armed\n");
  serviceUnoLink();
  assert(httpCalls==0 && pendingCommandId.empty());
  assert(ackReportStatus=="completed" && lastCommandStatus=="completed");
  serviceAckReport();
  assert(httpCalls==1 && httpPayload.find("completed")!=std::string::npos);
  serviceUnoLink();
  assert(timeoutEvents==0 && unoLink.output.find("failsafe-stop")==std::string::npos);

  resetTest();
  dispatchCommand("r2","reset");
  inject("ACK|r2|DELIVERED|received\n"); serviceUnoLink(); serviceAckReport();
  assert(httpCalls==0 && pendingCommandId=="r2");
  loop();
  assert(httpCalls==0 && otherNetworkCalls==0);
  clockMs=14999; serviceUnoLink(); assert(pendingCommandId=="r2");
  clockMs=15000; serviceUnoLink();
  assert(pendingCommandId.empty() && ackReportStatus=="failed" && httpCalls==0);
  assert(ackReportMessage.find("final acknowledgement")!=std::string::npos);
  assert(timeoutEvents==1 && unoLink.output.find("CMD|failsafe-stop|STOP\n")!=std::string::npos);

  resetTest();
  dispatchCommand("r3","reset");
  inject("ACK|wrong-id|COMPLETED|armed\n"); serviceUnoLink();
  assert(pendingCommandId=="r3" && ackReportId.empty());
  clockMs=15000; serviceUnoLink();
  assert(ackReportMessage.find("No matching Uno")!=std::string::npos);
  assert(timeoutEvents==1 && lastCommandStatus=="failed");
  inject("ACK|r3|COMPLETED|armed\n"); serviceUnoLink();
  assert(ackReportStatus=="failed"); // A late reply cannot erase a timeout.

  resetTest();
  dispatchCommand("r4","reset");
  inject("ACK|r4|DELIVERED|received\nACK|r4|FAILED|door_open\n"); serviceUnoLink();
  assert(lastCommandStatus=="failed" && lastCommandMessage=="door open");
  assert(timeoutEvents==0 && httpCalls==0);
  serviceAckReport(); assert(httpCalls==1);

  resetTest();
  dispatchCommand("r5","reset");
  inject("ACK|r5|COMPL"); serviceUnoLink(); assert(pendingCommandId=="r5");
  inject("ETED|armed\r\n"); serviceUnoLink(); assert(lastCommandStatus=="completed");
  std::cout<<"ESP32 ACK checks passed: burst, slow HTTP, pending-network pause, delivered-only/no-reply timeout, wrong/late ID, rejection, fragmented reply.\n";
}
'''

build = root / "frontend/android/app/build/qa/esp32-ack-native"
build.mkdir(parents=True, exist_ok=True)
cpp = build / "ack-test.cpp"
cpp.write_text((timed_header + production_header + paper_input_header).replace("#pragma once", "") + "\n" + stub + "\n" + "\n\n".join(functions) + "\n" + tests)
compiler = shutil.which("g++")
if not compiler:
    raise SystemExit("g++ is required for this hardware-free test")
exe = build / "ack-test.exe"
subprocess.run([compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(exe)], check=True)
subprocess.run([str(exe)], check=True)
