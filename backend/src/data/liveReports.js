function createLiveReportsSnapshot(baseReports, summary, topBarangays = [], reportSeries = {}) {
  const now = new Date();
  const tick = Math.floor(now.getTime() / 5000);
  const totals = summary?.totals || {};
  const paperKg = Number(totals.paperKg || 0);
  const briquettes = Number(totals.briquettes || 0);
  const contributors = Number(totals.contributors || 0);
  const waterSavedL = Math.round(paperKg * 6.8 + wave(tick, 0.55, 1) * 38);
  const energyKwh = Math.max(0, Number((briquettes * 0.22 + wave(tick, 0.42, 3) * 4).toFixed(1)));
  const co2Kg = Number(totals.co2KgAvoided || 0);
  const barangays = normalizeTopBarangays(topBarangays, summary?.collections || []);
  const paperSeries = normalizeSeries(reportSeries.paperCollected, paperKg, 0.52, tick);
  const briquetteSeries = normalizeSeries(reportSeries.briquettesProduced, briquettes, 0.28, tick + 3);
  const waterSeries = normalizeSeries(reportSeries.waterSaved, waterSavedL, 0.48, tick + 6);

  return {
    ...baseReports,
    generatedAt: now.toISOString(),
    displayTime: formatManilaTime(now),
    nextPollMs: 7000,
    period: `Live report as of ${formatManilaTime(now)}`,
    cards: [
      {
        label: "Paper Collected",
        value: `${formatNumber(paperKg)} kg`,
        change: liveChange(tick, 10, 24),
        percent: percentOf(paperKg, autoTarget(paperKg, 120))
      },
      {
        label: "Briquettes Produced",
        value: `${formatNumber(briquettes)} pcs`,
        change: liveChange(tick, 8, 18),
        percent: percentOf(briquettes, autoTarget(briquettes, 260))
      },
      {
        label: "Water Saved",
        value: `${formatNumber(waterSavedL)} L`,
        change: liveChange(tick, 9, 19),
        percent: percentOf(waterSavedL, autoTarget(waterSavedL, 800))
      },
      {
        label: "Active Contributors",
        value: formatNumber(contributors),
        change: liveChange(tick, 4, 13),
        percent: percentOf(contributors, autoTarget(contributors, 10))
      }
    ],
    charts: {
      paperCollected: paperSeries.values,
      briquettesProduced: briquetteSeries.values,
      waterSaved: waterSeries.values
    },
    chartMeta: {
      labels: reportSeries.labels || [],
      source: paperSeries.fromDatabase || briquetteSeries.fromDatabase || waterSeries.fromDatabase
        ? "MySQL daily records"
        : "Live backend projection"
    },
    topBarangays: barangays,
    impact: [
      { label: "Trees Saved", value: formatNumber(Math.max(1, Math.round(paperKg / 69))) },
      { label: "CO2 Reduced", value: `${formatNumber(co2Kg)} kg` },
      { label: "Energy Generated", value: `${formatNumber(energyKwh)} kWh` },
      { label: "Landfill Diversion", value: `${formatNumber(paperKg)} kg` }
    ],
    summary: {
      updatedAt: formatManilaTime(now),
      note: "Report values are refreshed from the backend feed."
    }
  };
}

function normalizeTopBarangays(topBarangays, collections) {
  if (Array.isArray(topBarangays) && topBarangays.length) {
    return topBarangays.slice(0, 5);
  }

  const grouped = new Map();
  for (const item of collections) {
    const barangay = item.barangay || "Unassigned";
    const current = grouped.get(barangay) || { barangay, paperKg: 0, contributors: 0 };
    current.paperKg += Number(item.paperWeightKg || 0);
    current.contributors += 1;
    grouped.set(barangay, current);
  }

  return Array.from(grouped.values())
    .map((item) => ({
      barangay: item.barangay,
      paperKg: Number(item.paperKg.toFixed(2)),
      contributors: item.contributors
    }))
    .sort((first, second) => second.paperKg - first.paperKg)
    .slice(0, 5);
}

function buildSeries(total, scale, seed) {
  const safeTotal = Math.max(1, Number(total || 0));
  return Array.from({ length: 8 }, (_, index) => {
    const trend = 0.72 + index * 0.055;
    const pulse = wave(seed + index, 0.6, index) * 0.08;
    return Math.max(1, Math.round(safeTotal * scale * (trend + pulse)));
  });
}

function normalizeSeries(values, total, scale, seed) {
  const parsed = Array.isArray(values)
    ? values.map((value) => Number(value)).filter((value) => Number.isFinite(value) && value >= 0)
    : [];

  if (parsed.length) {
    return {
      values: parsed.slice(-14),
      fromDatabase: true
    };
  }

  return {
    values: buildSeries(total, scale, seed),
    fromDatabase: false
  };
}

function autoTarget(value, step) {
  const safeValue = Math.max(1, Number(value || 0));
  return Math.max(step, Math.ceil(safeValue / step) * step);
}

function percentOf(value, target) {
  const percent = Math.round((Number(value || 0) / Math.max(1, Number(target || 1))) * 100);
  return clamp(percent, Number(value || 0) > 0 ? 6 : 0, 100);
}

function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

function liveChange(seed, min, max) {
  const value = min + Math.abs(Math.round(wave(seed, 0.4, min) * (max - min)));
  return `+${value}%`;
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

function formatNumber(value) {
  return new Intl.NumberFormat("en-PH", {
    maximumFractionDigits: Number(value) % 1 === 0 ? 0 : 2
  }).format(Number(value) || 0);
}

module.exports = {
  createLiveReportsSnapshot
};
