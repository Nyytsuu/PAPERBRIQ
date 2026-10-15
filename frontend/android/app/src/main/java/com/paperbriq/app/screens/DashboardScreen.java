package com.paperbriq.app.screens;

import android.graphics.Color;
import android.graphics.Typeface;
import android.widget.LinearLayout;

import com.paperbriq.app.AppColors;
import com.paperbriq.app.AppController;
import com.paperbriq.app.ui.Ui;

import org.json.JSONArray;
import org.json.JSONObject;

public class DashboardScreen implements AppScreen {
    @Override
    public String key() {
        return "dashboard";
    }

    @Override
    public String navLabel() {
        return "Overview";
    }

    @Override
    public String title() {
        return "Live Overview";
    }

    @Override
    public String subtitle() {
        return "At-a-glance machine health, production progress, and resource levels.";
    }

    @Override
    public void render(LinearLayout root, AppController controller) {
        Ui ui = controller.ui();
        JSONObject dashboard = controller.session().pagePayload("dashboard");
        JSONObject summary = dashboard.optJSONObject("summary");
        JSONObject totals = summary != null ? summary.optJSONObject("totals") : controller.session().totals();
        if (totals == null) totals = new JSONObject();
        JSONObject systemStatus = dashboard.optJSONObject("systemStatus");
        if (systemStatus == null) systemStatus = new JSONObject();
        JSONObject weeklySummary = dashboard.optJSONObject("weeklySummary");
        if (weeklySummary == null) weeklySummary = new JSONObject();
        JSONArray processProgress = dashboard.optJSONArray("processProgress");
        JSONArray processLog = dashboard.optJSONArray("processLog");
        int overallProgress = dashboard.optInt("overallProgress", 0);

        LinearLayout status = ui.card(20);
        status.setPadding(ui.dp(18), ui.dp(18), ui.dp(18), ui.dp(18));
        status.setBackground(ui.round(AppColors.GREEN, ui.dp(20), Color.TRANSPARENT, 0));
        status.addView(ui.text("LIVE SYSTEM HEALTH", 12, Color.argb(220, 255, 255, 255), Typeface.BOLD));
        status.addView(ui.text(systemStatus.optString("status", "No machine data"), 34, Color.WHITE, Typeface.BOLD));
        status.addView(ui.text(systemStatus.optString("message", "Waiting for backend machine records."), 14, Color.argb(225, 255, 255, 255), Typeface.NORMAL));
        root.addView(status, ui.fullWidth(LinearLayout.LayoutParams.WRAP_CONTENT, 0, 0, 0, ui.dp(12)));

        ui.addGrid(root, new android.view.View[]{
            ui.statCard("Rainwater Level", ui.formatNumber(systemStatus.optDouble("rainwaterLevel", 0)) + "%", "From IoT sensor records", AppColors.BLUE),
            ui.statCard("Temperature", ui.formatNumber(systemStatus.optDouble("temperatureC", 0)) + " °C", "DHT22 live reading", AppColors.AMBER),
            ui.statCard("Humidity", ui.formatNumber(systemStatus.optDouble("humidityPercent", 0)) + "% RH", "DHT22 live reading", AppColors.BLUE),
            ui.statCard("Power Output", ui.formatNumber(systemStatus.optDouble("powerOutputWatts", 0)) + " w", "From IoT sensor records", AppColors.BLUE),
            ui.statCard("Briquettes", ui.formatNumber(totals.optInt("briquettes", 0)), "Production output", AppColors.GREEN)
        }, 2);

        root.addView(ui.sectionTitle("Process Progress", "Live"));
        root.addView(ui.simpleList(processRows(processProgress)));

        ui.addGrid(root, new android.view.View[]{
            ui.progressCard("Overall Progress", ui.formatNumber(overallProgress) + "%", dashboard.optString("displayTime", "Waiting for backend sync")),
            ui.infoListCard("Run Summary", new String[][]{
                {"Target", ui.formatNumber(weeklySummary.optInt("briquetteTarget", 0)) + " bricks"},
                {"Completed", ui.formatNumber(weeklySummary.optInt("briquettesProduced", 0)) + " bricks"},
                {"Water Saved", ui.formatNumber(weeklySummary.optInt("waterSavedL", 0)) + " L"}
            })
        }, 1);

        root.addView(ui.sectionTitle("Process Log", "View All"));
        root.addView(ui.simpleList(logRows(processLog)));

        root.addView(ui.sectionTitle("Weekly Summary", ""));
        ui.addGrid(root, new android.view.View[]{
            ui.statCard("Paper Waste Collected", ui.formatNumber(weeklySummary.optDouble("paperWasteKg", totals.optDouble("paperKg", 0))) + " kg", "From collection records", AppColors.GREEN),
            ui.statCard("Briquettes Produced", ui.formatNumber(weeklySummary.optInt("briquettesProduced", 0)) + " / " + ui.formatNumber(weeklySummary.optInt("briquetteTarget", 0)), "From process records", AppColors.AMBER),
            ui.statCard("Water Saved", ui.formatNumber(weeklySummary.optInt("waterSavedL", 0)) + " L", "Calculated from paper records", AppColors.BLUE),
            ui.statCard("Active Contributors", ui.formatNumber(weeklySummary.optInt("activeContributors", totals.optInt("contributors", 0))), "From contributor records", AppColors.GREEN)
        }, 2);
    }

    private String[][] processRows(JSONArray steps) {
        if (steps == null || steps.length() == 0) {
            return new String[][]{{"--", "No process records yet", "Waiting"}};
        }
        String[][] rows = new String[steps.length()][3];
        for (int index = 0; index < steps.length(); index++) {
            JSONObject step = steps.optJSONObject(index);
            if (step == null) step = new JSONObject();
            rows[index][0] = step.optString("status", "--");
            rows[index][1] = step.optString("step", step.optString("name", "Process step"));
            rows[index][2] = uiSafePercent(step.optDouble("progress", 0)) + " - " + step.optString("detail", "");
        }
        return rows;
    }

    private String[][] logRows(JSONArray logs) {
        if (logs == null || logs.length() == 0) {
            return new String[][]{{"--", "No process log records yet", "Waiting"}};
        }
        String[][] rows = new String[logs.length()][3];
        for (int index = 0; index < logs.length(); index++) {
            JSONObject log = logs.optJSONObject(index);
            if (log == null) log = new JSONObject();
            rows[index][0] = log.optString("time", "--");
            rows[index][1] = log.optString("event", "Process event");
            rows[index][2] = log.optString("status", log.optString("type", "--"));
        }
        return rows;
    }

    private String uiSafePercent(double value) {
        return String.valueOf(Math.round(value)) + "%";
    }
}
