function createLiveSummarySnapshot(baseSummary) {
  const now = new Date();
  const tick = Math.floor(now.getTime() / 4000);
  const cycle = tick % 60;
  const paperDelta = Number((cycle * 0.18 + wave(tick, 0.8, 1) * 0.12).toFixed(2));
  const briquetteDelta = Math.floor(cycle / 4);
  const totals = baseSummary.totals || {};
  const paperKg = Number((Number(totals.paperKg || 0) + paperDelta).toFixed(2));

  return {
    ...baseSummary,
    generatedAt: now.toISOString(),
    displayTime: formatManilaTime(now),
    totals: {
      paperKg,
      briquettes: Number(totals.briquettes || 0) + briquetteDelta,
      contributors: Number(totals.contributors || 0) + (cycle > 42 ? 1 : 0),
      co2KgAvoided: Number((Number(totals.co2KgAvoided || 0) + paperDelta * 0.82).toFixed(2))
    },
    collections: buildLiveCollections(baseSummary.collections || [], now, cycle),
    batches: buildLiveBatches(baseSummary.batches || [], cycle)
  };
}

function createLiveDashboardSnapshot(baseDashboard, summary) {
  const now = new Date();
  const tick = Math.floor(now.getTime() / 4000);
  const rainwaterLevel = clamp(Math.round(77 + wave(tick, 0.38, 2) * 5), 68, 90);
  const batteryLevel = clamp(Math.round(91 + wave(tick, 0.32, 4) * 4), 84, 98);
  const temperatureC = Number((29.4 + wave(tick, 0.34, 4) * 2.1).toFixed(1));
  const humidityPercent = clamp(Math.round(65 + wave(tick, 0.29, 6) * 7), 45, 78);
  const powerOutputWatts = clamp(Math.round(238 + wave(tick, 0.6, 5) * 34), 180, 290);
  const overallProgress = clamp(Math.round(58 + (tick % 34) * 1.1), 58, 94);
  const completedBriquettes = Math.max(8, Math.floor(overallProgress / 6));

  return {
    ...baseDashboard,
    generatedAt: now.toISOString(),
    displayTime: formatManilaTime(now),
    summary,
    overallProgress,
    systemStatus: {
      ...(baseDashboard.systemStatus || {}),
      status: "Running",
      message: `Live machine sync completed at ${formatManilaTime(now)}.`,
      rainwaterLevel,
      batteryLevel,
      temperatureC,
      humidityPercent,
      powerOutputWatts
    },
    processProgress: buildLiveProcess(baseDashboard.processProgress || [], overallProgress),
    processLog: buildLiveProcessLog(baseDashboard.processLog || [], now, overallProgress),
    weeklySummary: {
      ...(baseDashboard.weeklySummary || {}),
      paperWasteKg: Math.round(Number(summary?.totals?.paperKg || 0) * 0.1),
      briquettesProduced: completedBriquettes,
      activeContributors: Number(summary?.totals?.contributors || 0)
    }
  };
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
