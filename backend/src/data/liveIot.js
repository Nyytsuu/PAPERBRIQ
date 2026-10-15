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
  const seed = Math.floor(now.getTime() / 1000);
  const sensors = (baseIot.sensors || []).map((sensor, index) => buildLiveSensor(sensor, seed, index));
  const rainwater = sensors.find((sensor) => sensor.name === "Rainwater Level");
  const battery = sensors.find((sensor) => sensor.name === "Battery Level");
  const packetSuccess = 99.1 + wave(seed, 0.5, 9) * 0.7;
  const responseTimeMs = Math.round(285 + wave(seed, 0.62, 12) * 55);

  return {
    ...baseIot,
    generatedAt: now.toISOString(),
    displayTime: formatManilaTime(now),
    freshnessSeconds: 0,
    nextPollMs: 4000,
    machine: {
      id: "PBQ-MCH-001",
      name: "PaperBriq Station 01",
      state: "Running",
      barangay: "San Isidro",
      cycle: "Pulping and mixing",
      health: getMachineHealth(sensors)
    },
    sensors,
    devices: buildLiveDevices(baseIot.devices || [], seed),
    alerts: buildLiveAlerts(baseIot.alerts || [], rainwater, battery),
    diagnostics: (baseIot.diagnostics || []).map((diagnostic) => ({
      ...diagnostic,
      checkedAt: formatManilaTime(now)
    })),
    hardware: buildHardwareHealth(sensors, baseIot.diagnostics || [], now),
    network: {
      ...(baseIot.network || {}),
      onlineDevices: "12 / 12",
      packetSuccess: `${packetSuccess.toFixed(1)}%`,
      responseTimeMs,
      lastDataSync: formatManilaTime(now),
      transport: "MQTT bridge",
      signal: "Strong"
    }
  };
}

function buildHardwareHealth(sensors, diagnostics, now) {
  const statusFor = (name) => sensors.find((sensor) => sensor.name === name)?.status || "No data";
  const isHealthy = (status) => /normal|good|ok|online/i.test(status);
  const temperatureStatus = statusFor("Temperature");
  const humidityStatus = statusFor("Humidity");
  const controllerStatus = diagnostics.find((item) => /edge controller/i.test(item.item || ""))?.status || "No data";
  const sensorLinkStatus = diagnostics.find((item) => /sensor connectivity/i.test(item.item || ""))?.status || "No data";
  const checkedAt = formatManilaTime(now);

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

function buildLiveSensor(sensor, seed, index) {
  if (sensor.source === "esp32") {
    return {
      ...sensor,
      trend: [...(sensor.trend || []).slice(-11), Number(sensor.value)],
      delta: 0,
      updatedAt: sensor.updatedAt || formatManilaTime(new Date())
    };
  }
  const profile = SENSOR_PROFILES[sensor.name] || {
    base: Number(sensor.value) || 0,
    amplitude: Math.max(1, Number(sensor.value) * 0.06),
    decimals: 1,
    low: Number.NEGATIVE_INFINITY,
    high: Number.POSITIVE_INFINITY
  };
  const rawValue = profile.base + wave(seed, 0.42 + index * 0.05, index) * profile.amplitude;
  const value = Number(rawValue.toFixed(profile.decimals));
  const previousValue = Number((profile.base + wave(seed - 12, 0.42 + index * 0.05, index) * profile.amplitude).toFixed(profile.decimals));
  const delta = Number((value - previousValue).toFixed(profile.decimals));

  return {
    ...sensor,
    value,
    status: getSensorStatus(sensor.name, value, profile),
    trend: buildTrend(profile, seed, index),
    delta,
    updatedAt: formatManilaTime(new Date())
  };
}

function buildTrend(profile, seed, index) {
  return Array.from({ length: 12 }, (_, pointIndex) => {
    const trendSeed = seed - (11 - pointIndex) * 8;
    return Number((profile.base + wave(trendSeed, 0.42 + index * 0.05, index) * profile.amplitude).toFixed(profile.decimals));
  });
}

function buildLiveDevices(devices, seed) {
  return devices.map((device, index) => {
    if (device.name === "Heater Status") {
      const heaterOn = wave(seed, 0.2, index) > 0.58;
      return { ...device, value: heaterOn ? "ON" : "OFF", status: heaterOn ? "Heating" : "Standby" };
    }
    return {
      ...device,
      status: device.status === "Secure" ? "Secure" : "Running",
      load: `${Math.round(62 + wave(seed, 0.36, index) * 18)}%`
    };
  });
}

function buildLiveAlerts(alerts, rainwater, battery) {
  const liveAlerts = [...alerts];
  if (rainwater && rainwater.value < 76) {
    liveAlerts.unshift({
      title: "Rainwater Level Dropping",
      severity: "Warning",
      message: `Rainwater tank is at ${rainwater.value}%. Prepare refill or harvesting check.`
    });
  }
  if (battery && battery.value < 90) {
    liveAlerts.unshift({
      title: "Battery Below Target",
      severity: "Info",
      message: `Battery is at ${battery.value}%. Solar charging remains active.`
    });
  }
  return liveAlerts.slice(0, 5);
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
