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
  assert.match(done.detail, /Start drying separately/);
  assert(!done.steps.some(s => s.status === 'Current'));
  const offline = buildMachineProcess({...status([event('PROCESS_STARTED', 1)]), device: {connected: false}});
  assert.equal(offline.state, 'unknown');
  assert(!offline.steps.some(s => s.status === 'Current'));
});
test('inlet waiting is a reported stage, not shredding or completed movement', () => {
  const run = buildMachineProcess(status([event('WAITING_FOR_PAPER', 2), event('PROCESS_STARTED', 1)]));
  assert.equal(run.label, 'Waiting for paper');
  assert.equal(run.steps.find(s => s.id === 'paper').status, 'Current');
  assert.equal(run.steps.find(s => s.id === 'shred').status, 'Not reported');
});
test('pending start and other device events cannot claim a new run', () => {
  const waiting = buildMachineProcess({...status([event('PROCESS_STARTED', 1)]), commands: [
    {command: 'start', status: 'queued', createdAt: event('', 4).createdAt}
  ]});
  assert.equal(waiting.hasRun, false);
  const foreign = buildMachineProcess(status([event('PROCESS_STARTED', 1, {deviceId: 'other'})]));
  assert.equal(foreign.hasRun, false);
});

test('new paper returns the current step from finish-mixing to shredding', () => {
  const run = buildMachineProcess(status([
    event('SHREDDER_MIXER_STARTED', 4, {message: 'Paper detected; shredding resumed'}),
    event('SHREDDER_COMPLETED', 3), event('SHREDDER_MIXER_STARTED', 2),
    event('PROCESS_STARTED', 1)
  ]));
  assert.equal(run.steps.find(s => s.id === 'shred').status, 'Current');
  assert.equal(run.steps.find(s => s.id === 'mix').status, 'Reported');
  assert.equal(run.steps.find(s => s.id === 'transfer').status, 'Not reported');
  assert.equal(run.state, 'running');
});

test('forming milestones advance on reports and freeze on stop', () => {
  const first = [event('PROCESS_STARTED', 1), event('SHREDDER_MIXER_STARTED', 2)];
  const mixing = [...first, event('SHREDDER_COMPLETED', 3)];
  const released = [...mixing, event('MIXER_COMPLETED', 4, {message: 'Mixing ended'}),
    event('MATERIAL_RELEASE_STARTED', 5)];
  const count = events => buildMachineProcess(status(events)).milestones.filter(item => item.completed).length;
  assert.equal(count(first), 1);
  assert.equal(count(mixing), 2);
  assert.equal(count(released), 3);
  const stopped = buildMachineProcess(status([...released, event('STOPPED', 6),
    event('FAULT', 7, {message: 'Door contact opened'}), event('EJECTION_STARTED', 8)]));
  assert.equal(stopped.milestones.filter(item => item.completed).length, 3);
  assert.equal(stopped.activity[0].text, 'Door contact opened');
  assert.match(stopped.activity[2].text, /mixer gate into the molder/);
  const done = buildMachineProcess(status([...released, event('PROCESS_COMPLETED', 6)]));
  assert(done.milestones.every(item => item.completed));
});

test('shows reported timing and keeps manual mixer pulse reports from advancing forming', () => {
  const run = buildMachineProcess(status([
    event('PROCESS_STARTED', 1), event('SHREDDER_MIXER_STARTED', 2, {message: 'Mixer starts in 15 seconds'}),
    event('MIXER_COMPLETED', 3, {message: 'Mixer OFF'})
  ]));
  assert.equal(run.steps.find(step => step.id === 'shred').status, 'Current');
  assert.equal(run.activity[1].text, 'Mixer starts in 15 seconds');
  assert.equal(run.milestones.filter(item => item.completed).length, 1);
});

test('batch buttons show separate progress and highlight the batch reported by the Uno', () => {
  const events = [
    event('PROCESS_STARTED', 1, {message: 'Batch 1'}),
    event('SHREDDER_MIXER_STARTED', 2, {message: 'Batch 1'}),
    event('PRESSING_STARTED', 3),
    event('SHREDDER_MIXER_STARTED', 4, {message: 'Batch 2'}),
    event('MIXER_COMPLETED', 5, {message: 'Mixing ended'})
  ];
  const run = buildMachineProcess(status(events));
  assert.equal(run.activeBatch, 2);
  assert.equal(run.batches[0].label, 'Batch complete');
  assert(run.batches[0].milestones.every(milestone => milestone.completed));
  assert.equal(run.batches[1].steps.find(step => step.id === 'settle').status, 'Current');
  assert.equal(run.batches[1].steps.find(step => step.id === 'press').status, 'Not reported');
  assert.deepEqual(run.batches[2].activity, []);
  const stopped = buildMachineProcess(status([...events, event('STOPPED', 6)]));
  assert.equal(stopped.activeBatch, null);
  assert.equal(stopped.latestBatch, 2);
  assert.equal(stopped.batches[1].activity[0].text, 'Forming stopped.');
  const offline = buildMachineProcess({...status(events), device: {connected: false}});
  assert.equal(offline.activeBatch, null);
  assert.equal(offline.latestBatch, 2);
});

test('direct batch starts and legacy reports preserve the batch the controller actually reported', () => {
  const skipped = buildMachineProcess(status([event('PROCESS_STARTED', 1, {message: 'Batch 3'})]));
  assert.equal(skipped.activeBatch, 3);
  assert.deepEqual(skipped.batches[0].activity, []);
  assert.deepEqual(skipped.batches[1].activity, []);
  const legacy = buildMachineProcess(status([
    event('PROCESS_STARTED', 1, {message: 'Process started'}),
    event('SHREDDER_MIXER_STARTED', 2, {message: 'Batch started'})
  ]));
  assert.equal(legacy.activeBatch, null);
  assert.equal(legacy.steps.find(step => step.id === 'shred').status, 'Current');
  const single = buildMachineProcess(status([
    event('PROCESS_STARTED', 1, {message: 'Single batch'}),
    event('EJECTION_STARTED', 2)
  ]));
  assert.equal(single.activeBatch, 1);
  assert.equal(single.batches[0].steps.find(step => step.id === 'eject').status, 'Current');
  assert.deepEqual(single.batches[1].activity, []);
});
