import { useOutletContext } from "react-router-dom";
import { DataList, DataRow, MetricCard, Panel, StatusRow } from "../components/ui";
import { formatNumber } from "../api/format";

export default function OverviewPage(props = {}) {
  const outlet = useOutletContext() || {};
  const { summary, dashboard, dashboardMessage, onSectionChange } = { ...outlet, ...props };
  const totals = summary?.totals || {};
  const systemStatus = dashboard?.systemStatus || {};
  const processProgress = dashboard?.processProgress || [];
  const paperKg = Number(totals.paperKg || 0);
  const briquettes = Number(totals.briquettes || 0);
  const contributors = Number(totals.contributors || 0);
  const co2KgAvoided = Number(totals.co2KgAvoided || 0);
  const progress = Number(dashboard?.overallProgress ?? 0);
  const rainwaterLevel = Number(systemStatus.rainwaterLevel ?? 0);
  const powerOutputWatts = Number(systemStatus.powerOutputWatts ?? 0);
  const collectionSpark = reverseValues((summary?.collections || []).map((item) => item.paperWeightKg));
  const batchSpark = reverseValues((summary?.batches || []).map((item) => item.briquettesProduced));
  const contributorSpark = contributors ? [contributors] : [];
  const co2Spark = collectionSpark.length ? collectionSpark.map((value) => Number((Number(value) * 0.82).toFixed(2))) : [];

  return (
    <section className="overview-section">
      {dashboardMessage && <p className="form-message left">{dashboardMessage}</p>}

      <div className="metric-grid">
        <MetricCard tone="green" icon="paper" label="Paper Collected" value={`${formatNumber(paperKg)} kg`} detail={summary?.displayTime ? `Updated ${summary.displayTime}` : "This month"} spark={collectionSpark} />
        <MetricCard tone="amber" icon="briquette" label="Briquettes Made" value={formatNumber(briquettes)} detail="Production output" spark={batchSpark} />
        <MetricCard tone="blue" icon="users" label="Contributors" value={formatNumber(contributors)} detail="Registered users" spark={contributorSpark} />
        <MetricCard tone="slate" icon="leaf" label="CO2 Avoided" value={`${formatNumber(co2KgAvoided)} kg`} detail="Estimated impact" spark={co2Spark} />
      </div>

      <div className="insight-strip">
        <div>
          <span>{dashboard?.displayTime ? `Live sync ${dashboard.displayTime}` : "Today's operation focus"}</span>
          <strong>{systemStatus.message || "Keep paper sorting steady, monitor water level, and review queued collections."}</strong>
        </div>
        <button className="outline-button" type="button" onClick={() => onSectionChange("iot")}>View machine</button>
      </div>

      <div className="dashboard-grid">
        <Panel
          title="Collections"
          subtitle="Recent paper collection events."
          action={<button className="text-button" type="button" onClick={() => onSectionChange("reports")}>View reports</button>}
        >
          <DataList items={summary?.collections || []} empty="No collection records yet.">
            {(item) => (
              <DataRow
                title={item.barangay}
                subtitle={`${formatNumber(item.paperWeightKg)} kg paper - ${item.collectedAt}`}
                pill={item.status}
              />
            )}
          </DataList>
        </Panel>

        <Panel title="System Status" subtitle="Live operating summary.">
          <div className="status-stack">
            <StatusRow label="Overall Progress" value={`${progress}%`} percent={progress} />
            <StatusRow label="Rainwater Level" value={`${formatNumber(rainwaterLevel)}%`} percent={rainwaterLevel} tone="blue" />
            <StatusRow label="Battery Level" value={`${formatNumber(systemStatus.batteryLevel ?? 0)}%`} percent={Number(systemStatus.batteryLevel ?? 0)} />
            <StatusRow label="Power Output" value={`${formatNumber(powerOutputWatts)} W`} percent={Math.min(100, Math.round(powerOutputWatts / 3))} tone="amber" />
          </div>
        </Panel>

        <Panel title="Briquette Batches" subtitle="Latest production batches.">
          <DataList items={summary?.batches || []} empty="No briquette batches yet.">
            {(item) => (
              <DataRow
                title={item.batchCode}
                subtitle={`${formatNumber(item.briquettesProduced)} briquettes produced`}
                pill={item.qualityStatus}
              />
            )}
          </DataList>
        </Panel>

        <Panel title="Process Snapshot" subtitle="Live process steps from backend.">
          <DataList items={processProgress.slice(0, 4)} empty="No process updates yet.">
            {(item) => (
              <DataRow
                title={item.step || item.name}
                subtitle={`${formatNumber(item.progress)}% - ${item.detail || "Waiting for update"}`}
                pill={item.status}
              />
            )}
          </DataList>
        </Panel>
      </div>
    </section>
  );
}

function reverseValues(values) {
  return values
    .map((value) => Number(value))
    .filter((value) => Number.isFinite(value))
    .reverse();
}
