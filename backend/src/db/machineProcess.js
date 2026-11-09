// Controller events describe commanded steps, not proof of physical movement.
const STAGES = [
  { id: "prepare", name: "Prepare gates", event: "PROCESS_STARTED" },
  { id: "paper", name: "Waiting for paper", event: "WAITING_FOR_PAPER" },
  { id: "shred", name: "Shred and mix", event: "SHREDDER_MIXER_STARTED" },
  { id: "mix", name: "Finish mixing", event: "SHREDDER_COMPLETED" },
  { id: "transfer", name: "Release into molder", event: "MATERIAL_RELEASE_STARTED" },
  { id: "press", name: "Press briquette", event: "PRESSING_STARTED" },
  { id: "eject", name: "Eject briquette", event: "EJECTION_STARTED" },
  { id: "close", name: "Close gates", event: "GATES_CLOSING" }
];
const time = value => Date.parse(value) || 0;

function buildMachineProcess(control) {
  const online = control.device?.connected === true;
  const events = [...(control.events || [])]
    .filter(event => !event.deviceId || event.deviceId === control.deviceId)
    .map(event => ({ ...event, type: String(event.type).toUpperCase() }))
    .sort((a, b) => time(b.createdAt) - time(a.createdAt));
  const startIndex = events.findIndex(event => event.type === "PROCESS_STARTED");
  const start = startIndex < 0 ? null : events[startIndex];
  const rebootIndex = events.findIndex(event => event.type === "CONTROLLER_READY");
  const newerStartPending = (control.commands || []).some(command => command.command === "start" &&
    ["queued", "delivered"].includes(command.status) && time(command.createdAt) > time(start?.createdAt));
  const hasRun = Boolean(start && !(rebootIndex >= 0 && rebootIndex < startIndex) && !newerStartPending);
  const run = hasRun ? events.slice(0, startIndex + 1).filter(event =>
    !start.commandId || !event.commandId || event.commandId === start.commandId ||
    ["STOPPED", "FAULT"].includes(event.type)) : [];
  // A stop/fault ends a run; subsequent unrelated manual events cannot advance it.
  const terminalIndex = run.findLastIndex(event => ["STOPPED", "FAULT", "PROCESS_COMPLETED"].includes(event.type));
  const terminal = terminalIndex >= 0 ? run[terminalIndex] : null;
  const evidence = terminal ? run.slice(terminalIndex) : run;
  const stageEvent = evidence.find(event => STAGES.some(stage => stage.event === event.type));
  const current = STAGES.find(stage => stage.event === stageEvent?.type);
  const completed = terminal?.type === "PROCESS_COMPLETED";
  const interrupted = terminal && !completed;
  const stoppedWithoutEvent = hasRun && !terminal &&
    !["running", "starting"].includes(control.processState);
  let label = "Waiting for a forming run";
  if (newerStartPending) label = "Waiting for machine to start";
  else if (completed) label = "Forming sequence finished";
  else if (interrupted || stoppedWithoutEvent) label = "Forming sequence interrupted";
  else if (hasRun) label = current?.name || "Waiting for a step report";
  if (!online && hasRun) label = "Offline — last report: " + label;
  return {
    source: "controller_events",
    online,
    hasRun,
    state: !hasRun ? "waiting" : completed ? "completed" :
      interrupted || stoppedWithoutEvent ? "interrupted" : online ? "running" : "unknown",
    label,
    startedAt: hasRun ? start.createdAt : null,
    updatedAt: evidence[0]?.createdAt || null,
    detail: "Reported controller steps; motor and gate movement is not measured. Start drying separately after loading the chamber.",
    steps: STAGES.map(stage => {
      const reported = evidence.find(event => event.type === stage.event);
      const isCurrent = hasRun && online && !terminal && !stoppedWithoutEvent && current?.id === stage.id;
      return {
        id: stage.id, name: stage.name,
        status: isCurrent ? "Current" : reported ? "Reported" : "Not reported",
        detail: reported?.message || "Waiting for this step's controller report.",
        reportedAt: reported?.createdAt || null
      };
    })
  };
}

module.exports = { buildMachineProcess };
