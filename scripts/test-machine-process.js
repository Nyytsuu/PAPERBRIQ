const { test } = require('node:test');
const assert = require('node:assert/strict');
const { buildMachineProcess } = require('../backend/src/db/machineProcess');
const event = (type, second, extra = {}) => ({type, createdAt: new Date(1700000000000 + second * 1000).toISOString(), ...extra});
const status = events => ({deviceId: 'test', device: {connected: true}, processState: 'running', events, commands: []});

test('waiting and missing evidence never imply completion', () => {
  assert.equal(buildMachineProcess(status([event('process_started', 1)])).hasRun, true);
  const empty = buildMachineProcess(status([]));
  assert.equal(empty.hasRun, false);
  assert(empty.steps.every(s => s.status === 'Not reported'));
  const run = buildMachineProcess(status([event('PRESSING_STARTED', 3), event('PROCESS_STARTED', 1)]));
  assert.equal(run.steps.find(s => s.id === 'press').status, 'Current');
  assert.equal(run.steps.find(s => s.id === 'transfer').status, 'Not reported');
  assert(!JSON.stringify(run).includes('100%'));
});
test('stop, restart and reboot do not reuse a previous run', () => {
  const stopped = buildMachineProcess(status([event('PRESSING_STARTED', 4), event('STOPPED', 3), event('PROCESS_STARTED', 1)]));
  assert.equal(stopped.state, 'interrupted');
  assert.equal(stopped.steps.find(s => s.id === 'press').status, 'Not reported');
  const reset = buildMachineProcess(status([event('CONTROLLER_READY', 3), event('PROCESS_STARTED', 1)]));
  assert.equal(reset.hasRun, false);
  const restarted = buildMachineProcess(status([event('PROCESS_STARTED', 4), event('PROCESS_COMPLETED', 3), event('PROCESS_STARTED', 1)]));
  assert.equal(restarted.state, 'running');
  assert.equal(restarted.steps.find(s => s.id === 'close').status, 'Not reported');
});
test('completion means forming only and offline never shows a current step', () => {
  const done = buildMachineProcess(status([event('PROCESS_COMPLETED', 3), event('PROCESS_STARTED', 1)]));
  assert.equal(done.state, 'completed');
  assert.match(done.detail, /Drying is not enabled/);
  assert(!done.steps.some(s => s.status === 'Current'));
  const offline = buildMachineProcess({...status([event('PROCESS_STARTED', 1)]), device: {connected: false}});
  assert.equal(offline.state, 'unknown');
  assert(!offline.steps.some(s => s.status === 'Current'));
});
test('pending start and other device events cannot claim a new run', () => {
  const waiting = buildMachineProcess({...status([event('PROCESS_STARTED', 1)]), commands: [
    {command: 'start', status: 'queued', createdAt: event('', 4).createdAt}
  ]});
  assert.equal(waiting.hasRun, false);
  const foreign = buildMachineProcess(status([event('PROCESS_STARTED', 1, {deviceId: 'other'})]));
  assert.equal(foreign.hasRun, false);
});
