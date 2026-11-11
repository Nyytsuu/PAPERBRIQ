const DAY_SECONDS = 86400;
const MANILA_OFFSET_SECONDS = 8 * 3600;
const SHEETS_PER_BATCH = 4;
const dateLabel = new Intl.DateTimeFormat("en-PH", {
  month: "short", day: "numeric", timeZone: "UTC"
});
const monthLabel = new Intl.DateTimeFormat("en-PH", {
  month: "long", year: "numeric", timeZone: "UTC"
});

function reportCalendar(now) {
  const today = Math.floor((now.getTime() / 1000 + MANILA_OFFSET_SECONDS) / DAY_SECONDS);
  const localDate = new Date(today * DAY_SECONDS * 1000);
  const year = localDate.getUTCFullYear();
  const month = localDate.getUTCMonth();
  const current = Date.UTC(year, month, 1) / (DAY_SECONDS * 1000);
  const previous = Date.UTC(year, month - 1, 1) / (DAY_SECONDS * 1000);
  return { today, current, previous, fromSeconds: previous * DAY_SECONDS - MANILA_OFFSET_SECONDS,
    toSeconds: (today + 1) * DAY_SECONDS - MANILA_OFFSET_SECONDS };
}

function buildMonthlyReports(calendar, records, batchRecords) {
  const totals = { paper: new Map(), briquettes: new Map(), batches: new Map() };
  for (const { report_day, metric, amount } of [...(records || []), ...(batchRecords || [])]) {
    const key = Number(report_day);
    const map = totals[metric];
    if (map) map.set(key, (map.get(key) || 0) + Number(amount));
  }

  function paperSheets(from, to) {
    return batchRecords == null ? null : SHEETS_PER_BATCH * (sum(totals.batches, from, to) ?? 0);
  }

  function sum(map, from, to) {
    let total = null;
    for (let day = from; day <= to; day++) {
      if (map.has(day)) total = (total ?? 0) + map.get(day);
    }
    return total === null ? null : Math.round(total * 100) / 100;
  }

  function period(from, to) {
    const start = from * DAY_SECONDS * 1000;
    const end = to * DAY_SECONDS * 1000;
    const weeks = [];
    for (let day = from; day <= to; day += 7) {
      const lastDay = Math.min(day + 6, to);
      weeks.push({
        label: dateLabel.formatRange(day * DAY_SECONDS * 1000, lastDay * DAY_SECONDS * 1000),
        paperSheets: paperSheets(day, lastDay),
        paperKg: sum(totals.paper, day, lastDay),
        briquettes: sum(totals.briquettes, day, lastDay)
      });
    }
    return {
      month: monthLabel.format(start),
      dateRange: dateLabel.formatRange(start, end),
      paperSheets: paperSheets(from, to),
      paperKg: sum(totals.paper, from, to),
      briquettes: sum(totals.briquettes, from, to),
      weeks
    };
  }

  return {
    recordsAvailable: records != null || batchRecords != null,
    sheetsPerBatch: SHEETS_PER_BATCH,
    thisMonth: period(calendar.current, calendar.today),
    lastMonth: period(calendar.previous, calendar.current - 1)
  };
}

async function getReportsWithMonths(store, now = new Date()) {
  const calendar = reportCalendar(now);
  const [reports, records, batchRecords] = await Promise.all([
    store.getReports(),
    store.getMonthlyReportRecords?.(calendar),
    store.getMonthlyBatchRecords?.(calendar)
  ]);
  return { ...reports, monthly: buildMonthlyReports(calendar, records, batchRecords) };
}

module.exports = { getReportsWithMonths, reportCalendar, buildMonthlyReports, MANILA_OFFSET_SECONDS, DAY_SECONDS };
