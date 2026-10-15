import React, { useState } from "react";
import { DataList, DataRow, Panel, ReportBars, ReportItem } from "../components/ui";
import { formatNumber } from "../api/format";
import { connectLiveStream } from "../api/liveStream";

export default function ReportsPage() {
  const [reports, setReports] = useState(null);
  const [reportMessage, setReportMessage] = useState("Loading live reports...");

  React.useEffect(() => {
    let isMounted = true;
    let intervalId;
    let closeStream = () => {};

    async function loadReports() {
      try {
        const response = await fetch("/api/reports");
        const result = await response.json();
        if (!response.ok) throw new Error(result.error || "Unable to load reports.");
        if (!isMounted) return;
        setReports(result);
        setReportMessage("");
      } catch (error) {
        if (!isMounted) return;
        setReportMessage(error.message);
      }
    }

    loadReports();

    if (typeof EventSource === "undefined") {
      intervalId = window.setInterval(loadReports, 7000);
    } else {
      closeStream = connectLiveStream(["reports"], {
        onOpen: () => {
          if (isMounted) setReportMessage("");
        },
        onSnapshot: (payload) => {
          if (!isMounted || !payload.reports) return;
          setReports(payload.reports);
          setReportMessage("");
        },
        onError: (message) => {
          if (isMounted) setReportMessage(message);
        }
      });
    }

    return () => {
      isMounted = false;
      if (intervalId) window.clearInterval(intervalId);
      closeStream();
    };
  }, []);

  const cards = reports?.cards || [];
  const charts = reports?.charts || {};
  const chartMeta = reports?.chartMeta || {};
  const topBarangays = reports?.topBarangays || [];
  const impact = reports?.impact || [];

  return (
    <section className="reports-page">
      <Panel
        title="Reports"
        subtitle={reports?.period || "Monthly and barangay-level performance summary."}
        action={<span className="status-pill">{reports ? `Synced ${reports.displayTime}` : "Connecting"}</span>}
      >
        {reportMessage && <p className="muted-copy iot-message">{reportMessage}</p>}
        <div className="report-grid">
          {cards.length ? cards.map((card, index) => (
            <ReportItem
              key={card.label}
              label={card.label}
              value={card.value}
              percent={card.percent}
              change={card.change}
              tone={index === 2 ? "blue-bar" : index === 1 ? "amber-bar" : ""}
            />
          )) : (
            <>
              <ReportItem label="Paper Collected" value="--" width="12%" />
              <ReportItem label="Water Saved" value="--" width="12%" tone="blue-bar" />
              <ReportItem label="Energy Generated" value="--" width="12%" tone="amber-bar" />
            </>
          )}
        </div>
      </Panel>

      <div className="reports-layout">
        <Panel title="Output Charts" subtitle={`Backend-fed trend values - ${chartMeta.source || "connecting"}.`}>
          <ReportBars label="Paper Collected" values={charts.paperCollected || []} />
          <ReportBars label="Briquettes Produced" values={charts.briquettesProduced || []} tone="amber-bar" />
          <ReportBars label="Water Saved" values={charts.waterSaved || []} tone="blue-bar" />
        </Panel>

        <Panel title="Top Barangays" subtitle="Ranked by paper collected.">
          <DataList items={topBarangays} empty="No barangay report data yet.">
            {(item) => (
              <DataRow
                title={item.barangay}
                subtitle={`${formatNumber(item.paperKg)} kg paper - ${formatNumber(item.contributors)} contributors`}
                pill="Live"
              />
            )}
          </DataList>
        </Panel>
      </div>

      <Panel title="Environmental Impact" subtitle={reports?.summary?.note || "Impact generated from report totals."}>
        <div className="impact-grid">
          {impact.map((item) => (
            <article key={item.label}>
              <span>{item.label}</span>
              <strong>{item.value}</strong>
            </article>
          ))}
        </div>
      </Panel>
    </section>
  );
}
