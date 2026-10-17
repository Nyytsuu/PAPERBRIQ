function createLiveSummarySnapshot(baseSummary) {
  const now = new Date();

  return {
    ...baseSummary,
    generatedAt: now.toISOString(),
    displayTime: formatManilaTime(now)
  };
}

function createLiveDashboardSnapshot(baseDashboard, summary, iotData = {}) {
  const now = new Date();
  const sensors = iotData.sensors || [];
  const connected = sensors.some((sensor) => sensor.source === "esp32" && telemetryIsFresh(sensor.updatedAt, now));
  const sensorValue = (name) => {
    const sensor = sensors.find((item) => item.name === name && item.source === "esp32" && telemetryIsFresh(item.updatedAt, now));
    return sensor ? Number(sensor.value) : null;
  };
  const overallProgress = connected ? Number(baseDashboard.overallProgress || 0) : 0;

  return {
    ...baseDashboard,
    generatedAt: now.toISOString(),
    displayTime: formatManilaTime(now),
    summary,
    overallProgress,
    systemStatus: {
      ...(baseDashboard.systemStatus || {}),
      status: connected ? "Online" : "Offline",
      message: connected ? `ESP32 telemetry received at ${formatManilaTime(now)}.` : "Waiting for ESP32 telemetry.",
      rainwaterLevel: sensorValue("Rainwater Level"),
      batteryLevel: null,
      temperatureC: sensorValue("Temperature"),
      humidityPercent: sensorValue("Humidity"),
      powerOutputWatts: null
    },
    processProgress: connected ? (baseDashboard.processProgress || []) : [],
    processLog: connected ? (baseDashboard.processLog || []) : [],
    weeklySummary: {
      ...(baseDashboard.weeklySummary || {})
    }
  };
}

function telemetryIsFresh(value, now) {
  if (!value) return false;
  const timestamp = new Date(value);
  if (Number.isNaN(timestamp.getTime())) return false;
  const configured = Number(process.env.IOT_TELEMETRY_ONLINE_SECONDS || 20);
  const onlineSeconds = Number.isFinite(configured) ? Math.min(600, Math.max(10, configured)) : 20;
  return now.getTime() - timestamp.getTime() <= onlineSeconds * 1000;
}

function buildLiveCollections(collections, now, cycle) {
  const liveCollection = {
    barangay: cycle % 2 === 0 ? "San Isidro" : "Central",
    paperWeightKg: Number((12 + (cycle % 9) * 1.35).toFixed(2)),
    status: cycle % 3 === 0 ? "sorted" : cycle % 3 === 1 ? "received" : "queued",
    collectedAt: `Live sync, ${formatManilaTime(now)}`
  };

  return [liveCollection, ...collections].slice(0, 4);
}

function buildLiveBatches(batches, cycle) {
  const liveBatch = {
    batchCode: `PBQ-LIVE-${String(cycle + 1).padStart(2, "0")}`,
    briquettesProduced: 180 + cycle * 3,
    qualityStatus: cycle % 4 === 0 ? "curing" : "ready"
  };

  return [liveBatch, ...batches].slice(0, 3);
}

function buildLiveProcess(processProgress, overallProgress) {
  const defaults = [
    { step: "Collect Paper", progress: 100, status: "completed", detail: "Collection verified" },
    { step: "Shred Paper", progress: 100, status: "completed", detail: "Shredding complete" },
    { step: "Pulp and Mix", progress: clamp(overallProgress + 8, 62, 100), status: "in_progress", detail: `Mixture hydration ${clamp(overallProgress + 8, 62, 100)}%` },
    { step: "Press and Mold", progress: clamp(overallProgress - 28, 0, 88), status: overallProgress > 68 ? "in_progress" : "not_started", detail: `${Math.max(0, Math.floor((overallProgress - 50) / 4))} / 20 briquettes` },
    { step: "Drying", progress: clamp(overallProgress - 58, 0, 70), status: overallProgress > 78 ? "in_progress" : "not_started", detail: overallProgress > 78 ? "Drying tray warming" : "Waiting to start" }
  ];

  return defaults.map((fallback, index) => ({
    ...fallback,
    ...(processProgress[index] || {}),
    progress: fallback.progress,
    status: fallback.status,
    detail: fallback.detail
  }));
}

function buildLiveProcessLog(processLog, now, overallProgress) {
  const liveEvent = {
    time: formatManilaTime(now),
    event: overallProgress > 78 ? "Drying Tray Warming" : "Pulp Mix Sensor Updated",
    type: "Live"
  };

  return [liveEvent, ...processLog].slice(0, 5);
}

function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

function wave(seed, speed, offset) {
  return Math.sin(seed * speed + offset * 1.31);
}

function formatManilaTime(date) {
  return new Intl.DateTimeFormat("en-PH", {
    timeZone: "Asia/Manila",
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit"
  }).format(date);
}

module.exports = {
  createLiveDashboardSnapshot,
  createLiveSummarySnapshot
};
