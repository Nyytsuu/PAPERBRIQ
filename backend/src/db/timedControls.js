const { normalizeMachineHealth } = require('./machineChecks');
const LIMITS = Object.freeze({ heater: 10, pump: 3 });

function encodeTimedCommand(action, seconds) {
  if (!Object.hasOwn(LIMITS, action) || !Number.isInteger(seconds) || seconds < 1 || seconds > LIMITS[action]) {
    throw new RangeError('Use whole seconds: heater 1–10, pump 1–3.');
  }
  return `${action}:${seconds}`;
}

function buildTimedControls(report, control, now = Date.now()) {
  const age = now - Date.parse(report?.recordedAt);
  const fresh = Number.isFinite(age) && age >= 0 && age < 15000;
  const health = fresh ? normalizeMachineHealth(report?.health) : null;
  const supported = health?.version === 3 && health.timedSupported === true;
  const online = control.device?.connected === true;
  const busy = (control.commands || []).some(c => ['queued', 'delivered'].includes(c.status));
  let reason = '';
  if (!online) reason = 'PaperBriq is offline. Reconnect the machine to use these controls.';
  else if (!supported) reason = 'Waiting for a recent report from the updated machine boards.';
  else if (!health.unoLink || !health.controlIo || !health.indicatorIo) reason = 'Machine connection needs attention.';
  else if (!health.doorClosed) reason = 'Close the chamber door.';
  else if (!health.stopClosed || !health.emergencyClosed) reason = 'Check the physical Stop and Emergency contacts.';
  else if (health.armed !== false) reason = 'Use Stop before running the heater or pump.';
  else if (busy || health.testActive !== false || health.fanOn !== false || health.unlockOn !== false ||
      health.heaterOn !== false || health.pumpOn !== false) reason = 'Wait for the current action to finish.';
  else if (!['idle', 'stopped', 'ready', 'complete', 'completed'].includes(control.processState)) reason = 'Stop the current process first.';
  const available = !reason;
  return {
    supported: supported && online, reportAt: report?.recordedAt || null,
    validForMs: fresh ? 15000 - age : 0, reason,
    heater: { minSeconds: 1, maxSeconds: 10, intervalSeconds: 60,
      on: online && supported ? health.heaterOn : null,
      available: available && health.heaterReady === true,
      reason: reason || (health.heaterReady ? '' : 'Heater cooling down; wait for the next machine report.') },
    pump: { minSeconds: 1, maxSeconds: 3, intervalSeconds: 10,
      on: online && supported ? health.pumpOn : null,
      available: available && health.pumpReady === true,
      reason: reason || (health.pumpReady ? '' : 'Pump resting; wait for the next machine report.') }
  };
}

module.exports = { encodeTimedCommand, buildTimedControls, LIMITS };
