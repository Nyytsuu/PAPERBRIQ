// Controller events describe commanded steps, not proof of physical movement.
const STAGES = [
  { id: "prepare", name: "Prepare gates", event: "PROCESS_STARTED", reached: 0, update: "Preparing the mixer and molder gates." },
  { id: "paper", name: "Waiting for paper", event: "WAITING_FOR_PAPER", reached: 1, update: "Ready for paper at the shredder inlet." },
  { id: "shred", name: "Shred and mix", event: "SHREDDER_MIXER_STARTED", reached: 1, update: "Shredding started." },
  { id: "mix", name: "Finish mixing", event: "SHREDDER_COMPLETED", reached: 2, update: "Shredding finished. The mixer is finishing the batch." },
  { id: "settle", name: "Wait for mixer gate", event: "MIXER_COMPLETED", reached: 3, update: "Mixing finished. Waiting for the mixer gate to open." },
  { id: "transfer", name: "Release into molder", event: "MATERIAL_RELEASE_STARTED", reached: 3, update: "Releasing material through the mixer gate into the molder." },
  { id: "press", name: "Press briquette", event: "PRESSING_STARTED", reached: 4, update: "Mixer gate close sequence finished. Pressing starts next." },
  { id: "eject", name: "Eject briquette", event: "EJECTION_STARTED", reached: 5, update: "Opening the molder gate so the pusher can eject the briquette." },
  { id: "close", name: "Close gates", event: "GATES_CLOSING", reached: 6, update: "Ejection ended. Closing the gates." }
];
const MILESTONES = ["Gates ready", "Shredding", "Mixing", "Transfer", "Pressing", "Ejection", "Gates closed"];
const MOTION_UPDATES = {
  SHREDDER_STARTED: "Shredder is running.",
  MIXER_STARTED: "Mixer is running.",
  PRESSER_STARTED: "Presser is moving.",
  PRESSER_COMPLETED: "Presser movement ended.",
  PUSHER_STARTED: "Pusher is moving.",
  PUSHER_COMPLETED: "Pusher movement ended.",
  BATCH_COMPLETED: "Batch finished.",
  PROCESS_COMPLETED: "Forming sequence finished.",
  STOPPED: "Forming stopped."
};
const time = value => Date.parse(value) || 0;
const isBatchCommand = command => /^batch_[1-3]$/.test(command);

function buildBatchControls(report, control, now = Date.now()) {
  const health = report?.health;
  const mask = health?.batchMask;
  const supported = Number.isInteger(mask) && mask >= 0 && mask <= 7;
  const age = now - Date.parse(report?.recordedAt);
  const fresh = control.device?.connected === true && health?.unoLink === true && age >= 0 && age < 15000;
  const busy = (control.commands || []).some(command => ["queued", "delivered"].includes(command.status));
  const safe = health && ["controlIo", "indicatorIo", "stopClosed", "emergencyClosed", "doorClosed"]
    .every(field => health[field] === true) && ["testActive", "fanOn", "unlockOn"]
    .every(field => health[field] === false);
  const canSelect = supported && fresh && !busy && safe && health.armed === true && mask !== 0;
  const available = canSelect ? [1, 2, 3].filter(batch => mask & (1 << (batch - 1))) : [];
  const reason = !fresh ? "Waiting for a fresh controller report."
    : !supported ? "Batch selection requires the three-batch Uno sketch and updated ESP32 firmware."
    : busy ? "Wait for the current command to finish."
    : !safe ? "Check the door, Stop and E-stop contacts, and machine connections."
    : health.armed !== true ? "Fix the fault and use Reset before choosing a batch."
    : !canSelect ? "Wait for the current batch to finish and check the machine and paper sensor." : "";
  return { available, reason };
}

const batchNumber = event => ["PROCESS_STARTED", "SHREDDER_MIXER_STARTED", "BATCH_COMPLETED"].includes(event.type)
  ? Number(/^Batch ([1-3])$/.exec(event.message || "")?.[1]) || null : null;

function activityUpdate(event) {
  if (event.type === "FAULT") return event.message || "Forming interrupted by a machine fault.";
  const stage = STAGES.find(step => step.event === event.type);
  // Manual mixer pulses can report the same event type as the forming sequence.
  if (event.type === "MIXER_COMPLETED" && /^Mixer (OFF|completed)$/i.test(event.message || "")) {
    return "Mixer movement ended.";
  }
  const update = stage?.update || MOTION_UPDATES[event.type];
  if (!update) return null;
  // Keep explicit timing from the controller. Receipt times cannot predict movement.
  return /\b\d+(?:\.\d+)?\s*(?:ms|s|sec|seconds?|minutes?)\b/i.test(event.message || "")
    ? event.message : update;
}

function stageReportsFor(evidence) {
  return evidence.filter(event => event.type !== "MIXER_COMPLETED" ||
    !/^Mixer (OFF|completed)$/i.test(event.message || ""));
}

function activityFor(events) {
  return events.flatMap(event => {
    const text = activityUpdate(event);
    return text ? [{ text, reportedAt: event.createdAt }] : [];
  }).slice(0, 5);
}

function currentStageFor(stageReports) {
  const event = stageReports.find(report => STAGES.some(stage => stage.event === report.type));
  return STAGES.find(stage => stage.event === event?.type);
}

function reportProgress(stageReports, isRunning, completed, milestoneCount = MILESTONES.length) {
  const current = currentStageFor(stageReports);
  const reached = completed ? milestoneCount : STAGES.reduce((highest, stage) =>
    stageReports.some(event => event.type === stage.event) ? Math.max(highest, stage.reached) : highest, 0);
  return {
    milestones: MILESTONES.slice(0, milestoneCount).map((name, index) => ({ name, completed: index < reached })),
    steps: STAGES.filter(stage => milestoneCount === MILESTONES.length || stage.reached < milestoneCount).map(stage => {
      const reported = stageReports.find(event => event.type === stage.event);
      return {
        id: stage.id, name: stage.name,
        status: isRunning && current?.id === stage.id ? "Current" : reported ? "Reported" : "Not reported",
        detail: reported ? activityUpdate(reported) : "Waiting for this step's controller report.",
        reportedAt: reported?.createdAt || null
      };
    })
  };
}

function reportBatches(evidence, isRunning, completed, label) {
  const eventsByBatch = [[], [], []];
  const singleBatch = evidence.some(event => event.type === "PROCESS_STARTED" && event.message === "Single batch");
  let latest = null;
  // Walk forward so each step belongs to the batch the controller reported at that time.
  for (const event of [...evidence].reverse()) {
    latest = singleBatch ? 1 : batchNumber(event) ?? latest;
    if (latest) eventsByBatch[latest - 1].push(event);
  }
  return {
    activeBatch: isRunning ? latest : null,
    latestBatch: latest,
    batches: eventsByBatch.map((events, index) => {
      const number = index + 1;
      const reports = events.reverse();
      const isCurrent = latest === number;
      const done = reports.some(event => event.type === "BATCH_COMPLETED") ||
        (reports.length > 0 && (!isCurrent || completed));
      return {
        number,
        label: done ? "Batch complete" : reports.length ? label : "Waiting for a batch report",
        ...reportProgress(stageReportsFor(reports), isCurrent && isRunning && !done, done, !singleBatch && number < 3 ? 5 : MILESTONES.length),
        activity: activityFor(reports)
      };
    })
  };
}

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
  const stageReports = stageReportsFor(evidence);
  const current = currentStageFor(stageReports);
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
  const stopFault = terminal?.type === "STOPPED" && run[terminalIndex - 1]?.type === "FAULT"
    ? [run[terminalIndex - 1]] : [];
  const isRunning = hasRun && online && !terminal && !stoppedWithoutEvent;
  return {
    source: "controller_events",
    online,
    hasRun,
    state: !hasRun ? "waiting" : completed ? "completed" :
      interrupted || stoppedWithoutEvent ? "interrupted" : online ? "running" : "unknown",
    label,
    ...reportProgress(stageReports, isRunning, completed),
    ...reportBatches([...stopFault, ...evidence], isRunning, completed, label),
    activity: activityFor([...stopFault, ...evidence]),
    startedAt: hasRun ? start.createdAt : null,
    updatedAt: evidence[0]?.createdAt || null,
    detail: "Reported controller steps; motor and gate movement is not measured. Start drying separately after loading the chamber."
  };
}

module.exports = { buildMachineProcess, buildBatchControls, isBatchCommand };
