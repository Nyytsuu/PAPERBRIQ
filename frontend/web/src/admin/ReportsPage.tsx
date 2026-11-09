import React, { useState } from "react";
import { Panel } from "../components/ui";
import { formatNumber } from "../api/format";
import { connectLiveStream } from "../api/liveStream";

export default function ReportsPage() {
  const [reports, setReports] = useState(null);
  const [selectedMonth, setSelectedMonth] = useState("thisMonth");
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

  const period = reports?.monthly?.[selectedMonth];
  const paperTotal = period?.paperKg;
  const briquetteTotal = period?.briquettes;

  return (
    <section className="reports-page">
      <Panel
        title={period?.month || "Production summary"}
        subtitle={period?.dateRange}
        action={<span className="status-pill">{reports ? `Synced ${reports.displayTime}` : "Connecting"}</span>}
      >
        {reportMessage && <p className="muted-copy iot-message">{reportMessage}</p>}
        <div className="report-periods" role="group" aria-label="Reporting period">
          {Object.entries({ thisMonth: "This month", lastMonth: "Last month" }).map(([key, label]) => (
            <button key={key} type="button"
              className={selectedMonth === key ? "primary-button" : "secondary-button"}
              aria-pressed={selectedMonth === key} onClick={() => setSelectedMonth(key)}>
              {label}
            </button>
          ))}
        </div>
        <div className="report-grid">
          <article>
            <span>Paper Collected</span>
            <strong>{paperTotal == null ? "—" : `${formatNumber(paperTotal)} kg`}</strong>
          </article>
          <article>
            <span>Briquettes Produced</span>
            <strong>{briquetteTotal == null ? "—" : `${formatNumber(briquetteTotal)} pcs`}</strong>
          </article>
        </div>
      </Panel>
      <Panel title="Weekly breakdown">
        {reports?.monthly?.recordsAvailable === false && <p className="muted-copy">Recorded data is unavailable.</p>}
        {period && (
          <table className="report-week-table">
            <thead><tr><th scope="col">Dates</th><th scope="col">Paper (kg)</th><th scope="col">Briquettes</th></tr></thead>
            <tbody>
              {period.weeks.map(({ label, paperKg, briquettes }) => (
                <tr key={label}>
                  <th scope="row">{label}</th>
                  {paperKg == null && briquettes == null ? <td colSpan={2}>No records</td> : (
                    <>
                      <td>{paperKg == null ? "—" : formatNumber(paperKg)}</td>
                      <td>{briquettes == null ? "—" : formatNumber(briquettes)}</td>
                    </>
                  )}
                </tr>
              ))}
            </tbody>
          </table>
        )}
        <p className="muted-copy">Paper used · Bond paper, 20 lb (about 75 gsm)</p>
      </Panel>
    </section>
  );
}
