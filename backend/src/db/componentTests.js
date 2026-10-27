const COMPONENTS = [
  ['shredder', 'Shredder'], ['mixer', 'Mixer'], ['mixer_gate', 'Mixer gate servo'],
  ['molder_gate', 'Molder servo'], ['presser', 'Presser'], ['pusher', 'Pusher'],
  ['fan', 'Exhaust fan'], ['indicators', 'Lights and buzzer'], ['lock', 'Door lock']
];
const timestamp = value => Date.parse(value) || 0;

function buildComponentTests(control) {
  const events = (control.events || []).filter(e => !e.deviceId || e.deviceId === control.deviceId)
    .map(e => ({...e, type: String(e.type).toUpperCase()}))
    .sort((a, b) => timestamp(b.createdAt) - timestamp(a.createdAt));
  const startIndex = events.findIndex(e => e.type === 'TEST_STARTED');
  const start = events[startIndex];
  const reboot = events.findIndex(e => e.type === 'CONTROLLER_READY');
  const newerCommand = (control.commands || []).find(c => c.command === 'self_test' &&
    c.id !== start?.commandId && ['queued', 'delivered', 'completed'].includes(c.status) &&
    timestamp(c.createdAt) >= timestamp(start?.createdAt));
  const hasRun = Boolean(start && !newerCommand && !(reboot >= 0 && reboot < startIndex));
  let run = hasRun ? events.slice(0, startIndex + 1).filter(e =>
    !start.commandId || !e.commandId || e.commandId === start.commandId || ['STOPPED', 'FAULT'].includes(e.type)) : [];
  const terminalIndex = run.findLastIndex(e => ['TEST_COMPLETED', 'TEST_ABORTED', 'STOPPED', 'FAULT'].includes(e.type));
  const terminal = run[terminalIndex];
  if (terminal) run = run.slice(terminalIndex);
  const items = COMPONENTS.map(([id, label]) => {
    const done = run.find(e => e.type === 'TEST_ITEM_DONE' && e.component === id);
    const started = run.find(e => e.type === 'TEST_ITEM_STARTED' && e.component === id);
    return {id, label, status: done ? 'completed' : started ? terminal ? 'interrupted' : control.device?.connected === true ? 'testing' : 'unknown' : 'waiting',
      checked: Boolean(done), physicalVerified: false,
      detail: done ? 'Timed run completed. Confirm the part moved correctly.' : started ? 'Controller started this test.' : 'Not run yet.'};
  });
  const online = control.device?.connected === true;
  let state = !hasRun ? newerCommand ? 'queued' : 'waiting' : !terminal ? 'running' :
    terminal.type === 'TEST_COMPLETED' ? items.every(i => i.checked) ? 'completed' : 'missing_reports' : 'interrupted';
  if (!online && state === 'running') state = 'unknown';
  return {state, runId: hasRun ? start.id : null, startedAt: hasRun ? start.createdAt : null,
    online, items, completedCount: items.filter(i => i.checked).length,
    detail: 'Ticks confirm timed commands finished. They do not verify rotation, position, lock engagement, or freedom from jams.'};
}
module.exports = { buildComponentTests };
