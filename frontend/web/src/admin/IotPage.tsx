import React, { useState } from "react";
import { DataRow, NetworkTile, Panel, SensorLineChart } from "../components/ui";
import { formatSensorValue } from "../api/format";
import { connectLiveStream } from "../api/liveStream";

export default function IotPage() {
  const [iotData, setIotData] = useState(null);
  const [iotMessage, setIotMessage] = useState("Loading live telemetry...");

  React.useEffect(() => {
    let isMounted = true;
    let intervalId;
    let closeStream = () => {};

    async function loadIotData() {
      try {
        const response = await fetch("/api/iot");
        const result = await response.json();
        if (!response.ok) throw new Error(result.error || "Unable to load IoT telemetry.");
        if (!isMounted) return;
        setIotData(result);
        setIotMessage("");
      } catch (error) {
        if (!isMounted) return;
        setIotMessage(error.message);
      }
    }

    loadIotData();

    if (typeof EventSource === "undefined") {
      intervalId = setInterval(loadIotData, iotData?.nextPollMs || 4000);
    } else {
      closeStream = connectLiveStream(["iot"], {
        onOpen: () => {
          if (isMounted) setIotMessage("");
        },
        onSnapshot: (payload) => {
          if (!isMounted || !payload.iot) return;
          setIotData(payload.iot);
          setIotMessage("");
        },
        onError: (message) => {
          if (isMounted) setIotMessage(message);
        }
      });
    }

    return () => {
      isMounted = false;
      if (intervalId) clearInterval(intervalId);
      closeStream();
    };
  }, []);

  const sensors = iotData?.sensors || [];
  const devices = iotData?.devices || [];
  const alerts = iotData?.alerts || [];
  const diagnostics = iotData?.diagnostics || [];
  const network = iotData?.network || {};

  return (
    <section className="iot-page">
      <Panel
        title="IoT Monitoring"
        subtitle="Live machine telemetry from the PaperBriq station."
        action={<span className="status-pill">{iotData ? `Synced ${iotData.displayTime}` : "Connecting"}</span>}
      >
        {iotMessage && <p className="muted-copy iot-message">{iotMessage}</p>}

        <div className="iot-overview">
          <article className="machine-health-card">
            <span>Machine</span>
            <strong>{iotData?.machine?.state || "Connecting"}</strong>
            <p>{iotData?.machine?.cycle || "Waiting for telemetry..."}</p>
            <small>{iotData?.machine?.health || "Checking sensors"}</small>
          </article>

          <div className="network-strip">
            <NetworkTile label="Online Devices" value={network.onlineDevices || "--"} />
            <NetworkTile label="Packet Success" value={network.packetSuccess || "--"} />
            <NetworkTile label="Response Time" value={network.responseTimeMs ? `${network.responseTimeMs} ms` : "--"} />
            <NetworkTile label="Signal" value={network.signal || "--"} />
          </div>
        </div>

        <div className="sensor-grid live-sensor-grid">
          {sensors.map((sensor) => (
            <article key={sensor.name}>
              <div className="sensor-card-top">
                <span>{sensor.name}</span>
                <small className={sensor.status === "Warning" ? "warning" : ""}>{sensor.status}</small>
              </div>
              <strong>{formatSensorValue(sensor)}</strong>
              <SensorLineChart label={sensor.name} values={sensor.trend || []} />
              <p>{sensor.delta > 0 ? "+" : ""}{sensor.delta ?? 0} since last sample - {sensor.updatedAt}</p>
            </article>
          ))}
        </div>
      </Panel>

      <div className="iot-detail-grid">
        <section className="panel">
          <div className="section-title">
            <div>
              <h2>Device States</h2>
              <p>Actuators and safety locks.</p>
            </div>
          </div>
          <div className="data-list compact-list">
            {devices.map((device) => (
              <DataRow key={device.name} title={device.name} subtitle={`${device.value}${device.load ? ` - ${device.load} load` : ""}`} pill={device.status} />
            ))}
          </div>
        </section>

        <section className="panel">
          <div className="section-title">
            <div>
              <h2>Live Alerts</h2>
              <p>Current warnings and scheduled reminders.</p>
            </div>
          </div>
          <div className="data-list compact-list">
            {alerts.map((alert) => (
              <DataRow key={`${alert.title}-${alert.message}`} title={alert.title} subtitle={alert.message} pill={alert.severity} />
            ))}
          </div>
        </section>

        <section className="panel">
          <div className="section-title">
            <div>
              <h2>Diagnostics</h2>
              <p>Connectivity and controller health.</p>
            </div>
          </div>
          <div className="data-list compact-list">
            {diagnostics.map((diagnostic) => (
              <DataRow key={diagnostic.item} title={diagnostic.item} subtitle={`Checked ${diagnostic.checkedAt || iotData?.displayTime || "--"}`} pill={diagnostic.status} />
            ))}
          </div>
        </section>
      </div>
    </section>
  );
}
