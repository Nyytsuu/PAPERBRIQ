package com.paperbriq.app.data;

import org.json.JSONArray;
import org.json.JSONObject;

/** Sensor validity is independent of the Uno's armed state and TFT connection. */
public final class ChamberSensorStatus {
    private ChamberSensorStatus() {}

    public static String status(JSONObject control, JSONObject telemetry) {
        JSONObject device = control.optJSONObject("device");
        JSONObject checks = control.optJSONObject("machineChecks");
        if (device == null || !device.optBoolean("connected")) return "unknown";
        // Older deployed servers lack machineChecks. Display-only compatibility:
        // require recent, numeric ESP32 readings for both DHT measurements.
        if (checks == null) return hasFreshReadings(telemetry) ? "passed" : "unknown";
        // Use the server's remaining lifetime and a monotonic local clock.
        // A phone whose wall clock differs from Render must not hide valid data.
        long receivedAt = control.optLong("_receivedAtElapsedMs", -1);
        long remaining = Math.min(30000, checks.optLong("validForMs", 0));
        long age = android.os.SystemClock.elapsedRealtime() - receivedAt;
        if (receivedAt < 0 || remaining <= 0 || age < 0 || age >= remaining) return "unknown";
        JSONArray items = checks.optJSONArray("items");
        for (int i = 0; items != null && i < items.length(); i++) {
            JSONObject item = items.optJSONObject(i);
            if (item != null && "dht".equals(item.optString("id"))) return item.optString("status", "unknown");
        }
        return "unknown";
    }

    private static boolean hasFreshReadings(JSONObject telemetry) {
        JSONArray sensors = telemetry.optJSONArray("sensors");
        boolean temperature = false, humidity = false;
        for (int i = 0; sensors != null && i < sensors.length(); i++) {
            JSONObject sensor = sensors.optJSONObject(i);
            if (sensor == null) continue;
            String name = sensor.optString("name");
            boolean isTemperature = "Temperature".equalsIgnoreCase(name);
            if (!isTemperature && !"Humidity".equalsIgnoreCase(name)) continue;
            boolean fresh = SensorReadingFreshness.isFresh(telemetry.optString("generatedAt"),
                sensor.optString("updatedAt"), telemetry.optLong("_receivedAtElapsedMs", -1),
                android.os.SystemClock.elapsedRealtime(), sensor.optString("source"),
                sensor.optString("status"), sensor.optDouble("value", Double.NaN),
                isTemperature ? -40 : 0, isTemperature ? 80 : 100);
            if (isTemperature) temperature = fresh; else humidity = fresh;
        }
        return temperature && humidity;
    }

    public static boolean isReadingAvailable(JSONObject control, JSONObject telemetry) {
        return "passed".equals(status(control, telemetry));
    }
}
