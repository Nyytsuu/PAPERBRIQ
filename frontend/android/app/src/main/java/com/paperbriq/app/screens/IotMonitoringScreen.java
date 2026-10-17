package com.paperbriq.app.screens;

import android.widget.LinearLayout;

import com.paperbriq.app.AppColors;
import com.paperbriq.app.AppController;
import com.paperbriq.app.ui.Ui;

import org.json.JSONArray;
import org.json.JSONObject;

public class IotMonitoringScreen implements AppScreen {
    @Override
    public String key() {
        return "iot";
    }

    @Override
    public String navLabel() {
        return "Live Telemetry";
    }

    @Override
    public String title() {
        return "Live Telemetry";
    }

    @Override
    public String subtitle() {
        return "Real-time sensor data and device status.";
    }

    @Override
    public void render(LinearLayout root, AppController controller) {
        Ui ui = controller.ui();
        JSONObject iot = controller.session().pagePayload("iot");
        JSONObject network = iot.optJSONObject("network");
        if (network == null) network = new JSONObject();
        JSONObject controlStatus = controller.session().pagePayload("deviceControl");
        JSONObject device = controlStatus.optJSONObject("device");
        boolean machineOnline = device != null && device.optBoolean("connected", false);
        JSONObject backendHealth = controller.session().pagePayload("backendHealth");
        boolean databaseConfigured = !"not configured".equalsIgnoreCase(backendHealth.optString("database", "not configured"));

        root.addView(ui.sectionTitle("Real-time Sensor Data", machineOnline ? "Live" : "ESP32 offline"));
        ui.addGrid(root, sensorCards(ui, iot.optJSONArray("sensors"), machineOnline, databaseConfigured), 2);

        root.addView(ui.sectionTitle("Device Status", ""));
        ui.addGrid(root, deviceCards(ui, iot.optJSONArray("devices"), machineOnline), 2);

        root.addView(ui.sectionTitle("Hardware Health", "Live checks"));
        root.addView(ui.simpleList(hardwareRows(machineOnline ? iot.optJSONArray("hardware") : null)));

        ui.addGrid(root, new android.view.View[]{
            ui.infoListCard("System Alerts", alertRows(iot.optJSONArray("alerts"))),
            ui.infoListCard("Diagnostics", diagnosticRows(iot.optJSONArray("diagnostics")))
        }, 1);

        root.addView(ui.sectionTitle("Network Health", ""));
        ui.addGrid(root, new android.view.View[]{
            ui.statCard("Online Devices", machineOnline ? network.optString("onlineDevices", "--") : "0 / 1", "From device records", AppColors.GREEN),
            ui.statCard("Packet Success", machineOnline ? network.optString("packetSuccess", "--") : "--", "From diagnostics", AppColors.GREEN),
            ui.statCard("Response Time", machineOnline && network.has("responseTimeMs") && !network.isNull("responseTimeMs") ? network.optString("responseTimeMs") + " ms" : "--", "From network records", AppColors.GREEN),
            ui.statCard("Last Data Sync", machineOnline ? network.optString("lastDataSync", "--") : "Never", "From sensor records", AppColors.GREEN)
        }, 2);
    }

    private android.view.View[] sensorCards(Ui ui, JSONArray sensors, boolean machineOnline, boolean databaseConfigured) {
        if (!machineOnline || sensors == null || sensors.length() == 0) {
            return new android.view.View[]{ui.sensorCard("ESP32", "--", "Offline", AppColors.RED)};
        }
        android.view.View[] views = new android.view.View[sensors.length()];
        for (int index = 0; index < sensors.length(); index++) {
            JSONObject sensor = sensors.optJSONObject(index);
            if (sensor == null) sensor = new JSONObject();
            boolean trustedSource = "esp32".equalsIgnoreCase(sensor.optString("source", "")) || databaseConfigured;
            boolean hasValue = trustedSource && sensor.has("value") && !sensor.isNull("value");
            String value = hasValue ? ui.formatNumber(sensor.optDouble("value")) + " " + sensor.optString("unit", "") : "--";
            String status = hasValue ? sensor.optString("status", "--") : "No live data";
            int color = status.equalsIgnoreCase("Warning") || !hasValue ? AppColors.RED : AppColors.GREEN;
            views[index] = ui.sensorCard(sensor.optString("name", "Sensor"), value.trim(), status, color);
        }
        return views;
    }

    private android.view.View[] deviceCards(Ui ui, JSONArray devices, boolean machineOnline) {
        if (!machineOnline || devices == null || devices.length() == 0) {
            return new android.view.View[]{ui.gaugeCard("ESP32", "OFFLINE", "Waiting for telemetry")};
        }
        android.view.View[] views = new android.view.View[devices.length()];
        for (int index = 0; index < devices.length(); index++) {
            JSONObject device = devices.optJSONObject(index);
            if (device == null) device = new JSONObject();
            views[index] = ui.gaugeCard(device.optString("name", "Device"), device.optString("value", "--"), device.optString("status", "--"));
        }
        return views;
    }

    private String[][] alertRows(JSONArray alerts) {
        if (alerts == null || alerts.length() == 0) {
            return new String[][]{{"--", "No system alerts recorded"}};
        }
        String[][] rows = new String[alerts.length()][2];
        for (int index = 0; index < alerts.length(); index++) {
            JSONObject alert = alerts.optJSONObject(index);
            if (alert == null) alert = new JSONObject();
            rows[index][0] = alert.optString("title", "Alert");
            rows[index][1] = alert.optString("severity", "--") + " - " + alert.optString("message", "");
        }
        return rows;
    }

    private String[][] hardwareRows(JSONArray hardware) {
        if (hardware == null || hardware.length() == 0) {
            return new String[][]{{"--", "No hardware health data", "Waiting", "No data"}};
        }
        String[][] rows = new String[hardware.length()][4];
        for (int index = 0; index < hardware.length(); index++) {
            JSONObject item = hardware.optJSONObject(index);
            if (item == null) item = new JSONObject();
            rows[index][0] = item.optString("checkedAt", "--");
            rows[index][1] = item.optString("name", "Hardware");
            rows[index][2] = item.optString("detail", "Live device check");
            rows[index][3] = item.optString("status", "No data");
        }
        return rows;
    }

    private String[][] diagnosticRows(JSONArray diagnostics) {
        if (diagnostics == null || diagnostics.length() == 0) {
            return new String[][]{{"--", "No diagnostics recorded"}};
        }
        String[][] rows = new String[diagnostics.length()][2];
        for (int index = 0; index < diagnostics.length(); index++) {
            JSONObject item = diagnostics.optJSONObject(index);
            if (item == null) item = new JSONObject();
            rows[index][0] = item.optString("item", "Diagnostic");
            rows[index][1] = item.optString("status", "--");
        }
        return rows;
    }
}
