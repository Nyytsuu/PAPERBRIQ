package com.paperbriq.app.screens;

import android.app.AlertDialog;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import com.paperbriq.app.AppColors;
import com.paperbriq.app.AppController;
import com.paperbriq.app.data.ChamberSensorStatus;
import com.paperbriq.app.ui.Ui;
import org.json.JSONArray;
import org.json.JSONObject;
import java.util.ArrayList;
import java.util.Locale;

public class IotMonitoringScreen implements AppScreen {
    @Override public String key() { return "iot"; }
    @Override public String navLabel() { return "Sensors"; }
    @Override public String title() { return "Sensors"; }
    @Override public String subtitle() { return "Latest readings and equipment status."; }

    @Override
    public void render(LinearLayout root, AppController controller) {
        Ui ui = controller.ui();
        JSONObject iot = controller.session().pagePayload("iot");
        JSONObject device = controller.session().pagePayload("deviceControl").optJSONObject("device");
        boolean online = device != null && device.optBoolean("connected", false);
        boolean databaseConfigured = !"not configured".equalsIgnoreCase(
            controller.session().pagePayload("backendHealth").optString("database", "not configured"));
        root.addView(ui.pill(online ? "Online" : "Offline", online ? AppColors.GREEN : AppColors.RED),
            ui.fullWidth(LinearLayout.LayoutParams.WRAP_CONTENT, 0, 0, 0, ui.dp(10)));
        String dhtStatus = ChamberSensorStatus.status(controller.session().pagePayload("deviceControl"), iot);
        String chamberMessage = "passed".equals(dhtStatus) ? "Drying chamber sensor is responding."
            : "attention".equals(dhtStatus) ? "Drying chamber sensor is not responding. Check its connection."
            : "Waiting for a fresh drying chamber reading.";
        root.addView(ui.text(chamberMessage, 14, "passed".equals(dhtStatus) ? AppColors.GREEN : AppColors.MUTED,
            android.graphics.Typeface.NORMAL), ui.fullWidth(-2, 0, 0, 0, ui.dp(10)));
        ui.addGrid(root, sensorCards(ui, iot.optJSONArray("sensors"), online, databaseConfigured, dhtStatus), 2);

        Button details = ui.outlineButton("Equipment details");
        details.setOnClickListener(view -> showEquipment(controller, iot, online));
        root.addView(details, ui.fullWidth(ui.dp(48), 0, 0, 0, ui.dp(8)));
        root.addView(ui.infoListCard("Alerts", alertRows(ui, iot.optJSONArray("alerts"))));
    }

    private View[] sensorCards(Ui ui, JSONArray sensors, boolean online, boolean databaseConfigured, String dhtStatus) {
        if (!online || sensors == null || sensors.length() == 0) {
            return new View[]{ui.text(online ? "Waiting for sensor readings." : "Readings will appear when PaperBriq reconnects.",
                14, AppColors.MUTED, android.graphics.Typeface.NORMAL)};
        }
        ArrayList<View> views = new ArrayList<>();
        for (int index = 0; index < sensors.length(); index++) {
            JSONObject sensor = sensors.optJSONObject(index);
            if (sensor == null) continue;
            String name = sensor.optString("name", "Sensor");
            if (isRetiredSensor(name)) continue;
            String label = ui.equipmentLabel(name);
            boolean waterLevel = "Water level".equals(label);
            boolean trusted = "esp32".equalsIgnoreCase(sensor.optString("source", "")) || databaseConfigured;
            boolean hasValue = trusted && sensor.has("value") && !sensor.isNull("value");
            boolean chamberSensor = "Temperature".equalsIgnoreCase(name) || "Humidity".equalsIgnoreCase(name);
            if (chamberSensor && !"passed".equals(dhtStatus)) hasValue = false;
            boolean paperSensor = "Shredder Inlet".equalsIgnoreCase(name) || "Shredder Outlet".equalsIgnoreCase(name);
            if ((paperSensor || waterLevel) && "Offline".equalsIgnoreCase(sensor.optString("status"))) hasValue = false;
            String value = hasValue ? ui.formatSensorValue(label, sensor.optDouble("value"), sensor.optString("unit", "")) : "--";
            if (hasValue && ("IR Sensor".equalsIgnoreCase(name) || paperSensor)) value = sensor.optDouble("value") > 0 ? "Detected" : "Clear";
            if (waterLevel && "--".equals(value)) hasValue = false;
            String status = hasValue ? sensor.optString("status", "") : "Waiting for reading";
            if (chamberSensor) status = hasValue ? "Drying chamber" : "attention".equals(dhtStatus) ? "Sensor not responding" : "Waiting for fresh reading";
            if (paperSensor && hasValue) status = "Shredder Inlet".equalsIgnoreCase(name) ? "Paper entering" : "Paper leaving";
            boolean attention = !hasValue || "Warning".equalsIgnoreCase(status) || (waterLevel && "Low".equals(value));
            if (waterLevel && hasValue) status = "";
            views.add(ui.readingCard(label, value.trim(), status,
                attention ? AppColors.RED : AppColors.GREEN));
        }
        return views.toArray(new View[0]);
    }

    private boolean isRetiredSensor(String name) {
        switch (name.trim().toLowerCase(Locale.ROOT)) {
            case "battery level":
            case "tds water quality":
            case "water quality":
            case "power output":
            case "power use":
            case "water flow rate":
            case "system pressure":
                return true;
            default:
                return false;
        }
    }

    private void showEquipment(AppController controller, JSONObject iot, boolean online) {
        Ui ui = controller.ui();
        ScrollView scroll = new ScrollView(controller.activity());
        LinearLayout content = ui.column();
        content.setPadding(ui.dp(16), ui.dp(12), ui.dp(16), ui.dp(12));
        scroll.addView(content, ui.matchWrap());
        content.addView(ui.simpleList(hardwareRows(ui, online ? iot.optJSONArray("hardware") : null)));
        JSONObject network = iot.optJSONObject("network");
        content.addView(ui.infoListCard("Connection", new String[][]{
            {"PaperBriq", online ? "Online" : "Offline"},
            {"Last update", network != null ? network.optString("lastDataSync", "--") : "--"}
        }));
        new AlertDialog.Builder(controller.activity()).setTitle("Equipment details")
            .setView(scroll).setPositiveButton("Close", null).show();
    }

    private String[][] alertRows(Ui ui, JSONArray alerts) {
        if (alerts == null) return new String[][]{{"Status", "Waiting for updates"}};
        if (alerts.length() == 0) return new String[][]{{"Status", "No alerts reported"}};
        String[][] rows = new String[alerts.length()][2];
        for (int index = 0; index < alerts.length(); index++) {
            JSONObject alert = alerts.optJSONObject(index);
            if (alert == null) alert = new JSONObject();
            String title = alert.optString("title", "Alert");
            boolean lowWater = "Rainwater Level Dropping".equalsIgnoreCase(title) || "Low Rainwater Level".equalsIgnoreCase(title);
            rows[index][0] = lowWater ? "Water level low" : ui.equipmentLabel(title);
            rows[index][1] = lowWater ? "Check the water tank." : ui.equipmentLabel(alert.optString("message", ""));
        }
        return rows;
    }

    private String[][] hardwareRows(Ui ui, JSONArray hardware) {
        if (hardware == null || hardware.length() == 0) return new String[][]{{"--", "Waiting for equipment readings", ""}};
        String[][] rows = new String[hardware.length()][4];
        for (int index = 0; index < hardware.length(); index++) {
            JSONObject item = hardware.optJSONObject(index);
            if (item == null) item = new JSONObject();
            rows[index][0] = "";
            rows[index][1] = ui.equipmentLabel(item.optString("name", "Equipment"));
            rows[index][2] = ui.equipmentLabel(item.optString("detail", ""));
            rows[index][3] = item.optString("status", "Waiting");
        }
        return rows;
    }
}
