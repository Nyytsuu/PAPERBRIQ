export function formatSensorValue(sensor) {
  if (!sensor) return "--";
  return `${formatNumber(sensor.value)} ${sensor.unit || ""}`.trim();
}

export function formatNumber(value) {
  return new Intl.NumberFormat("en-PH", {
    maximumFractionDigits: Number(value) % 1 === 0 ? 0 : 2
  }).format(Number(value) || 0);
}
