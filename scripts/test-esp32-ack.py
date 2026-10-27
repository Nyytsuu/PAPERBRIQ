"""Exercise the real ESP32 ACK handlers with simulated UART, HTTP and time."""
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parents[1]
source = (root / "firmware/PaperBriqMonitor/PaperBriqMonitor.ino").read_text()
names = ["cacheFinalCommand", "postCommandAck", "serviceAckReport",
         "dispatchCommand", "finishPendingCommand", "handleUnoAck",
         "serviceUnoLink", "loop"]
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
enum { DISPLAY_FAULT,DISPLAY_GETTING_READY,DISPLAY_STOPPED };
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
void handleUnoEvent(const String &) {}
void handleUnoHealth(const String &) {}
void replyDhtCheck(const String &) {}
void sampleChamberSensor() {}
void serviceSensorSerial() {}
void serviceMachineDisplayStatus() {}
void connectWifi() { ++otherNetworkCalls; }
void pollNextCommand() { ++otherNetworkCalls; }
void serviceEventQueue() { ++otherNetworkCalls; }
void serviceHealthProbe() { ++otherNetworkCalls; }
int digitalRead(int) { return 0; }
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
}
int main() {
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
cpp.write_text(stub + "\n" + "\n\n".join(functions) + "\n" + tests)
compiler = shutil.which("g++")
if not compiler:
    raise SystemExit("g++ is required for this hardware-free test")
exe = build / "ack-test.exe"
subprocess.run([compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(exe)], check=True)
subprocess.run([str(exe)], check=True)
