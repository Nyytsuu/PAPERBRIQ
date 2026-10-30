const { randomBytes } = require('crypto');
const { publicStoreError } = require('./deviceControl');
const MAX_TIMER_SECONDS = 2147483; // Transport/clock capacity, not an operating limit.
const phases = ['idle', 'waiting_for_paper', 'running', 'finished', 'stopped', 'fault'];
function duration(value, allowUnset = false) {
  if (allowUnset && value === null) return null;
  if (!Number.isInteger(value) || value < 1 || value > MAX_TIMER_SECONDS)
    throw publicStoreError(400, 'Enter a positive duration in whole seconds.');
  return value;
}
function validateSettings(body) {
  return { pumpSeconds: duration(body.pumpSeconds, true), dryingSeconds: duration(body.dryingSeconds, true) };
}
function normalizeProduction(p) {
  if (!p || p.version !== 1) return null;
  const fields = ['pumpMaxSeconds','dryingMaxSeconds','pumpVerified','dryingVerified','kind','phase','runId','totalSeconds','elapsedSeconds','remainingSeconds'];
  if (fields.some(k => !Number.isInteger(p[k]) || p[k] < 0)) return null;
  if (p.pumpMaxSeconds > MAX_TIMER_SECONDS || p.dryingMaxSeconds > MAX_TIMER_SECONDS ||
      p.pumpVerified > 1 || p.dryingVerified > 1 || p.kind > 2 || p.phase > 5 ||
      p.runId > 0xFFFFFFFF || p.totalSeconds > MAX_TIMER_SECONDS || p.elapsedSeconds > 0xFFFFFFFF ||
      p.remainingSeconds > p.totalSeconds ||
      ([1,2].includes(p.phase) && (!p.runId || !p.kind || !p.totalSeconds))) return null;
  const sampleAgeMs = p.sampleAgeMs ?? 0;
  if (!Number.isInteger(sampleAgeMs) || sampleAgeMs < 0 || sampleAgeMs > 0xFFFFFFFF) return null;
  return Object.fromEntries([['version',1], ['sampleAgeMs',sampleAgeMs], ...fields.map(k => [k,p[k]])]);
}
function buildProductionControls(report, control, settings, now = Date.now()) {
  const age = now - Date.parse(report?.recordedAt);
  const fresh = Number.isFinite(age) && age >= 0 && age < 15000 && control.device?.connected === true;
  const sample = normalizeProduction(report?.health?.production);
  const p = fresh && report?.health?.unoLink === true && sample && age + sample.sampleAgeMs < 15000 ? sample : null;
  const health = report?.health;
  const busy = (control.commands || []).some(c => ['queued','delivered'].includes(c.status));
  const safe = p && ['controlIo','indicatorIo','doorClosed','stopClosed','emergencyClosed'].every(k => health[k] === true)
    && health.testActive === false && health.unlockOn === false && health.fanOn === false
    && health.heaterOn === false && health.pumpOn === false;
  const active = p && [1,2].includes(p.phase);
  const stopped = health?.armed === false && ['idle','stopped','ready','completed','complete'].includes(control.processState);
  let reason = !p ? 'Waiting for a fresh production-controller report.' : !safe ? 'Check the door, contacts and machine connections.' : busy ? 'Waiting for the current command acknowledgement.' : '';
  const result = { supported: !!p, fresh: !!p, reportAt: report?.recordedAt || null,
    settings: settings || {pumpSeconds:null,dryingSeconds:null}, reason,
    active: !!active, run: p ? { id:p.runId.toString(16).padStart(8,'0').toUpperCase(),
      output:p.kind===1?'pump':p.kind===2?'drying':null, state:phases[p.phase],
      totalSeconds:p.totalSeconds, elapsedSeconds:p.elapsedSeconds, remainingSeconds:p.remainingSeconds } : null };
  for (const key of ['pump','drying']) {
    const max = p?.[key+'MaxSeconds'] || 0, verified = p?.[key+'Verified'] === 1;
    const ownRun = active && result.run.output === key;
    const outputReason = reason || (!verified ? 'Hardware protection needs commissioning.' : !max ? 'Operating time limit needs commissioning.' : '');
    result[key] = { maxSeconds:max, verified, canStart:!outputReason && !active && stopped,
      canAdjust:!outputReason && !!ownRun, reason:outputReason || (active && !ownRun ? 'Another output cycle is active.' : !active && !stopped ? 'Stop the machine before preparing this cycle.' : '') };
  }
  return result;
}
function encodeProductionCommand(action, body, controls) {
  const key = action.startsWith('pump_') ? 'pump' : 'drying';
  const edit = action.endsWith('_time');
  if (!['pump_arm','drying_start','pump_time','drying_time'].includes(action)) throw publicStoreError(400,'Unsupported production action.');
  const seconds = duration(body.durationSeconds);
  const output = controls[key];
  if (!output || !(edit ? output.canAdjust : output.canStart)) throw publicStoreError(409,output?.reason || 'Production control unavailable.');
  if (seconds > output.maxSeconds) throw publicStoreError(400,`Maximum ${key} duration is ${output.maxSeconds} seconds.`);
  let run;
  if (edit) {
    run = String(body.runId || '').toUpperCase();
    if (!/^[0-9A-F]{8}$/.test(run) || run !== controls.run?.id) throw publicStoreError(409,'The active run changed. Refresh before adjusting its time.');
  } else { do { run=randomBytes(4).toString('hex').toUpperCase(); } while(run==='00000000' || run===controls.run?.id); }
  return `${key==='pump'?'P':'D'}${edit?'T':key==='pump'?'P':'D'}:${run}:${seconds}`;
}
module.exports = { validateSettings, normalizeProduction, buildProductionControls, encodeProductionCommand };
