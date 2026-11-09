const { randomBytes } = require('crypto');
const { publicStoreError } = require('./deviceControl');
const MAX_TIMER_SECONDS = 2147483; // Transport/clock capacity, not an operating limit.
const PUMP_PRESET_SECONDS = 20;
const phases = ['idle', 'waiting_for_paper', 'running', 'finished', 'stopped', 'fault'];
function duration(value, allowUnset = false) {
  if (allowUnset && value === null) return null;
  if (!Number.isInteger(value) || value < 1 || value > MAX_TIMER_SECONDS)
    throw publicStoreError(400, 'Enter a positive duration in whole seconds.');
  return value;
}
function validateSettings(body) {
  return { dryingSeconds: duration(body.dryingSeconds, true) };
}
function normalizeProduction(p) {
  if (!p || ![1,2].includes(p.version)) return null;
  const fields = ['pumpMaxSeconds','dryingMaxSeconds','pumpVerified','dryingVerified','kind','phase','runId','totalSeconds','elapsedSeconds','remainingSeconds'];
  if (p.version === 2) fields.push('preheatMaxSeconds','targetC','temperatureFresh','thermalStage','heaterFanConfirmed');
  if (fields.some(k => !Number.isInteger(p[k]) || p[k] < 0)) return null;
  if (p.pumpMaxSeconds > MAX_TIMER_SECONDS || p.dryingMaxSeconds > MAX_TIMER_SECONDS ||
      p.pumpVerified > 1 || p.dryingVerified > 1 || p.kind > (p.version === 2 ? 3 : 2) || p.phase > 5 ||
      p.runId > 0xFFFFFFFF || p.totalSeconds > MAX_TIMER_SECONDS || p.elapsedSeconds > 0xFFFFFFFF ||
      p.remainingSeconds > p.totalSeconds ||
      ([1,2].includes(p.phase) && (!p.runId || !p.kind || !p.totalSeconds))) return null;
  if (p.version === 2 && (p.preheatMaxSeconds > 600 || p.targetC < 30 || p.targetC > 50 ||
      p.temperatureFresh > 1 || p.thermalStage > 4 || p.heaterFanConfirmed > 1)) return null;
  const sampleAgeMs = p.sampleAgeMs ?? 0;
  if (!Number.isInteger(sampleAgeMs) || sampleAgeMs < 0 || sampleAgeMs > 0xFFFFFFFF) return null;
  return Object.fromEntries([['version',p.version], ['sampleAgeMs',sampleAgeMs], ...fields.map(k => [k,p[k]])]);
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
    settings: { dryingSeconds: settings?.dryingSeconds ?? null }, reason,
    active: !!active, run: p ? { id:p.runId.toString(16).padStart(8,'0').toUpperCase(),
      output:p.kind===1?'pump':p.kind===2?'drying':p.kind===3?'preheat':null,
      state:p.version===2 && active && p.kind!==1 ? ['idle','warming','ready_to_load','drying','fault'][p.thermalStage] : phases[p.phase],
      totalSeconds:p.totalSeconds, elapsedSeconds:p.elapsedSeconds, remainingSeconds:p.remainingSeconds } : null };
  result.thermal = p?.version === 2 ? { targetC:p.targetC, temperatureFresh:p.temperatureFresh===1,
    fanConfirmed:p.heaterFanConfirmed===1, concurrentForming:false } : null;
  for (const key of ['pump','drying','preheat']) {
    const max = p?.[key+'MaxSeconds'] || 0, verified = p?.[(key==='preheat'?'drying':key)+'Verified'] === 1;
    const ownRun = active && result.run.output === key;
    const thermalReason = key === 'pump' ? '' : p?.version !== 2 ?
      'Update both boards for temperature-controlled preheating and drying.' :
      !p.heaterFanConfirmed ? 'Confirm the heater fan runs continuously before enabling heating.' :
      !p.temperatureFresh ? 'Waiting for a fresh chamber temperature.' : '';
    const outputReason = reason || thermalReason || (!verified ? 'Hardware protection needs commissioning.' :
      !max ? 'Operating time limit needs commissioning.' :
      key === 'pump' && max < PUMP_PRESET_SECONDS ? 'The pump preset exceeds the machine operating limit.' : '');
    result[key] = { maxSeconds:max, verified, canStart:!outputReason && !active && stopped,
      canAdjust:key === 'drying' && !outputReason && !!ownRun, reason:outputReason || (active && !ownRun ? 'Another output cycle is active.' : !active && !stopped ? 'Stop the machine before preparing this cycle.' : '') };
  }
  return result;
}
function encodeProductionCommand(action, body, controls) {
  const key = action === 'pump_arm' ? 'pump' : action === 'preheat_start' ? 'preheat' : 'drying';
  const edit = action.endsWith('_time');
  if (!['pump_arm','drying_start','drying_time','preheat_start'].includes(action)) throw publicStoreError(400,'Unsupported production action.');
  const seconds = key === 'pump' ? PUMP_PRESET_SECONDS : duration(body.durationSeconds);
  const output = controls[key];
  if (!output || !(edit ? output.canAdjust : output.canStart)) throw publicStoreError(409,output?.reason || 'Production control unavailable.');
  if (seconds > output.maxSeconds) throw publicStoreError(400,`Maximum ${key} duration is ${output.maxSeconds} seconds.`);
  let run;
  if (edit) {
    run = String(body.runId || '').toUpperCase();
    if (!/^[0-9A-F]{8}$/.test(run) || run !== controls.run?.id) throw publicStoreError(409,'The active run changed. Refresh before adjusting its time.');
  } else { do { run=randomBytes(4).toString('hex').toUpperCase(); } while(run==='00000000' || run===controls.run?.id); }
  const prefix = key==='pump'?'P':key==='preheat'?'H':'D';
  return `${prefix}${edit?'T':prefix}:${run}:${seconds}`;
}
module.exports = { validateSettings, normalizeProduction, buildProductionControls, encodeProductionCommand };
