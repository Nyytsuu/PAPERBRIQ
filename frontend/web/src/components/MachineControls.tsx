import React from "react";
import { backendFetch } from "../api/backendFetch";
import { ConfirmModal, Panel } from "./ui";

const DEVICE_ID = "paperbriq-esp32";
const CONTROL_KEY_STORAGE = "paperbriqControlKey";
const CONTROL_POLL_MS = 3000;
const TOAST_EVENT_TYPES = new Set([
  "MIXER_STARTED",
  "MIXER_COMPLETED",
  "PROCESS_STARTED",
  "PROCESS_COMPLETED"
]);

export default function MachineControls() {
  const [controlKey, setControlKey] = React.useState(() => readSessionControlKey());
  const [status, setStatus] = React.useState(null);
  const [statusMessage, setStatusMessage] = React.useState("Checking remote-control status...");
  const [commandMessage, setCommandMessage] = React.useState(null);
  const [pendingCommand, setPendingCommand] = React.useState("");
  const [confirmCommand, setConfirmCommand] = React.useState("");
  const [toasts, setToasts] = React.useState([]);
  const [errorAlerts, setErrorAlerts] = React.useState([]);
  const seenEventIds = React.useRef(new Set());
  const hasLoadedEvents = React.useRef(false);
  const toastTimers = React.useRef(new Map());

  const pushToast = React.useCallback((event) => {
    const id = eventIdentity(event);
    const toast = {
      id,
      title: eventTitle(event.type),
      message: event.message || eventTitle(event.type)
    };

    setToasts((current) => [...current.filter((item) => item.id !== id), toast].slice(-4));
    const previousTimer = toastTimers.current.get(id);
    if (previousTimer) window.clearTimeout(previousTimer);
    const timer = window.setTimeout(() => {
      setToasts((current) => current.filter((item) => item.id !== id));
      toastTimers.current.delete(id);
    }, 6000);
    toastTimers.current.set(id, timer);
  }, []);

  const processEvents = React.useCallback((events, processState) => {
    const orderedEvents = Array.isArray(events) ? [...events].reverse() : [];
    const newEvents = [];

    orderedEvents.forEach((event) => {
      const id = eventIdentity(event);
      if (seenEventIds.current.has(id)) return;
      seenEventIds.current.add(id);
      newEvents.push(event);
    });

    if (hasLoadedEvents.current) {
      newEvents.forEach((event) => {
        if (isErrorEvent(event)) {
          setErrorAlerts((current) => [...current, { ...event, notificationId: eventIdentity(event) }]);
        } else if (TOAST_EVENT_TYPES.has(normalizeEventType(event.type))) {
          pushToast(event);
        }
      });
    } else if (isFaultState(processState)) {
      const currentError = [...orderedEvents].reverse().find(isErrorEvent);
      if (currentError) {
        setErrorAlerts([{ ...currentError, notificationId: eventIdentity(currentError) }]);
      }
    }

    hasLoadedEvents.current = true;
  }, [pushToast]);

  const loadControlStatus = React.useCallback(async () => {
    try {
      const result = await backendFetch(`/api/device/control-status?deviceId=${encodeURIComponent(DEVICE_ID)}`);
      setStatus(result);
      setStatusMessage("");
      processEvents(result.events, result.processState);
      return result;
    } catch (error) {
      setStatus(null);
      setStatusMessage(error.message || "Unable to load remote-control status.");
      return null;
    }
  }, [processEvents]);

  React.useEffect(() => {
    let isMounted = true;

    async function refresh() {
      if (!isMounted) return;
      await loadControlStatus();
    }

    refresh();
    const intervalId = window.setInterval(refresh, CONTROL_POLL_MS);

    return () => {
      isMounted = false;
      window.clearInterval(intervalId);
      toastTimers.current.forEach((timer) => window.clearTimeout(timer));
      toastTimers.current.clear();
    };
  }, [loadControlStatus]);

  function updateControlKey(value) {
    setControlKey(value);
    if (value) sessionStorage.setItem(CONTROL_KEY_STORAGE, value);
    else sessionStorage.removeItem(CONTROL_KEY_STORAGE);
    setCommandMessage(null);
  }

  async function sendCommand(command) {
    const key = controlKey.trim();
    if (!key) {
      setCommandMessage({ tone: "error", text: "Enter the operator control key before sending a command." });
      return;
    }

    setPendingCommand(command);
    setCommandMessage(null);

    try {
      const result = await backendFetch("/api/device/commands", {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          "X-Control-Key": key
        },
        body: JSON.stringify({
          deviceId: DEVICE_ID,
          command,
          source: "web"
        })
      });
      const queuedCommand = result.command || {};
      const readableCommand = command === "stop" ? "Software Stop" : commandLabel(command);
      setCommandMessage({
        tone: "success",
        text: `${readableCommand} queued${queuedCommand.id ? ` as command ${queuedCommand.id}` : ""}. Waiting for the ESP32 to acknowledge it.`
      });
      await loadControlStatus();
    } catch (error) {
      setCommandMessage({ tone: "error", text: error.message || `Unable to queue ${commandLabel(command)}.` });
    } finally {
      setPendingCommand("");
    }
  }

  const processState = String(status?.processState || "unknown");
  const remoteAvailable = Boolean(status?.enabled && status?.configured);
  const connected = Boolean(status?.device?.connected);
  const keyReady = Boolean(controlKey.trim());
  const safeToStart = isStartableState(processState);
  const faulted = isFaultState(processState);
  const latestCommand = Array.isArray(status?.commands) ? status.commands[0] : null;
  const latestCommandInFlight = ["queued", "delivered"].includes(String(latestCommand?.status || "").toLowerCase());
  const stopAlreadyQueued = latestCommandInFlight && String(latestCommand?.command || "").toLowerCase() === "stop";
  const startEnabled = remoteAvailable && connected && keyReady && safeToStart && !pendingCommand && !latestCommandInFlight;
  const stopEnabled = remoteAvailable && keyReady && !pendingCommand && !stopAlreadyQueued;
  const resetEnabled = remoteAvailable && connected && keyReady && faulted && !pendingCommand && !latestCommandInFlight;
  const activeError = errorAlerts[0] || null;

  return (
    <>
      <Panel
        title="Machine Controls"
        subtitle="Authorized controls for the connected PaperBriq station."
        action={
          <span className={`control-connection-pill ${status ? (connected ? "connected" : "offline") : "pending"}`}>
            <span aria-hidden="true" />
            {status ? (connected ? "ESP32 connected" : "ESP32 offline") : "Checking ESP32"}
          </span>
        }
      >
        <div className="control-key-row">
          <label className="control-key-field">
            <span>Operator control key</span>
            <input
              type="password"
              autoComplete="off"
              spellCheck="false"
              value={controlKey}
              placeholder="Enter control key"
              onChange={(event) => updateControlKey(event.target.value)}
            />
            <small>Kept only in this browser tab session. It is never saved to local storage.</small>
          </label>
          {controlKey && (
            <button className="forget-control-key" type="button" onClick={() => updateControlKey("")}>
              Forget key
            </button>
          )}
        </div>

        <div className="control-status-grid" aria-label="Machine control status">
          <StatusItem label="Process state" value={humanize(processState)} tone={faulted ? "danger" : connected ? "ready" : "muted"} />
          <StatusItem
            label="Remote control"
            value={remoteAvailable ? "Enabled" : status?.configured ? "Disabled" : "Not configured"}
            tone={remoteAvailable ? "ready" : "danger"}
          />
          <StatusItem
            label="Latest command"
            value={latestCommand ? `${commandLabel(latestCommand.command)} · ${humanize(latestCommand.status)}` : "No commands yet"}
            tone={latestCommand?.status === "failed" ? "danger" : "muted"}
          />
        </div>

        <div className="machine-control-grid">
          <ControlButton
            tone="start"
            label={pendingCommand === "start" ? "Queuing Start..." : "Start Process"}
            detail={startDisabledReason({ status, remoteAvailable, connected, keyReady, safeToStart, processState, latestCommandInFlight })}
            disabled={!startEnabled}
            onClick={() => setConfirmCommand("start")}
          />
          <ControlButton
            tone="stop"
            label={pendingCommand === "stop" ? "Queuing Stop..." : "Software Stop"}
            detail={stopDisabledReason({ status, remoteAvailable, keyReady, stopAlreadyQueued })}
            disabled={!stopEnabled}
            onClick={() => sendCommand("stop")}
          />
          <ControlButton
            tone="reset"
            label={pendingCommand === "reset" ? "Queuing Reset..." : "Reset Fault"}
            detail={resetDisabledReason({ status, remoteAvailable, connected, keyReady, faulted, processState, latestCommandInFlight })}
            disabled={!resetEnabled}
            onClick={() => setConfirmCommand("reset")}
          />
        </div>

        <p className="software-stop-note">
          <strong>Software Stop is not an emergency stop.</strong> Use the hard-wired emergency disconnect whenever anyone or anything is in danger.
        </p>

        {statusMessage && <p className="control-feedback error" role="status">{statusMessage}</p>}
        {commandMessage && (
          <p className={`control-feedback ${commandMessage.tone}`} role="status">{commandMessage.text}</p>
        )}
      </Panel>

      <div className="event-toast-region" aria-live="polite" aria-label="Machine event notifications">
        {toasts.map((toast) => (
          <article className="event-toast" key={toast.id}>
            <span aria-hidden="true">✓</span>
            <div>
              <strong>{toast.title}</strong>
              <p>{toast.message}</p>
            </div>
            <button
              type="button"
              aria-label={`Dismiss ${toast.title}`}
              onClick={() => setToasts((current) => current.filter((item) => item.id !== toast.id))}
            >
              ×
            </button>
          </article>
        ))}
      </div>

      {activeError && (
        <div className="modal-backdrop event-alert-backdrop" role="presentation">
          <section className="confirm-modal danger event-alert-modal" role="alertdialog" aria-modal="true" aria-labelledby="machineErrorTitle">
            <span className="event-alert-label">Machine error</span>
            <h2 id="machineErrorTitle">{eventTitle(activeError.type)}</h2>
            <p>{activeError.message || "The PaperBriq controller reported an error."}</p>
            <dl>
              <div><dt>Component</dt><dd>{activeError.component || "Machine"}</dd></div>
              <div><dt>Process state</dt><dd>{humanize(activeError.processState || processState)}</dd></div>
              <div><dt>Reported</dt><dd>{formatTimestamp(activeError.createdAt)}</dd></div>
            </dl>
            <p className="event-alert-safety">Inspect the machine locally. Acknowledge only after the area is safe.</p>
            <div className="confirm-actions">
              <button className="primary-button confirm-button" type="button" onClick={() => setErrorAlerts((current) => current.slice(1))}>
                Acknowledge alert
              </button>
            </div>
          </section>
        </div>
      )}

      {confirmCommand === "start" && (
        <ConfirmModal
          title="Start the PaperBriq process?"
          message="Confirm the machine area is clear, guards are in place, and local safety interlocks are ready. The ESP32 will receive a queued start command."
          confirmLabel="Confirm Start"
          onCancel={() => setConfirmCommand("")}
          onConfirm={() => {
            setConfirmCommand("");
            sendCommand("start");
          }}
        />
      )}

      {confirmCommand === "reset" && (
        <ConfirmModal
          title="Reset the software fault?"
          message="This only requests a controller fault reset. It does not release a physical emergency stop and it will not restart the machine."
          confirmLabel="Reset Fault"
          tone="danger"
          onCancel={() => setConfirmCommand("")}
          onConfirm={() => {
            setConfirmCommand("");
            sendCommand("reset");
          }}
        />
      )}
    </>
  );
}

function StatusItem({ label, value, tone }) {
  return (
    <article className={`control-status-item ${tone || "muted"}`}>
      <span>{label}</span>
      <strong>{value}</strong>
    </article>
  );
}

function ControlButton({ tone, label, detail, disabled, onClick }) {
  return (
    <div className={`machine-control-action ${tone}`}>
      <button className="machine-control-button" type="button" disabled={disabled} onClick={onClick}>
        {label}
      </button>
      <small>{detail}</small>
    </div>
  );
}

function startDisabledReason({ status, remoteAvailable, connected, keyReady, safeToStart, processState, latestCommandInFlight }) {
  if (!status) return "Waiting for backend status.";
  if (!remoteAvailable) return status.configured ? "Remote control is disabled." : "Server control key is not configured.";
  if (!connected) return "Unavailable while the ESP32 is offline.";
  if (!keyReady) return "Enter the operator control key.";
  if (latestCommandInFlight) return "Waiting for the ESP32 to finish the queued command.";
  if (!safeToStart) return `Unavailable while process state is ${humanize(processState)}.`;
  return "Requires confirmation before the command is queued.";
}

function stopDisabledReason({ status, remoteAvailable, keyReady, stopAlreadyQueued }) {
  if (!status) return "Waiting for backend status.";
  if (!remoteAvailable) return status.configured ? "Remote control is disabled." : "Server control key is not configured.";
  if (!keyReady) return "Enter the operator control key.";
  if (stopAlreadyQueued) return "A Software Stop is already waiting for acknowledgement.";
  return "Queues a stop request even if the ESP32 has just disconnected.";
}

function resetDisabledReason({ status, remoteAvailable, connected, keyReady, faulted, processState, latestCommandInFlight }) {
  if (!status) return "Waiting for backend status.";
  if (!remoteAvailable) return status.configured ? "Remote control is disabled." : "Server control key is not configured.";
  if (!connected) return "Unavailable while the ESP32 is offline.";
  if (!keyReady) return "Enter the operator control key.";
  if (latestCommandInFlight) return "Waiting for the ESP32 to finish the queued command.";
  if (!faulted) return `Available only for a reported fault, not ${humanize(processState)}.`;
  return "Clears a software fault only; it never restarts the machine.";
}

function readSessionControlKey() {
  try {
    return sessionStorage.getItem(CONTROL_KEY_STORAGE) || "";
  } catch (error) {
    return "";
  }
}

function isStartableState(value) {
  return /^(idle|ready|stopped|complete|completed)$/i.test(String(value || "").trim());
}

function isFaultState(value) {
  return /(fault|error|alarm)/i.test(String(value || ""));
}

function isErrorEvent(event) {
  const severity = String(event?.severity || "").toLowerCase();
  return severity === "error" || severity === "critical" || severity === "fatal";
}

function normalizeEventType(type) {
  return String(type || "EVENT").trim().replace(/[\s-]+/g, "_").toUpperCase();
}

function eventIdentity(event) {
  if (event?.id !== undefined && event?.id !== null) return String(event.id);
  return [event?.type, event?.createdAt, event?.commandId, event?.message].filter(Boolean).join("|") || `event-${Date.now()}`;
}

function eventTitle(type) {
  const normalized = normalizeEventType(type);
  const titles = {
    MIXER_STARTED: "Mixer started",
    MIXER_COMPLETED: "Mixer completed",
    PROCESS_STARTED: "Process started",
    PROCESS_COMPLETED: "Process completed"
  };
  return titles[normalized] || humanize(normalized);
}

function commandLabel(command) {
  const normalized = String(command || "").toLowerCase();
  if (normalized === "start") return "Start Process";
  if (normalized === "stop") return "Software Stop";
  if (normalized === "reset") return "Reset Fault";
  return humanize(command || "Command");
}

function humanize(value) {
  const text = String(value || "Unknown").trim().replace(/[_-]+/g, " ").toLowerCase();
  return text.replace(/\b\w/g, (letter) => letter.toUpperCase());
}

function formatTimestamp(value) {
  if (!value) return "Just now";
  const parsed = new Date(value);
  if (Number.isNaN(parsed.getTime())) return String(value);
  return parsed.toLocaleString([], { dateStyle: "medium", timeStyle: "short" });
}
