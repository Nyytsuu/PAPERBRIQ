const { test } = require('node:test');
const assert = require('node:assert/strict');
const { buildComponentTests } = require('../backend/src/db/componentTests');
const event = (type, second, extra = {}) => ({id: `${type}-${second}`, type,
  createdAt: new Date(1700000000000 + second * 1000).toISOString(), ...extra});
const status = events => ({deviceId: 'test', device: {connected: true}, events, commands: []});

test('only actual completion reports tick a component', () => {
  const result = buildComponentTests(status([event('test_item_done', 3, {component:'shredder'}),
    event('test_item_started', 2, {component:'shredder'}), event('test_started', 1)]));
  assert.equal(result.completedCount, 1);
  assert(result.items[0].checked);
  assert(result.items.every(i => i.physicalVerified === false));
  assert(!result.items[1].checked);
});
test('aborted, restarted, rebooted and offline tests preserve uncertainty', () => {
  const events = [event('test_item_done', 4, {component:'mixer'}), event('test_aborted', 3),
    event('test_item_started', 2, {component:'mixer'}), event('test_started', 1)];
  const stopped = buildComponentTests(status(events));
  assert.equal(stopped.state, 'interrupted');
  assert.equal(stopped.completedCount, 0);
  assert.equal(stopped.items[1].status, 'interrupted');
  assert.equal(buildComponentTests(status([event('controller_ready', 5), ...events])).completedCount, 0);
  assert.equal(buildComponentTests(status([event('test_started', 5), ...events])).completedCount, 0);
  const offline = buildComponentTests({...status(events.slice(2)), device: {connected: false}});
  assert.equal(offline.state, 'unknown');
  assert.equal(offline.items[1].status, 'unknown');
});
test('overall completion cannot invent missing individual reports', () => {
  const result = buildComponentTests(status([event('test_completed', 4), event('test_started', 1)]));
  assert.equal(result.state, 'missing_reports');
  assert.equal(result.completedCount, 0);
});
test('new command, other devices and other run IDs cannot reuse ticks', () => {
  const result = buildComponentTests({...status([event('test_started', 1, {commandId:'old'})]),
    commands:[{command:'self_test', id:'new', status:'queued', createdAt:event('',2).createdAt}]});
  assert.equal(result.state, 'queued');
  const foreign = buildComponentTests(status([event('test_item_done', 3, {component:'shredder', commandId:'other'}),
    event('test_started', 1, {commandId:'current'})]));
  assert.equal(foreign.completedCount, 0);
  assert.equal(buildComponentTests(status([event('test_started',1,{deviceId:'other'})])).state, 'waiting');
});
