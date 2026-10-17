package com.paperbriq.app.screens;

import android.graphics.Color;
import android.graphics.Typeface;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

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
        return "Live progress, guarded software controls, and machine events.";
    }

    @Override
    public void render(LinearLayout root, AppController controller) {
        Ui ui = controller.ui();
        JSONObject payload = controller.session().pagePayload("process");
        JSONObject activeBatch = payload.optJSONObject("activeBatch");
        JSONArray steps = payload.optJSONArray("steps");
        JSONArray logs = payload.optJSONArray("logs");
        JSONArray qualityChecks = payload.optJSONArray("qualityChecks");

        if (controller.canControlMachine()) {
            addMachineControls(root, controller, ui);
        }

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

    private void addMachineControls(LinearLayout root, AppController controller, Ui ui) {
        JSONObject status = controller.session().pagePayload("deviceControl");
        boolean enabled = status.optBoolean("enabled", false);
        JSONObject device = status.optJSONObject("device");
        boolean online = device != null
            ? device.optBoolean("connected", false)
            : status.optBoolean("deviceOnline", status.optBoolean("online", false));
        String processState = status.optString("processState", status.optString("state", online ? "Ready" : "Waiting for device"));
        String error = status.optString("error", "");
        JSONArray commands = status.optJSONArray("commands");
        JSONObject latestCommand = commands != null && commands.length() > 0 ? commands.optJSONObject(0) : null;
        String latestStatus = latestCommand != null ? latestCommand.optString("status", "") : "";
        boolean commandActive = "queued".equals(latestStatus) || "delivered".equals(latestStatus);
        boolean startableState = "idle".equalsIgnoreCase(processState) ||
            "ready".equalsIgnoreCase(processState) ||
            "stopped".equalsIgnoreCase(processState) ||
            "complete".equalsIgnoreCase(processState) ||
            "completed".equalsIgnoreCase(processState);

        root.addView(ui.sectionTitle("Machine Controls", online ? "Device online" : "Device not confirmed"));
        LinearLayout card = ui.card(18);
        card.setPadding(ui.dp(16), ui.dp(16), ui.dp(16), ui.dp(16));
        card.addView(ui.text("Remote operator controls", 17, AppColors.INK, Typeface.BOLD));
        card.addView(ui.text(
            "These commands use Wi-Fi and are not an emergency stop. Keep the physical power-cut emergency stop within reach.",
            13,
            AppColors.MUTED,
            Typeface.NORMAL
        ), ui.fullWidth(LinearLayout.LayoutParams.WRAP_CONTENT, 0, ui.dp(6), 0, ui.dp(12)));

        TextView statusText = ui.text(
            enabled ? "Control enabled • " + processState : "Control disabled on the backend" + (error.isEmpty() ? "" : " • " + error),
            13,
            enabled ? AppColors.GREEN_DARK : AppColors.RED,
            Typeface.BOLD
        );
        statusText.setGravity(Gravity.START);
        card.addView(statusText, ui.fullWidth(LinearLayout.LayoutParams.WRAP_CONTENT, 0, 0, 0, ui.dp(12)));

        Button start = ui.greenButton("Start process");
        start.setEnabled(enabled && online && !commandActive && startableState);
        start.setOnClickListener(view -> controller.confirmAction(
            "Start PaperBriq process?",
            "Start only after checking the guards, gates, molder, and physical emergency stop beside the machine.",
            () -> controller.sendDeviceCommand("start")
        ));
        card.addView(start, ui.fullWidth(ui.dp(52), 0, 0, 0, ui.dp(8)));

        Button stop = ui.greenButton("Software Stop");
        stop.setBackground(ui.round(AppColors.RED, ui.dp(14), Color.TRANSPARENT, 0));
        stop.setEnabled(enabled && online);
        stop.setOnClickListener(view -> controller.sendDeviceCommand("stop"));
        card.addView(stop, ui.fullWidth(ui.dp(52), 0, 0, 0, ui.dp(8)));

        Button reset = ui.outlineButton("Reset software fault");
        reset.setEnabled(enabled && online && !commandActive && ("fault".equalsIgnoreCase(processState) || "stopped".equalsIgnoreCase(processState)));
        reset.setOnClickListener(view -> controller.confirmAction(
            "Reset software fault?",
            "Reset clears only a stopped software fault. It does not reset the physical emergency stop and must not start movement.",
            () -> controller.sendDeviceCommand("reset")
        ));
        card.addView(reset, ui.fullWidth(ui.dp(52), 0, 0, 0, ui.dp(8)));

        Button key = ui.outlineButton(controller.hasControlKey() ? "Change control key" : "Enter control key");
        key.setOnClickListener(view -> controller.showControlKeyDialog(null));
        card.addView(key, ui.fullWidth(ui.dp(48), 0, 0, 0, 0));
        root.addView(card);
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
