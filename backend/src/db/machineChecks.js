const MAX_REPORT_AGE_MS = 30000;
const { normalizeProduction } = require('./productionControls');
const HEALTH_FIELDS = ["unoLink", "controlIo", "stopClosed", "emergencyClosed", "indicatorIo", "armed", "dht"];

function normalizeMachineHealth(value) {
  if (!value || ![1, 2, 3].includes(value.version)) return null;
  const health = { version: value.version };
  health.production = normalizeProduction(value.production);
  for (const key of HEALTH_FIELDS) health[key] = typeof value[key] === "boolean" ? value[key] : null;
  if (value.version >= 2) {
    for (const key of ["doorClosed", "fanOn", "unlockOn", "testActive"]) health[key] = typeof value[key] === "boolean" ? value[key] : null;
  }
  if (value.version === 3) {
    for (const key of ["timedSupported", "heaterOn", "pumpOn", "heaterReady", "pumpReady"]) health[key] = typeof value[key] === "boolean" ? value[key] : null;
  }
  return health;
}

// Read-only evidence: never issue a reset, start, stop, or actuator test here.
function buildMachineChecks(report, control, now = Date.now()) {
  const age = report ? now - Date.parse(report.recordedAt) : NaN;
  const fresh = Number.isFinite(age) && age >= 0 && age <= MAX_REPORT_AGE_MS;
  const health = fresh ? normalizeMachineHealth(report.health) : null;
  const online = control.device?.connected === true;
  const linked = online && health?.unoLink === true;
  const items = [];
  const add = (id, label, value, pass, fail, unknown) => items.push({
    id, label, status: value === true ? "passed" : value === false ? "attention" : "unknown",
    detail: value === true ? pass : value === false ? fail : unknown
  });
  add("connection", "PaperBriq connection", online, "PaperBriq is connected.", "PaperBriq is offline. Check its power and Wi-Fi.");
  add("report", "Fresh machine report", health ? true : null, "Recent checks received from the machine.", "",
    report && !fresh ? "Report expired. Wait for PaperBriq to reconnect, then check again." : "No supported report yet. Update both machine boards and wait for a report.");
  add("unoLink", "Machine communication", online && health ? health.unoLink : null,
    "Both directions of the machine connection responded.", "The machine controller did not reply. Check the link between the boards.", "Waiting for a fresh machine report.");
  add("dht", "Temperature and humidity", online && health ? health.dht : null,
    "The sensor returned a valid reading.", "The sensor is not returning valid readings. Check its connection.", "Waiting for a fresh sensor check.");
  const controllerCheck = (id, label, pass, fail) => add(id, label, linked ? health[id] : null, pass, fail, "Cannot check until machine communication responds.");
  controllerCheck("controlIo", "Button connections", "The button board responded.", "The button board is not responding.");
  for (const [id, label] of [["stopClosed", "Stop contact"], ["emergencyClosed", "Emergency stop contact"]]) {
    add(id, label, linked && health.controlIo === true ? health[id] : null,
      "The contact is closed. Physical operation still needs inspection.",
      "The contact is open. Check the button and its wiring beside the machine.", "Cannot read the contact until the button board responds.");
  }
  controllerCheck("indicatorIo", "Indicator connections", "The indicator board responded. Check the actual lights and buzzer yourself.", "The indicator board is not responding.");
  controllerCheck("armed", "Ready to start", "The software stop has been reset.", "Inspect the machine, then use Reset and check again.");
  if (health?.version >= 2) {
    add("doorClosed", "Door contact", linked && health.indicatorIo === true ? health.doorClosed : null,
      "Door contact is closed; this does not prove lock engagement.", "Close the door and check its interlock.", "Door input unavailable.");
    add("testIdle", "Component tests idle", linked && health.testActive !== null ? !health.testActive : null,
      "No component test is running.", "A component test is running.", "Test status unavailable.");
    add("chamberIdle", "Fan and unlock outputs idle", linked && health.fanOn !== null && health.unlockOn !== null
      ? !health.fanOn && !health.unlockOn : null, "Chamber outputs are idle.", "Wait for chamber outputs to stop.", "Chamber output status unavailable.");
  }
  const busy = (control.commands || []).some(c => c.status === "queued" || c.status === "delivered");
  const stopped = ["idle", "ready", "stopped", "complete", "completed"].includes(String(control.processState).toLowerCase());
  add("stopped", "Machine stopped", stopped && !busy, "No process or command is active.", "Wait for the machine to stop and pending commands to finish.");
  return {
    passed: items.every(item => item.status === "passed"),
    checkedAt: new Date(now).toISOString(),
    reportAt: report?.recordedAt || null,
    validForMs: fresh ? Math.max(0, MAX_REPORT_AGE_MS - age) : 0,
    componentTestsSupported: linked && health?.version >= 2,
    chamber: linked && health?.version >= 2 && health.indicatorIo === true ? {
      doorClosed: health.doorClosed, fanOn: health.fanOn, unlockOn: health.unlockOn, testActive: health.testActive
    } : null,
    items
  };
}

module.exports = { normalizeMachineHealth, buildMachineChecks, MAX_REPORT_AGE_MS };
