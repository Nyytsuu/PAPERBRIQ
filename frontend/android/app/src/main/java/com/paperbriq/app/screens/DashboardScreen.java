package com.paperbriq.app.screens;

import android.graphics.Typeface;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.view.Gravity;
import android.view.View;
import android.widget.LinearLayout;
import com.paperbriq.app.AppColors;
import com.paperbriq.app.AppController;
import com.paperbriq.app.data.ChamberSensorStatus;
import com.paperbriq.app.ui.Ui;
import com.paperbriq.app.ui.PaperIconView;
import org.json.JSONArray;
import org.json.JSONObject;

public class DashboardScreen implements AppScreen {
    @Override public String key() { return "dashboard"; }
    @Override public String navLabel() { return "Home"; }
    @Override public String title() { return "Overview"; }
    @Override public String subtitle() { return "Machine status and latest readings."; }

    @Override
    public void render(LinearLayout root, AppController controller) {
        Ui ui = controller.ui();
        JSONObject dashboard = controller.session().pagePayload("dashboard");
        JSONObject summary = dashboard.optJSONObject("summary");
        JSONObject totals = summary != null ? summary.optJSONObject("totals") : controller.session().totals();
        if (totals == null) totals = new JSONObject();
        JSONObject control = controller.session().pagePayload("deviceControl");
        JSONObject device = control.optJSONObject("device");
        boolean online = device != null && device.optBoolean("connected", false);
        boolean databaseConfigured = !"not configured".equalsIgnoreCase(
            controller.session().pagePayload("backendHealth").optString("database", "not configured"));
        JSONArray sensors = controller.session().pagePayload("iot").optJSONArray("sensors");
        boolean dhtReady = online && ChamberSensorStatus.isReadingAvailable(control, controller.session().pagePayload("iot"));

        LinearLayout status = ui.card(22);
        GradientDrawable gradient = new GradientDrawable(GradientDrawable.Orientation.TL_BR,
            new int[]{AppColors.INK, AppColors.GREEN_DARK});
        gradient.setCornerRadius(ui.dp(22));
        status.setBackground(gradient);
        status.setPadding(ui.dp(16), ui.dp(16), ui.dp(16), ui.dp(14));
        LinearLayout statusHeading = ui.row();
        statusHeading.setGravity(Gravity.CENTER_VERTICAL);
        View dot = new View(controller.activity());
        dot.setBackground(ui.round(online ? 0xFFC4EA7B : 0xFFFFC6A8, ui.dp(5), Color.TRANSPARENT, 0));
        LinearLayout.LayoutParams dotParams = new LinearLayout.LayoutParams(ui.dp(8), ui.dp(8));
        dotParams.setMargins(0, 0, ui.dp(8), 0);
        statusHeading.addView(dot, dotParams);
        statusHeading.addView(ui.text(online ? "PaperBriq online" : "PaperBriq offline", 18,
            Color.WHITE, Typeface.BOLD), new LinearLayout.LayoutParams(0, -2, 1));
        statusHeading.addView(new PaperIconView(controller.activity(), "iot", 0xFFCAE4D2),
            new LinearLayout.LayoutParams(ui.dp(24), ui.dp(24)));
        status.addView(statusHeading);
        status.addView(ui.text(online
            ? ui.machineStateLabel(control.optString("processState", "idle"))
            : "Check the machine's power and Wi-Fi connection.", 14, 0xFFE2EEE5, Typeface.NORMAL),
            ui.fullWidth(-2, 0, ui.dp(5), 0, ui.dp(10)));
        LinearLayout statusAction = ui.row();
        statusAction.setGravity(Gravity.CENTER_VERTICAL);
        statusAction.addView(ui.text("Open machine controls", 12, Color.WHITE, Typeface.BOLD),
            new LinearLayout.LayoutParams(0, -2, 1));
        statusAction.addView(new PaperIconView(controller.activity(), "arrow", Color.WHITE),
            new LinearLayout.LayoutParams(ui.dp(20), ui.dp(20)));
        status.addView(statusAction);
        ui.interactive(status, (online ? "PaperBriq online. " : "PaperBriq offline. ") + "Open Process and machine controls",
            () -> controller.showPage("process"), 22);
        root.addView(status, ui.fullWidth(LinearLayout.LayoutParams.WRAP_CONTENT, 0, 0, 0, ui.dp(14)));

        ui.addGrid(root, new View[]{
            shortcut(controller, "Water level", sensorValue(ui, sensors, "Rainwater Level", online, databaseConfigured), AppColors.BLUE, "iot"),
            shortcut(controller, "Chamber temperature", sensorValue(ui, sensors, "Temperature", dhtReady, databaseConfigured), 0xFFAC5A29, "iot"),
            shortcut(controller, "Chamber humidity", sensorValue(ui, sensors, "Humidity", dhtReady, databaseConfigured), 0xFF427E96, "iot"),
            shortcut(controller, "Briquettes made", databaseConfigured ? ui.formatNumber(totals.optInt("briquettes", 0)) : "--", AppColors.GREEN_DARK, "reports")
        }, 2);
        JSONObject processRun = control.optJSONObject("processRun");
        String processLabel = processRun == null ? "Waiting for step reports"
            : processRun.optString("label", "Waiting for a forming run");
        if (!online && processRun != null && processRun.optBoolean("online", false)) {
            processLabel = "Offline — last report: " + processLabel;
        }
        LinearLayout progress = ui.card(20);
        progress.setPadding(ui.dp(14), ui.dp(14), ui.dp(14), ui.dp(14));
        LinearLayout progressHeading = ui.row();
        progressHeading.setGravity(Gravity.CENTER_VERTICAL);
        progressHeading.addView(ui.iconBadge("process", AppColors.GREEN_DARK, 34), new LinearLayout.LayoutParams(ui.dp(34), ui.dp(34)));
        android.widget.TextView title = ui.text("Forming progress", 14, AppColors.INK, Typeface.BOLD);
        title.setPadding(ui.dp(8), 0, 0, 0);
        progressHeading.addView(title, new LinearLayout.LayoutParams(0, -2, 1));
        progressHeading.addView(new PaperIconView(controller.activity(), "arrow", AppColors.GREEN_DARK), new LinearLayout.LayoutParams(ui.dp(20), ui.dp(20)));
        progress.addView(progressHeading);
        LinearLayout progressCopy = ui.row();
        progressCopy.setGravity(Gravity.CENTER_VERTICAL);
        progressCopy.addView(ui.text(processLabel, 17, AppColors.GREEN_DARK, Typeface.BOLD),
            new LinearLayout.LayoutParams(0, -2, 1));
        progressCopy.addView(ui.text("View steps", 12, AppColors.MUTED, Typeface.NORMAL));
        progress.addView(progressCopy, ui.fullWidth(-2, 0, ui.dp(8), 0, ui.dp(6)));
        progress.addView(ui.text("Drying is not enabled.", 12, AppColors.MUTED, Typeface.NORMAL));
        ui.interactive(progress, processLabel + ". View process steps",
            () -> controller.showPage("process"), 20);
        root.addView(progress);
        String updated = dashboard.optString("displayTime", "");
        if (!updated.isEmpty()) root.addView(ui.text("Updated " + updated, 12, AppColors.MUTED, Typeface.NORMAL),
            ui.fullWidth(LinearLayout.LayoutParams.WRAP_CONTENT, 0, ui.dp(10), 0, 0));
    }

    private View shortcut(AppController controller, String title, String value, int accent, String destination) {
        Ui ui = controller.ui();
        LinearLayout card = (LinearLayout) ui.readingCard(title, value, "", accent);
        // A visible arrow communicates that the reading is also a shortcut.
        View badge = card.getChildAt(0);
        card.removeView(badge);
        LinearLayout heading = ui.row();
        heading.setGravity(Gravity.CENTER_VERTICAL);
        heading.addView(badge, new LinearLayout.LayoutParams(ui.dp(34), ui.dp(34)));
        heading.addView(new View(controller.activity()), new LinearLayout.LayoutParams(0, 1, 1));
        heading.addView(new PaperIconView(controller.activity(), "arrow", AppColors.MUTED), new LinearLayout.LayoutParams(ui.dp(17), ui.dp(17)));
        card.addView(heading, 0, ui.fullWidth(-2, 0, 0, 0, ui.dp(6)));
        ui.interactive(card, title + ", " + value + ". Open " + ("iot".equals(destination) ? "Sensors" : "Reports"),
            () -> controller.showPage(destination), 20);
        return card;
    }

    private String sensorValue(Ui ui, JSONArray sensors, String name, boolean online, boolean databaseConfigured) {
        if (!online || sensors == null) return "--";
        for (int index = 0; index < sensors.length(); index++) {
            JSONObject sensor = sensors.optJSONObject(index);
            if (sensor == null || !name.equalsIgnoreCase(sensor.optString("name", ""))) continue;
            boolean trustedSource = "esp32".equalsIgnoreCase(sensor.optString("source", "")) || databaseConfigured;
            if (!trustedSource || !sensor.has("value") || sensor.isNull("value")) return "--";
            return ui.formatSensorValue(name, sensor.optDouble("value"), sensor.optString("unit", ""));
        }
        return "--";
    }
}
