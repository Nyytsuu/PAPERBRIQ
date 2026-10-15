const { dataStore } = require("../db");

const DEFAULT_CHANNELS = ["dashboard", "reports", "iot"];
const STREAM_INTERVAL_MS = 2500;
const HEARTBEAT_INTERVAL_MS = 15000;

async function streamLive(req, res, requestUrl) {
  const channels = getRequestedChannels(requestUrl);
  const once = requestUrl.searchParams.get("once") === "1";
  let closed = false;
  let inFlight = false;
  let intervalId;
  let heartbeatId;

  res.writeHead(200, {
    "Content-Type": "text/event-stream; charset=utf-8",
    "Cache-Control": "no-cache, no-transform",
    Connection: "keep-alive",
    "X-Accel-Buffering": "no"
  });

  req.on("close", () => {
    closed = true;
    if (intervalId) clearInterval(intervalId);
    if (heartbeatId) clearInterval(heartbeatId);
  });

  async function sendSnapshot() {
    if (closed || inFlight) return;
    inFlight = true;

    try {
      const payload = await buildLiveSnapshot(channels);
      writeEvent(res, "snapshot", payload);
      if (once && !closed) {
        closed = true;
        res.end();
      }
    } catch (error) {
      if (!closed) {
        writeEvent(res, "stream-error", {
          message: error.message || "Unable to load live data."
        });
      }
    } finally {
      inFlight = false;
    }
  }

  await sendSnapshot();

  if (once || closed) return;

  intervalId = setInterval(sendSnapshot, STREAM_INTERVAL_MS);
  heartbeatId = setInterval(() => {
    if (!closed) res.write(`: heartbeat ${Date.now()}\n\n`);
  }, HEARTBEAT_INTERVAL_MS);
}

async function buildLiveSnapshot(channels) {
  const payload = {
    generatedAt: new Date().toISOString(),
    channels
  };

  await Promise.all(channels.map(async (channel) => {
    if (channel === "dashboard") {
      payload.dashboard = await dataStore.getDashboard();
    }

    if (channel === "reports") {
      payload.reports = await dataStore.getReports();
    }

    if (channel === "iot") {
      payload.iot = await dataStore.getIotMonitoring();
    }

    if (channel === "rewards") {
      payload.rewards = await dataStore.getRewards();
    }
  }));

  return payload;
}

function getRequestedChannels(requestUrl) {
  const requested = String(requestUrl.searchParams.get("channels") || "")
    .split(",")
    .map((channel) => channel.trim().toLowerCase())
    .filter(Boolean);
  const allowed = new Set([...DEFAULT_CHANNELS, "rewards"]);
  const selected = requested.filter((channel) => allowed.has(channel));
  return selected.length ? selected : DEFAULT_CHANNELS;
}

function writeEvent(res, eventName, payload) {
  res.write(`event: ${eventName}\n`);
  res.write(`data: ${JSON.stringify(payload)}\n\n`);
}

module.exports = {
  streamLive
};
