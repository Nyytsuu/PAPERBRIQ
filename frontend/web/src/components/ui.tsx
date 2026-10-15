import React from "react";
import { formatNumber } from "../api/format";

export function NetworkTile({ label, value }) {
  return (
    <article>
      <span>{label}</span>
      <strong>{value}</strong>
    </article>
  );
}

export function Panel({ title, subtitle, action, children }) {
  return (
    <section className="panel">
      <div className="section-title">
        <div>
          <h2>{title}</h2>
          <p>{subtitle}</p>
        </div>
        {action}
      </div>
      {children}
    </section>
  );
}

export function ConfirmModal({
  title,
  message,
  confirmLabel = "Confirm",
  cancelLabel = "Cancel",
  tone = "default",
  onCancel,
  onConfirm
}) {
  return (
    <div className="modal-backdrop" role="presentation" onClick={onCancel}>
      <section
        className={`confirm-modal ${tone}`}
        role="dialog"
        aria-modal="true"
        aria-labelledby="confirmModalTitle"
        onClick={(event) => event.stopPropagation()}
      >
        <h2 id="confirmModalTitle">{title}</h2>
        <p>{message}</p>
        <div className="confirm-actions">
          <button className="secondary-button" type="button" onClick={onCancel}>
            {cancelLabel}
          </button>
          <button className="primary-button confirm-button" type="button" onClick={onConfirm}>
            {confirmLabel}
          </button>
        </div>
      </section>
    </div>
  );
}

export function Field({ label, ...props }) {
  return (
    <label className="field-group">
      <span>{label}</span>
      <span className="input-shell">
        <input {...props} />
      </span>
    </label>
  );
}

export function DataList({ items, empty, children }) {
  if (!items.length) return <p className="muted-copy">{empty}</p>;
  return <div className="data-list">{items.map((item, index) => <React.Fragment key={`${item.id || item.batchCode || item.barangay}-${index}`}>{children(item)}</React.Fragment>)}</div>;
}

export function DataRow({ title, subtitle, pill }) {
  return (
    <article className="data-row">
      <span className="row-mark">{String(title || "P").slice(0, 1).toUpperCase()}</span>
      <div>
        <strong>{title}</strong>
        <span>{subtitle}</span>
      </div>
      <span className="status-pill">{pill}</span>
    </article>
  );
}

export function MetricCard({ tone, icon, label, value, detail, spark = [] }) {
  const sparkValues = normalizeSpark(spark);

  return (
    <article className={`metric-card ${tone}`}>
      <div className="metric-top">
        <span>{label}</span>
        <b aria-hidden="true"><MetricIcon name={icon} /></b>
      </div>
      <strong>{value}</strong>
      <small>{detail}</small>
      <span className="metric-spark" aria-hidden="true">
        <LineGraph values={sparkValues} label={`${label} trend`} compact />
      </span>
    </article>
  );
}

export function StatusRow({ label, value, percent = 0, tone = "green" }) {
  return (
    <div>
      <span>
        <span>{label}</span>
        <strong>{value}</strong>
      </span>
      <i className={`status-bar ${tone}`} aria-hidden="true"><em style={{ width: `${Math.min(100, Math.max(0, percent))}%` }} /></i>
    </div>
  );
}

export function ReportItem({ label, value, width, percent, change, tone = "" }) {
  const widthValue = percent ?? Number.parseFloat(width || "0");
  const safePercent = Number.isFinite(Number(widthValue)) ? Number(widthValue) : 0;
  const barWidth = `${clamp(safePercent, 0, 100)}%`;

  return (
    <article>
      <span>{label}</span>
      <strong>{value}</strong>
      {change && <small className="report-change">{change}</small>}
      <svg className={`progress-graph ${tone}`} viewBox="0 0 100 12" preserveAspectRatio="none" role="img" aria-label={`${label} progress ${barWidth}`}>
        <rect className="progress-track" x="0" y="1" width="100" height="10" rx="5" />
        <rect className="progress-fill" x="0" y="1" width={String(clamp(safePercent, 0, 100))} height="10" rx="5" />
      </svg>
    </article>
  );
}

export function ReportBars({ label, values, tone = "" }) {
  const chartValues = normalizeChartValues(values);

  return (
    <article className="report-bars">
      <div>
        <strong>{label}</strong>
        <span>{chartValues.length ? `${formatNumber(chartValues.at(-1))} latest` : "Waiting for data"}</span>
      </div>
      <div className={`report-bar-row ${tone}`} aria-label={`${label} chart`}>
        <BarGraph values={chartValues} label={label} />
      </div>
    </article>
  );
}

export function SensorLineChart({ label, values }) {
  return (
    <div className="sensor-trend" aria-label={`${label} live trend`}>
      <LineGraph values={normalizeChartValues(values)} label={`${label} live trend`} />
    </div>
  );
}

function normalizeSpark(values) {
  const parsed = Array.isArray(values)
    ? values.map((value) => Number(value)).filter((value) => Number.isFinite(value))
    : [];

  if (parsed.length) return parsed.slice(-6);
  return [18, 34, 26, 44];
}

function normalizeChartValues(values) {
  const parsed = Array.isArray(values)
    ? values.map((value) => Number(value)).filter((value) => Number.isFinite(value))
    : [];

  return parsed.length ? parsed : [0, 0, 0, 0, 0, 0];
}

function BarGraph({ values, label }) {
  const chart = values.slice(-14);
  const max = Math.max(...chart, 1);
  const width = 320;
  const height = 120;
  const padding = 12;
  const gap = 7;
  const innerWidth = width - padding * 2;
  const innerHeight = height - padding * 2;
  const barWidth = Math.max(8, (innerWidth - gap * Math.max(0, chart.length - 1)) / Math.max(1, chart.length));

  return (
    <svg className="bar-chart" viewBox={`0 0 ${width} ${height}`} role="img" aria-label={`${label} live bar chart`}>
      <title>{label} live bar chart</title>
      <g className="chart-grid">
        {[0, 1, 2].map((line) => {
          const y = padding + (innerHeight / 2) * line;
          return <line key={`${label}-grid-${line}`} x1={padding} y1={y} x2={width - padding} y2={y} />;
        })}
      </g>
      {chart.map((value, index) => {
        const barHeight = Math.max(6, (Number(value) / max) * innerHeight);
        const x = padding + index * (barWidth + gap);
        const y = height - padding - barHeight;
        return (
          <rect
            key={`${label}-bar-${index}`}
            x={x}
            y={y}
            width={barWidth}
            height={barHeight}
            rx="4"
          />
        );
      })}
    </svg>
  );
}

function LineGraph({ values, label, compact = false }) {
  const chart = normalizeChartValues(values).slice(compact ? -6 : -16);
  const width = compact ? 150 : 320;
  const height = compact ? 54 : 96;
  const padding = compact ? 5 : 10;
  const points = buildLinePoints(chart, width, height, padding);
  const areaPath = buildAreaPath(points, height - padding);
  const lastPoint = points.at(-1) || { x: padding, y: height - padding };

  return (
    <svg className={`line-chart ${compact ? "compact-line-chart" : ""}`} viewBox={`0 0 ${width} ${height}`} role="img" aria-label={label}>
      <title>{label}</title>
      {!compact && (
        <g className="chart-grid">
          {[0, 1, 2].map((line) => {
            const y = padding + ((height - padding * 2) / 2) * line;
            return <line key={`${label}-grid-${line}`} x1={padding} y1={y} x2={width - padding} y2={y} />;
          })}
        </g>
      )}
      <path className="chart-fill" d={areaPath} />
      <polyline className="chart-line" points={points.map((point) => `${point.x},${point.y}`).join(" ")} />
      <circle className="chart-point" cx={lastPoint.x} cy={lastPoint.y} r={compact ? 3 : 4} />
    </svg>
  );
}

function buildLinePoints(values, width, height, padding) {
  const min = Math.min(...values);
  const max = Math.max(...values);
  const innerWidth = width - padding * 2;
  const innerHeight = height - padding * 2;
  const denominator = Math.max(1, values.length - 1);

  return values.map((value, index) => {
    const x = padding + (index / denominator) * innerWidth;
    const ratio = max === min ? 0.5 : (Number(value) - min) / (max - min);
    const y = height - padding - ratio * innerHeight;
    return {
      x: Number(x.toFixed(2)),
      y: Number(y.toFixed(2))
    };
  });
}

function buildAreaPath(points, bottomY) {
  if (!points.length) return "";
  const first = points[0];
  const last = points[points.length - 1];
  return [
    `M ${first.x} ${bottomY}`,
    ...points.map((point) => `L ${point.x} ${point.y}`),
    `L ${last.x} ${bottomY}`,
    "Z"
  ].join(" ");
}

function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

function MetricIcon({ name }) {
  const icons = {
    paper: (
      <>
        <path d="M7 4h7l3 3v13H7z" />
        <path d="M14 4v4h4" />
        <path d="M9 12h6" />
        <path d="M9 15h5" />
      </>
    ),
    briquette: (
      <>
        <path d="M4 9h16v8H4z" />
        <path d="M7 6h10v3H7z" />
        <path d="M7 17h10v3H7z" />
        <path d="M9 12h6" />
      </>
    ),
    users: (
      <>
        <path d="M9 11a3 3 0 1 0 0-6 3 3 0 0 0 0 6Z" />
        <path d="M3.5 19a5.5 5.5 0 0 1 11 0" />
        <path d="M16 11a2.5 2.5 0 1 0 0-5" />
        <path d="M15 15a5 5 0 0 1 5.5 4" />
      </>
    ),
    leaf: (
      <>
        <path d="M20 4c-8 0-14 5-14 12 0 2 1 4 3 5 7 0 12-6 11-17Z" />
        <path d="M6 20c3-6 7-9 13-13" />
      </>
    )
  };

  return (
    <svg className="metric-icon" viewBox="0 0 24 24" focusable="false">
      {icons[name] || icons.paper}
    </svg>
  );
}

export function ActionCard({ title, subtitle, onClick }) {
  return (
    <button className="action-card" type="button" onClick={onClick}>
      <strong>{title}</strong>
      <span>{subtitle}</span>
    </button>
  );
}
