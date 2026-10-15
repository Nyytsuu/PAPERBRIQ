package com.paperbriq.app.screens;

import android.widget.LinearLayout;

import com.paperbriq.app.AppColors;
import com.paperbriq.app.AppController;
import com.paperbriq.app.ui.Ui;

import org.json.JSONArray;
import org.json.JSONObject;

public class ProcessTrackingScreen implements AppScreen {
    @Override
    public String key() {
        return "process";
    }

    @Override
    public String navLabel() {
        return "Process Monitor";
    }

    @Override
    public String title() {
        return "Process Tracking";
    }

    @Override
    public String subtitle() {
        return "Read-only progress from pulping through briquette drying.";
    }

    @Override
    public void render(LinearLayout root, AppController controller) {
        Ui ui = controller.ui();
        JSONObject payload = controller.session().pagePayload("process");
        JSONObject activeBatch = payload.optJSONObject("activeBatch");
        JSONArray steps = payload.optJSONArray("steps");
        JSONArray logs = payload.optJSONArray("logs");
        JSONArray qualityChecks = payload.optJSONArray("qualityChecks");

        root.addView(ui.sectionTitle("Active Batch", activeBatch != null ? activeBatch.optString("batchCode", "No active batch") : "No active batch"));
        root.addView(ui.infoListCard("Batch Details", batchRows(activeBatch, ui)));

        root.addView(ui.sectionTitle("Process Steps", ""));
        root.addView(ui.simpleList(stepRows(steps, ui)));

        root.addView(ui.sectionTitle("Quality Checks", ""));
        root.addView(ui.infoListCard("Quality Checklist", qualityRows(qualityChecks)));

        root.addView(ui.sectionTitle("Process Log", "Live"));
        root.addView(ui.simpleList(logRows(logs)));

        ui.addGrid(root, new android.view.View[]{
            ui.statCard("Estimated Time", activeBatch != null ? activeBatch.optString("estimatedTimeRemaining", "--") : "--", "Remaining", AppColors.GREEN),
            ui.statCard("Total to Produce", ui.formatNumber(activeBatch != null ? activeBatch.optDouble("targetBriquettes", 0) : 0) + " bricks", "Target output", AppColors.AMBER),
            ui.statCard("Completed", ui.formatNumber(activeBatch != null ? activeBatch.optDouble("completedBriquettes", 0) : 0) + " bricks", "Current output", AppColors.GREEN)
        }, 1);
    }

    private String[][] batchRows(JSONObject activeBatch, Ui ui) {
        if (activeBatch == null) {
            return new String[][]{{"Status", "No process run in MySQL yet"}};
        }
        return new String[][]{
            {"Status", activeBatch.optString("status", "--")},
            {"Overall Progress", ui.formatNumber(activeBatch.optDouble("overallProgress", 0)) + "%"},
            {"Target Briquettes", ui.formatNumber(activeBatch.optDouble("targetBriquettes", 0))},
            {"Completed Briquettes", ui.formatNumber(activeBatch.optDouble("completedBriquettes", 0))}
        };
    }

    private String[][] stepRows(JSONArray steps, Ui ui) {
        if (steps == null || steps.length() == 0) {
            return new String[][]{{"--", "No process steps in MySQL yet", "Waiting", "Empty"}};
        }
        String[][] rows = new String[steps.length()][4];
        for (int index = 0; index < steps.length(); index++) {
            JSONObject step = steps.optJSONObject(index);
            if (step == null) step = new JSONObject();
            rows[index][0] = String.valueOf(index + 1);
            rows[index][1] = step.optString("name", step.optString("step", "Process step"));
            rows[index][2] = ui.formatNumber(step.optDouble("progress", 0)) + "% - " + step.optString("detail", "");
            rows[index][3] = step.optString("status", "--");
        }
        return rows;
    }

    private String[][] qualityRows(JSONArray qualityChecks) {
        if (qualityChecks == null || qualityChecks.length() == 0) {
            return new String[][]{{"Checks", "No quality checks in MySQL yet"}};
        }
        String[][] rows = new String[qualityChecks.length()][2];
        for (int index = 0; index < qualityChecks.length(); index++) {
            JSONObject item = qualityChecks.optJSONObject(index);
            if (item == null) item = new JSONObject();
            rows[index][0] = item.optString("label", "Check");
            rows[index][1] = item.optString("value", "--");
        }
        return rows;
    }

    private String[][] logRows(JSONArray logs) {
        if (logs == null || logs.length() == 0) {
            return new String[][]{{"--", "No process logs in MySQL yet", "Waiting", "Empty"}};
        }
        String[][] rows = new String[logs.length()][4];
        for (int index = 0; index < logs.length(); index++) {
            JSONObject item = logs.optJSONObject(index);
            if (item == null) item = new JSONObject();
            rows[index][0] = item.optString("time", "--");
            rows[index][1] = item.optString("event", "Process event");
            rows[index][2] = "Recorded from process log table";
            rows[index][3] = item.optString("status", "--");
        }
        return rows;
    }
}
