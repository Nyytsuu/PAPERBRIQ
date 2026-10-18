const COMMAND_STATUSES = new Set(["queued", "delivered", "completed", "failed", "expired"]);
const ACK_STATUSES = new Set(["delivered", "completed", "failed"]);
const TERMINAL_COMMAND_STATUSES = new Set(["completed", "failed", "expired"]);

function toIso(value) {
  if (!value) return null;
  if (value instanceof Date) return value.toISOString();
  const parsed = new Date(value);
  return Number.isNaN(parsed.getTime()) ? null : parsed.toISOString();
}

function mapDeviceCommand(row) {
  if (!row) return null;
  const status = String(row.status || "queued").toLowerCase();
  return {
    id: String(row.id),
    deviceId: String(row.device_id ?? row.deviceId ?? ""),
    command: String(row.command || ""),
    status: COMMAND_STATUSES.has(status) ? status : "queued",
    createdAt: toIso(row.created_at ?? row.createdAt),
    expiresAt: toIso(row.expires_at ?? row.expiresAt),
    deliveredAt: toIso(row.delivered_at ?? row.deliveredAt),
    completedAt: toIso(row.completed_at ?? row.completedAt),
    message: String(row.message || "")
  };
}

function mapDeviceEvent(row) {
  if (!row) return null;
  return {
    id: String(row.id),
    deviceId: String(row.device_id ?? row.deviceId ?? ""),
    type: String(row.event_type ?? row.type ?? "event"),
    severity: String(row.severity || "info"),
    component: String(row.component || "system"),
    message: String(row.message || ""),
    processState: nullableString(row.process_state ?? row.processState),
    commandId: nullableString(row.command_id ?? row.commandId),
    metadata: parseMetadata(row.metadata),
    createdAt: toIso(row.created_at ?? row.createdAt)
  };
}

function parseMetadata(value) {
  if (!value) return {};
  if (typeof value === "object") return value;
  try {
    const parsed = JSON.parse(value);
    return parsed && typeof parsed === "object" && !Array.isArray(parsed) ? parsed : {};
  } catch (error) {
    return {};
  }
}

function nullableString(value) {
  return value === undefined || value === null || value === "" ? null : String(value);
}

function canAcknowledge(currentStatus, nextStatus) {
  if (!ACK_STATUSES.has(nextStatus)) return false;
  if (currentStatus === nextStatus) return true;
  if (currentStatus === "queued") return true;
  if (currentStatus === "delivered") return nextStatus === "completed" || nextStatus === "failed";
  return false;
}

function commandProcessState(command) {
  if (!command) return "idle";
  if (command.status === "failed") return "fault";
  if (command.status === "expired") return "idle";

  if (command.command === "start") {
    return command.status === "completed" ? "running" : "starting";
  }
  if (command.command === "stop") {
    return command.status === "completed" ? "stopped" : "stopping";
  }
  if (command.command === "reset") {
    return command.status === "completed" ? "idle" : "resetting";
  }
  return "idle";
}

function timeValue(value) {
  const parsed = value ? new Date(value).getTime() : Number.NaN;
  return Number.isNaN(parsed) ? Number.NEGATIVE_INFINITY : parsed;
}

function inferProcessState(events, commands) {
  const latestStateEvent = events.find((event) => event.processState);
  const latest = commands[0];
  if (!latestStateEvent) return commandProcessState(latest);
  if (!latest) return latestStateEvent.processState;

  // A completed command can be newer than the most recently stored event.
  // In particular, a successful RESET must supersede an older fault even if
  // its optional ARMED event is delayed or lost on the UART/network path.
  const commandAt = timeValue(
    latest.completedAt || latest.deliveredAt || latest.createdAt
  );
  const eventAt = timeValue(latestStateEvent.createdAt);
  if (commandAt >= eventAt) return commandProcessState(latest);
  return latestStateEvent.processState;
}

function publicStoreError(statusCode, publicMessage) {
  const error = new Error(publicMessage);
  error.statusCode = statusCode;
  error.publicMessage = publicMessage;
  return error;
}

module.exports = {
  ACK_STATUSES,
  TERMINAL_COMMAND_STATUSES,
  canAcknowledge,
  inferProcessState,
  mapDeviceCommand,
  mapDeviceEvent,
  publicStoreError,
  toIso
};
