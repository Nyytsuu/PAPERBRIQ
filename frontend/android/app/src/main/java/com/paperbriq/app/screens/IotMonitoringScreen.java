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

        root.addView(ui.sectionTitle("Real-time Sensor Data", "Live"));
        ui.addGrid(root, sensorCards(ui, iot.optJSONArray("sensors")), 2);

        root.addView(ui.sectionTitle("Device Status", ""));
        ui.addGrid(root, deviceCards(ui, iot.optJSONArray("devices")), 2);

        root.addView(ui.sectionTitle("Hardware Health", "Live checks"));
        root.addView(ui.simpleList(hardwareRows(iot.optJSONArray("hardware"))));

        ui.addGrid(root, new android.view.View[]{
            ui.infoListCard("System Alerts", alertRows(iot.optJSONArray("alerts"))),
            ui.infoListCard("Diagnostics", diagnosticRows(iot.optJSONArray("diagnostics")))
        }, 1);

        root.addView(ui.sectionTitle("Network Health", ""));
        ui.addGrid(root, new android.view.View[]{
            ui.statCard("Online Devices", network.optString("onlineDevices", "--"), "From device records", AppColors.GREEN),
            ui.statCard("Packet Success", network.optString("packetSuccess", "--"), "From diagnostics", AppColors.GREEN),
            ui.statCard("Response Time", network.has("responseTimeMs") && !network.isNull("responseTimeMs") ? network.optString("responseTimeMs") + " ms" : "--", "From network records", AppColors.GREEN),
            ui.statCard("Last Data Sync", network.optString("lastDataSync", "--"), "From sensor records", AppColors.GREEN)
        }, 2);
    }

    private android.view.View[] sensorCards(Ui ui, JSONArray sensors) {
        if (sensors == null || sensors.length() == 0) {
            return new android.view.View[]{ui.sensorCard("No sensors", "--", "Waiting", AppColors.GREEN)};
        }
        android.view.View[] views = new android.view.View[sensors.length()];
        for (int index = 0; index < sensors.length(); index++) {
            JSONObject sensor = sensors.optJSONObject(index);
            if (sensor == null) sensor = new JSONObject();
            String value = ui.formatNumber(sensor.optDouble("value", 0)) + " " + sensor.optString("unit", "");
            int color = sensor.optString("status", "").equalsIgnoreCase("Warning") ? AppColors.RED : AppColors.GREEN;
            views[index] = ui.sensorCard(sensor.optString("name", "Sensor"), value, sensor.optString("status", "--"), color);
        }
        return views;
    }

    private android.view.View[] deviceCards(Ui ui, JSONArray devices) {
        if (devices == null || devices.length() == 0) {
            return new android.view.View[]{ui.gaugeCard("No devices", "--", "Waiting")};
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
