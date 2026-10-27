package com.paperbriq.app.screens;

import android.app.AlertDialog;
import android.graphics.Color;
import android.graphics.Typeface;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
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
        return "Process";
    }

    @Override
    public String title() {
        return "Process";
    }

    @Override
    public String subtitle() {
        return "Progress and machine controls.";
    }

    @Override
    public void render(LinearLayout root, AppController controller) {
        Ui ui = controller.ui();
        JSONObject payload = controller.session().pagePayload("process");
        JSONObject activeBatch = payload.optJSONObject("activeBatch");
        JSONObject processRun = controller.session().pagePayload("deviceControl").optJSONObject("processRun");
        JSONArray steps = processRun == null ? null : processRun.optJSONArray("steps");
        JSONArray qualityChecks = payload.optJSONArray("qualityChecks");

        if (controller.canControlMachine()) {
            addMachineControls(root, controller, ui);
        }

        root.addView(ui.sectionTitle("Forming steps", ""));
        root.addView(ui.text(processRun == null ? "Waiting for controller step reports"
            : processRun.optString("label", "Waiting for a forming run"),
            16, AppColors.INK, Typeface.BOLD), ui.fullWidth(-2, 0, 0, 0, ui.dp(6)));
        root.addView(ui.text("These are reported controller steps, not proof that a motor or gate moved.",
            12, AppColors.MUTED, Typeface.NORMAL), ui.fullWidth(-2, 0, 0, 0, ui.dp(10)));
        Button viewSteps = ui.outlineButton("View forming steps");
        viewSteps.setOnClickListener(view -> {
            ScrollView scroll = new ScrollView(controller.activity());
            LinearLayout content = ui.column();
            content.setPadding(ui.dp(16), ui.dp(12), ui.dp(16), ui.dp(12));
            content.addView(ui.simpleList(stepRows(steps, ui)));
            scroll.addView(content, ui.matchWrap());
            new AlertDialog.Builder(controller.activity()).setTitle("Forming steps")
                .setView(scroll).setPositiveButton("Close", null).show();
        });
        root.addView(viewSteps, ui.fullWidth(ui.dp(48), 0, 0, 0, ui.dp(10)));

        JSONObject checks = controller.session().pagePayload("deviceControl").optJSONObject("machineChecks");
        JSONObject chamber = checks == null ? null : checks.optJSONObject("chamber");
        root.addView(ui.infoListCard("Drying chamber", new String[][]{
            {"Drying", "Not enabled"},
            {"Fan", reportedBoolean(chamber, "fanOn", "On command", "Off command")},
            {"Unlock power", reportedBoolean(chamber, "unlockOn", "On", "Off")},
            {"Door contact", reportedBoolean(chamber, "doorClosed", "Closed", "Open")}
        }));

        Button details = ui.outlineButton("Batch details and quality checks");
        details.setOnClickListener(view -> {
            ScrollView scroll = new ScrollView(controller.activity());
            LinearLayout content = ui.column();
            content.setPadding(ui.dp(16), ui.dp(12), ui.dp(16), ui.dp(12));
            content.addView(ui.infoListCard("Batch details", batchRows(activeBatch, ui)));
            content.addView(ui.infoListCard("Quality checks", qualityRows(qualityChecks)));
            scroll.addView(content, ui.matchWrap());
            new AlertDialog.Builder(controller.activity()).setTitle("Batch details")
                .setView(scroll).setPositiveButton("Close", null).show();
        });
        root.addView(details, ui.fullWidth(ui.dp(48), 0, ui.dp(10), 0, 0));
    }

    private String reportedBoolean(JSONObject report, String key, String yes, String no) {
        return report == null || !report.has(key) || report.isNull(key)
            ? "Not reported" : report.optBoolean(key) ? yes : no;
    }

    private void addMachineControls(LinearLayout root, AppController controller, Ui ui) {
        JSONObject status = controller.session().pagePayload("deviceControl");
        boolean enabled = status.optBoolean("enabled", false);
        JSONObject device = status.optJSONObject("device");
        boolean online = device != null
            ? device.optBoolean("connected", false)
            : status.optBoolean("deviceOnline", status.optBoolean("online", false));
        String processState = status.optString("processState", status.optString("state", online ? "Ready" : "Waiting for device"));
        JSONArray commands = status.optJSONArray("commands");
        boolean commandActive = false;
        for (int i = 0; commands != null && i < commands.length(); i++) {
            JSONObject command = commands.optJSONObject(i);
            String commandStatus = command == null ? "" : command.optString("status");
            if ("queued".equals(commandStatus) || "delivered".equals(commandStatus)) commandActive = true;
        }
        boolean startableState = "idle".equalsIgnoreCase(processState) ||
            "ready".equalsIgnoreCase(processState) ||
            "stopped".equalsIgnoreCase(processState) ||
            "complete".equalsIgnoreCase(processState) ||
            "completed".equalsIgnoreCase(processState);
        boolean startAvailable = enabled && online && !commandActive && startableState;
        boolean checksPassed = controller.automaticMachineChecksReady();

        root.addView(ui.pill(online ? "PaperBriq online" : "PaperBriq offline", online ? AppColors.GREEN : AppColors.RED),
            ui.fullWidth(LinearLayout.LayoutParams.WRAP_CONTENT, 0, 0, 0, ui.dp(10)));
        LinearLayout card = ui.card(18);
        card.setPadding(ui.dp(16), ui.dp(16), ui.dp(16), ui.dp(16));
        card.addView(ui.text(
            "App controls use Wi-Fi. For an emergency, use the machine's physical emergency stop.",
            13,
            AppColors.MUTED,
            Typeface.NORMAL
        ), ui.fullWidth(LinearLayout.LayoutParams.WRAP_CONTENT, 0, ui.dp(6), 0, ui.dp(12)));

        TextView statusText = ui.text(
            enabled ? ui.machineStateLabel(processState) : "Machine controls unavailable",
            13,
            enabled ? AppColors.GREEN_DARK : AppColors.RED,
            Typeface.BOLD
        );
        statusText.setGravity(Gravity.START);
        card.addView(statusText, ui.fullWidth(LinearLayout.LayoutParams.WRAP_CONTENT, 0, 0, 0, ui.dp(12)));

        Button start = ui.greenButton("Start forming");
        start.setEnabled(startAvailable && checksPassed);
        start.setOnClickListener(view -> controller.confirmAction(
            "Start forming briquettes?",
            "This runs the forming sequence only. Drying is not enabled. Confirm the machine works, the door is closed, and the area is clear. A completed component test does not prove physical movement.",
            () -> controller.sendDeviceCommand("start")
        ));
        Button stop = ui.greenButton("Stop");
        stop.setBackground(ui.round(AppColors.RED, ui.dp(14), Color.TRANSPARENT, 0));
        stop.setEnabled(enabled && online);
        stop.setOnClickListener(view -> controller.sendDeviceCommand("stop"));
        Button reset = ui.outlineButton("Reset");
        reset.setEnabled(enabled && online && !commandActive && ("fault".equalsIgnoreCase(processState) || "stopped".equalsIgnoreCase(processState)));
        reset.setOnClickListener(view -> controller.confirmAction(
            "Reset software fault?",
            "Reset clears only a stopped software fault. It does not reset the physical emergency stop and must not start movement.",
            () -> controller.sendDeviceCommand("reset")
        ));
        for (Button action : new Button[]{start, stop, reset}) {
            action.setAlpha(action.isEnabled() ? 1f : 0.45f);
            action.setMinWidth(0);
            action.setMinimumWidth(0);
            action.setMinHeight(ui.dp(48));
            action.setPadding(ui.dp(4), ui.dp(8), ui.dp(4), ui.dp(8));
        }
        ui.addGrid(card, new android.view.View[]{start, stop, reset}, 3);

        Button autoCheck = ui.greenButton("Check machine");
        autoCheck.setOnClickListener(view -> showFunctionalChecks(controller, ui));
        card.addView(autoCheck, ui.fullWidth(ui.dp(48), 0, 0, 0, ui.dp(6)));
        card.addView(ui.text("Run each component briefly and follow its checklist. Use an empty machine and remain beside the emergency stop.",
            12, AppColors.MUTED, Typeface.NORMAL),
            ui.fullWidth(LinearLayout.LayoutParams.WRAP_CONTENT, 0, 0, 0, ui.dp(8)));

        if (!controller.hasControlKey()) {
            Button key = ui.outlineButton("Set up machine access");
            key.setOnClickListener(view -> controller.showControlKeyDialog(null));
            card.addView(key, ui.fullWidth(ui.dp(48), 0, ui.dp(8), 0, 0));
        }
        root.addView(card);
    }

    private void showFunctionalChecks(AppController controller, Ui ui) {
        ScrollView scroll = new ScrollView(controller.activity());
        LinearLayout content = ui.column();
        content.setPadding(ui.dp(16), ui.dp(12), ui.dp(16), ui.dp(12));
        TextView summary = ui.text("Waiting for test reports", 16, AppColors.INK, Typeface.BOLD);
        content.addView(summary);
        content.addView(ui.text("A tick means the timed run completed. Watch each part to confirm it moved correctly. Heater testing is unavailable.",
            13, AppColors.MUTED, Typeface.NORMAL), ui.fullWidth(-2, 0, ui.dp(8), 0, ui.dp(12)));
        LinearLayout rows = ui.column();
        content.addView(rows);
        Button connections = ui.outlineButton("Check connections and sensors");
        connections.setOnClickListener(v -> controller.checkMachine(() -> showMachineCheckResults(controller, ui)));
        content.addView(connections, ui.fullWidth(ui.dp(48), 0, ui.dp(12), 0, 0));
        scroll.addView(content, ui.matchWrap());
        AlertDialog dialog = new AlertDialog.Builder(controller.activity()).setTitle("Check machine")
            .setView(scroll).setNegativeButton("Close", null)
            .setNeutralButton("Stop tests", (v, which) -> controller.sendDeviceCommand("stop"))
            .setPositiveButton("Run tests", null).create();
        Handler handler = new Handler(Looper.getMainLooper());
        Runnable refresh = new Runnable() {
            @Override public void run() {
                if (!dialog.isShowing()) return;
                JSONObject status = controller.session().pagePayload("deviceControl");
                JSONObject test = status.optJSONObject("componentTests");
                JSONObject health = status.optJSONObject("machineChecks");
                String state = test == null ? "waiting" : test.optString("state", "waiting");
                summary.setText("completed".equals(state) ? "Test runs completed — confirm movement"
                    : "interrupted".equals(state) ? "Test interrupted"
                    : "running".equals(state) ? "Testing components…"
                    : "queued".equals(state) ? "Waiting for the machine to start"
                    : "missing_reports".equals(state) ? "Some completion reports are missing"
                    : "unknown".equals(state) ? "Connection lost — results unconfirmed"
                    : "Close the door and use Reset before testing");
                rows.removeAllViews();
                JSONArray items = test == null ? null : test.optJSONArray("items");
                if (items == null) {
                    rows.addView(ui.text("Update the server and both machine boards to enable component tests.", 13, AppColors.MUTED, Typeface.NORMAL));
                } else for (int i = 0; i < items.length(); i++) {
                    JSONObject item = items.optJSONObject(i);
                    if (item == null) continue;
                    boolean done = item.optBoolean("checked", false);
                    String itemState = item.optString("status", "waiting");
                    String label = done ? "✓ Run completed" : "testing".equals(itemState) ? "Testing…"
                        : "interrupted".equals(itemState) ? "Interrupted" : "unknown".equals(itemState) ? "Unconfirmed" : "Waiting";
                    rows.addView(ui.text(item.optString("label") + " — " + label, 14,
                        done ? AppColors.GREEN_DARK : AppColors.INK, Typeface.BOLD), ui.fullWidth(-2, 0, 0, 0, ui.dp(10)));
                }
                boolean available = status.optBoolean("enabled", false) && health != null
                    && health.optBoolean("componentTestsSupported", false) && health.optBoolean("passed", false)
                    && health.optLong("validForMs", 0) > 0;
                dialog.getButton(AlertDialog.BUTTON_POSITIVE).setEnabled(available);
                handler.postDelayed(this, 1000);
            }
        };
        dialog.setOnDismissListener(v -> handler.removeCallbacks(refresh));
        dialog.setOnShowListener(v -> {
            dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(clicked -> controller.confirmAction(
                "Run all component tests?",
                "The shredder, mixer, gates, presser, pusher, fan, lights, buzzer and lock will operate in sequence. Confirm the machine is unloaded, the door is closed, and you will stay beside the physical emergency stop. Timed completion cannot detect a jam or disconnected actuator.",
                () -> controller.checkMachine(() -> {
                    if (controller.automaticMachineChecksReady()) controller.sendDeviceCommand("self_test");
                    else showMachineCheckResults(controller, ui);
                })));
            refresh.run();
        });
        dialog.show();
    }

    private void showMachineCheckResults(AppController controller, Ui ui) {
        ScrollView scroll = new ScrollView(controller.activity());
        LinearLayout content = ui.column();
        content.setPadding(ui.dp(16), ui.dp(12), ui.dp(16), ui.dp(12));
        JSONObject result = controller.machineCheckResults();
        JSONArray items = result == null ? null : result.optJSONArray("items");
        content.addView(ui.text(controller.automaticMachineChecksReady()
                ? "Connection and sensor checks passed" : "Needs attention", 17,
            controller.automaticMachineChecksReady() ? AppColors.GREEN_DARK : AppColors.RED, Typeface.BOLD));
        content.addView(ui.text("These are the latest reported results. This check does not move or test-drive any part.",
            12, AppColors.MUTED, Typeface.NORMAL));
        if (items == null || items.length() == 0) {
            content.addView(ui.text("No check results available. Check your internet connection. The app, server, and both machine boards need the check update.",
                14, AppColors.INK, Typeface.NORMAL), ui.fullWidth(-2, 0, ui.dp(12), 0, 0));
        } else {
            for (int index = 0; index < items.length(); index++) {
                JSONObject item = items.optJSONObject(index);
                if (item == null) continue;
                String state = item.optString("status");
                String label = "passed".equals(state) ? "Passed" : "attention".equals(state) ? "Needs attention" : "Not checked";
                content.addView(ui.text(item.optString("label") + " — " + label, 14,
                    "passed".equals(state) ? AppColors.GREEN_DARK : AppColors.RED, Typeface.BOLD),
                    ui.fullWidth(LinearLayout.LayoutParams.WRAP_CONTENT, 0, ui.dp(12), 0, ui.dp(3)));
                content.addView(ui.text(item.optString("detail"), 12, AppColors.MUTED, Typeface.NORMAL));
            }
        }
        content.addView(ui.text("Not tested automatically: motor and gate movement, guards, emergency-stop operation, lights, buzzer, screen, and water/count-sensor accuracy. A passed connection check does not prove these parts work.",
            13, AppColors.INK, Typeface.BOLD), ui.fullWidth(LinearLayout.LayoutParams.WRAP_CONTENT, 0, ui.dp(16), 0, 0));
        scroll.addView(content, ui.matchWrap());
        AlertDialog.Builder dialog = new AlertDialog.Builder(controller.activity()).setTitle("Connections and sensors")
            .setView(scroll).setNegativeButton("Close", null);
        dialog.setPositiveButton("Check again", (view, which) -> controller.checkMachine(() -> showMachineCheckResults(controller, ui)));
        dialog.show();
    }

    private String[][] batchRows(JSONObject activeBatch, Ui ui) {
        if (activeBatch == null) {
            return new String[][]{{"Status", "No production run yet"}};
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
            return new String[][]{{"--", "No production steps yet", "Waiting for a production run", "Waiting"}};
        }
        String[][] rows = new String[steps.length()][4];
        for (int index = 0; index < steps.length(); index++) {
            JSONObject step = steps.optJSONObject(index);
            if (step == null) step = new JSONObject();
            rows[index][0] = String.valueOf(index + 1);
            rows[index][1] = step.optString("name", step.optString("step", "Process step"));
            rows[index][2] = step.optString("detail", "Waiting for a controller report.");
            rows[index][3] = step.optString("status", "Not reported");
        }
        return rows;
    }

    private String[][] qualityRows(JSONArray qualityChecks) {
        if (qualityChecks == null || qualityChecks.length() == 0) {
            return new String[][]{{"Checks", "No quality checks recorded yet"}};
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

}
