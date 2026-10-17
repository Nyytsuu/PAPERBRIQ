const SENSOR_PROFILES = {
  Temperature: { base: 29.4, amplitude: 2.1, decimals: 1, low: 26, high: 32 },
  Humidity: { base: 65, amplitude: 7, decimals: 0, low: 45, high: 78 },
  "Rainwater Level": { base: 78, amplitude: 4, decimals: 0, low: 70, high: 100 },
  "Battery Level": { base: 92, amplitude: 3, decimals: 0, low: 25, high: 100 },
  "Power Output": { base: 240, amplitude: 34, decimals: 0, low: 120, high: 320 },
  "Water Flow Rate": { base: 18.6, amplitude: 2.8, decimals: 1, low: 8, high: 26 },
  "System Pressure": { base: 2.3, amplitude: 0.34, decimals: 1, low: 1.2, high: 3.3 },
  "TDS Water Quality": { base: 152, amplitude: 18, decimals: 0, low: 60, high: 210 }
};

function createLiveIotSnapshot(baseIot) {
  const now = new Date();
  const sensors = (baseIot.sensors || []).map((sensor) => buildLiveSensor(sensor, now));
  const connected = sensors.some((sensor) => sensor.source === "esp32" && sensor.status !== "Offline");
  const rainwater = sensors.find((sensor) => sensor.name === "Rainwater Level");
  const battery = sensors.find((sensor) => sensor.name === "Battery Level");
  const latestTelemetryAt = latestSensorTimestamp(sensors);

  return {
    ...baseIot,
    generatedAt: now.toISOString(),
    displayTime: formatManilaTime(now),
    freshnessSeconds: latestTelemetryAt ? Math.max(0, Math.round((now.getTime() - latestTelemetryAt.getTime()) / 1000)) : null,
    nextPollMs: 4000,
    machine: {
      id: "PBQ-MCH-001",
      name: "PaperBriq Station 01",
      state: connected ? "Online" : "Offline",
      barangay: "San Isidro",
      cycle: connected ? "Receiving ESP32 telemetry" : "Waiting for ESP32 telemetry",
      health: connected ? getMachineHealth(sensors) : "No live device data"
    },
    sensors,
    devices: buildLiveDevices(baseIot.devices || [], connected),
    alerts: buildLiveAlerts(baseIot.alerts || [], rainwater, battery, connected),
    diagnostics: (baseIot.diagnostics || []).map((diagnostic) => ({
      ...diagnostic,
      status: connected ? diagnostic.status : "Offline",
      checkedAt: formatManilaTime(now)
    })),
    hardware: buildHardwareHealth(sensors, baseIot.diagnostics || [], now, connected),
    network: {
      ...(baseIot.network || {}),
      onlineDevices: connected ? "1 / 1" : "0 / 1",
      packetSuccess: connected ? "Receiving" : "--",
      responseTimeMs: null,
      lastDataSync: latestTelemetryAt ? formatManilaTime(latestTelemetryAt) : "Never",
      transport: "HTTPS telemetry",
      signal: connected ? "Connected" : "Offline"
    }
  };
}

function buildHardwareHealth(sensors, diagnostics, now, connected) {
  const statusFor = (name) => sensors.find((sensor) => sensor.name === name)?.status || "No data";
  const isHealthy = (status) => /normal|good|ok|online/i.test(status);
  const temperatureStatus = statusFor("Temperature");
  const humidityStatus = statusFor("Humidity");
  const controllerStatus = diagnostics.find((item) => /edge controller/i.test(item.item || ""))?.status || "No data";
  const sensorLinkStatus = diagnostics.find((item) => /sensor connectivity/i.test(item.item || ""))?.status || "No data";
  const checkedAt = formatManilaTime(now);

  if (!connected) {
    return [
      { name: "DHT22", detail: "Temperature and humidity sensor", status: "Offline", checkedAt },
      { name: "IR Sensor", detail: "Object detection sensor", status: "Offline", checkedAt },
      { name: "TFT Display", detail: "Local monitoring display", status: "Offline", checkedAt }
    ];
  }

  return [
    {
      name: "DHT22",
      detail: "Temperature and humidity sensor",
      status: isHealthy(temperatureStatus) && isHealthy(humidityStatus) ? "Online" : "Attention",
      checkedAt
    },
    {
      name: "IR Sensor",
      detail: "Object detection sensor",
      status: isHealthy(sensorLinkStatus) ? "Online" : "Attention",
      checkedAt
    },
    {
      name: "TFT Display",
      detail: "Local monitoring display",
      status: isHealthy(controllerStatus) ? "Online" : "Attention",
      checkedAt
    }
  ];
}

function buildLiveSensor(sensor, now) {
  if (sensor.source === "esp32" && isFreshTelemetry(sensor.updatedAt, now)) {
    return {
      ...sensor,
      trend: [...(sensor.trend || []).slice(-11), Number(sensor.value)],
      delta: 0,
      updatedAt: sensor.updatedAt
    };
  }

  return {
    ...sensor,
    value: null,
    status: "Offline",
    trend: [],
    delta: null,
    updatedAt: sensor.source === "esp32" ? sensor.updatedAt : "Never"
  };
}

function buildTrend(profile, seed, index) {
  return Array.from({ length: 12 }, (_, pointIndex) => {
    const trendSeed = seed - (11 - pointIndex) * 8;
    return Number((profile.base + wave(trendSeed, 0.42 + index * 0.05, index) * profile.amplitude).toFixed(profile.decimals));
  });
}

function buildLiveDevices(devices, connected) {
  return devices.map((device) => ({
    ...device,
    value: "--",
    status: connected ? "No actuator telemetry" : "Offline",
    load: null
  }));
}

function buildLiveAlerts(alerts, rainwater, battery, connected) {
  if (!connected) {
    return [{
      title: "ESP32 Offline",
      severity: "Warning",
      message: "No recent telemetry has been received from the PaperBriq ESP32."
    }];
  }
  const liveAlerts = [...alerts];
  if (rainwater && rainwater.value !== null && rainwater.value < 76) {
    liveAlerts.unshift({
      title: "Rainwater Level Dropping",
      severity: "Warning",
      message: `Rainwater tank is at ${rainwater.value}%. Prepare refill or harvesting check.`
    });
  }
  if (battery && battery.value !== null && battery.value < 90) {
    liveAlerts.unshift({
      title: "Battery Below Target",
      severity: "Info",
      message: `Battery is at ${battery.value}%. Solar charging remains active.`
    });
  }
  return liveAlerts.slice(0, 5);
}

function isFreshTelemetry(value, now) {
  if (!value) return false;
  const timestamp = new Date(value);
  if (Number.isNaN(timestamp.getTime())) return false;
  const configured = Number(process.env.IOT_TELEMETRY_ONLINE_SECONDS || 20);
  const onlineSeconds = Number.isFinite(configured) ? Math.min(600, Math.max(10, configured)) : 20;
  return now.getTime() - timestamp.getTime() <= onlineSeconds * 1000;
}

function latestSensorTimestamp(sensors) {
  const timestamps = sensors
    .filter((sensor) => sensor.source === "esp32" && sensor.updatedAt)
    .map((sensor) => new Date(sensor.updatedAt))
    .filter((date) => !Number.isNaN(date.getTime()));
  if (!timestamps.length) return null;
  return new Date(Math.max(...timestamps.map((date) => date.getTime())));
}

function getSensorStatus(name, value, profile) {
  if (value < profile.low || value > profile.high) return "Warning";
  if (name === "Battery Level" && value >= 88) return "Good";
  if (name === "TDS Water Quality" && value <= 180) return "Good";
  return "Normal";
}

function getMachineHealth(sensors) {
  return sensors.some((sensor) => sensor.status === "Warning") ? "Needs attention" : "All systems normal";
}

function wave(seed, speed, offset) {
  return Math.sin(seed * speed + offset * 1.73);
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
  createLiveIotSnapshot
};
