package com.paperbriq.app.screens;

import android.app.AlertDialog;
import android.graphics.Color;
import android.graphics.Typeface;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.NumberPicker;
import android.widget.ScrollView;
import android.widget.TextView;

import com.paperbriq.app.AppColors;
import com.paperbriq.app.AppController;
import com.paperbriq.app.ui.Ui;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.Locale;

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
            addProductionSettings(root, controller, ui);
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
            {"Drying start", "Separate start after loading"},
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

    private void addProductionSettings(LinearLayout root, AppController controller, Ui ui) {
        JSONObject status = controller.session().pagePayload("deviceControl");
        boolean machineAccess = controller.hasMachineAccess();
        JSONObject controls = status.optJSONObject("productionControls");
        JSONObject run = controls == null ? null : controls.optJSONObject("run");
        LinearLayout card = ui.card(18);
        card.setPadding(ui.dp(16), ui.dp(16), ui.dp(16), ui.dp(16));
        card.addView(ui.text("Water and drying", 19, AppColors.INK, Typeface.BOLD));
        JSONObject thermal = controls == null ? null : controls.optJSONObject("thermal");
        JSONObject preheat = controls == null ? null : controls.optJSONObject("preheat");
        int target = thermal == null ? 0 : thermal.optInt("targetC");
        int preheatLimit = preheat == null ? 0 : preheat.optInt("maxSeconds");
        card.addView(ui.text("Preheat chamber", 17, AppColors.INK, Typeface.BOLD),
            ui.fullWidth(-2, 0, ui.dp(16), 0, ui.dp(6)));
        card.addView(ui.text(target > 0 ? "Set the physical thermostat to " + target + " °C. Preheating and holding stop after "
            + durationLabel(preheatLimit) + " in total." : "Update both boards to use preheating.",
            12, AppColors.MUTED, Typeface.NORMAL));
        boolean preheating = run != null && "preheat".equals(run.optString("output")) && controls.optBoolean("active");
        if (preheating) card.addView(ui.text("ready_to_load".equals(run.optString("state")) ?
            "Last report: ready to load" : "Preheating — waiting for temperature",
            15, AppColors.GREEN_DARK, Typeface.BOLD));
        if (preheating) card.addView(ui.text("Preheat time remaining: " + run.optInt("remainingSeconds") + " sec",
            12, AppColors.MUTED, Typeface.NORMAL));
        Button preheatButton = ui.greenButton("Start preheat");
        boolean preheatAvailable = machineAccess && preheat != null && preheat.optBoolean("canStart") && preheatLimit > 0;
        preheatButton.setEnabled(preheatAvailable); preheatButton.setAlpha(preheatAvailable ? 1f : 0.45f);
        preheatButton.setOnClickListener(v -> controller.confirmAction("Preheat the empty chamber?",
            "Close the door, keep the heater fan running, and set the thermostat to " + target
                + " °C. Heating stops after " + durationLabel(preheatLimit) + ". Forming cannot run during preheating.",
            () -> controller.sendProductionCommand("preheat_start", preheatLimit, "")));
        card.addView(preheatButton, ui.fullWidth(ui.dp(52), 0, ui.dp(8), 0, 0));
        if (preheating && machineAccess) {
            Button stopPreheat = ui.outlineButton("Stop preheat to load");
            stopPreheat.setTextColor(AppColors.RED);
            stopPreheat.setOnClickListener(v -> controller.sendDeviceCommand("stop"));
            card.addView(stopPreheat, ui.fullWidth(ui.dp(48), 0, ui.dp(8), 0, 0));
        }
        if (!preheatAvailable && !preheating) card.addView(ui.text(preheat == null ? "Waiting for machine controls."
            : preheat.optString("reason", "Waiting for machine controls."), 12, AppColors.MUTED, Typeface.NORMAL));
        card.addView(ui.text("Preheat while forming is stopped. Press Stop, load briquettes, close the door, then Start drying. The drying countdown begins after the chamber reaches temperature. Opening the door cancels heating; closing it does not restart it.",
            12, AppColors.MUTED, Typeface.NORMAL), ui.fullWidth(-2, 0, ui.dp(8), 0, 0));
        JSONObject pump = controls == null ? null : controls.optJSONObject("pump");
        Button enablePump = ui.greenButton("Enable preset pump for next batch");
        boolean pumpAvailable = machineAccess && pump != null && pump.optBoolean("canStart");
        enablePump.setEnabled(pumpAvailable);
        enablePump.setAlpha(pumpAvailable ? 1f : 0.45f);
        enablePump.setOnClickListener(v -> controller.confirmAction("Enable the preset pump?",
            "After enabling, use Reset and Start forming. Wait for Ready for paper before feeding.",
            () -> controller.sendProductionCommand("pump_arm", 0, "")));
        card.addView(enablePump, ui.fullWidth(ui.dp(52), 0, ui.dp(12), 0, 0));
        if (!pumpAvailable && pump != null && !pump.optString("reason").isEmpty()) {
            card.addView(ui.text(pump.optString("reason"), 12, AppColors.MUTED, Typeface.NORMAL));
        }
        String action = "drying";
        String label = "Drying time";
        LinearLayout row = ui.row();
        row.setGravity(Gravity.CENTER_VERTICAL);
        LinearLayout description = ui.column();
        description.addView(ui.text(label, 15, AppColors.INK, Typeface.BOLD));
        TextView duration = ui.text(durationLabel(controller.productionSeconds(action)), 18, AppColors.GREEN_DARK, Typeface.BOLD);
        description.addView(duration);
        row.addView(description, new LinearLayout.LayoutParams(0, -2, 1));
        Button edit = ui.outlineButton("Set time");
        edit.setEnabled(machineAccess);
        edit.setContentDescription("Set " + label.toLowerCase());
        edit.setOnClickListener(v -> editProductionDuration(controller, ui, action, label));
        row.addView(edit, new LinearLayout.LayoutParams(ui.dp(108), ui.dp(48)));
        card.addView(row, ui.fullWidth(-2, 0, ui.dp(16), 0, 0));
        JSONObject output = controls == null ? null : controls.optJSONObject(action);
        boolean ownRun = controls != null && controls.optBoolean("active") && run != null && action.equals(run.optString("output"));
        int maximum = output == null ? 0 : output.optInt("maxSeconds");
        card.addView(ui.text(maximum > 0 ? "Maximum: " + durationLabel(maximum) : "Maximum time awaiting commissioning", 12, AppColors.MUTED, Typeface.NORMAL));
        if (run != null && action.equals(run.optString("output"))) {
            String state = run.optString("state", "unknown").replace('_', ' ');
            int remaining = run.optInt("remainingSeconds");
            card.addView(ui.text("warming".equals(state)
                ? "Warming to target — drying countdown has not started"
                : "Controller: " + state + " • remaining " + (remaining == 0 ? "0 sec" : durationLabel(remaining)), 13, AppColors.INK, Typeface.BOLD));
            card.addView(ui.text("Applied total: " + durationLabel(run.optInt("totalSeconds")) + " • last machine report", 12, AppColors.MUTED, Typeface.NORMAL));
        }
        String command = ownRun ? "drying_time" : "drying_start";
        Button apply = ui.greenButton(ownRun ? "Apply total time" : "Start drying");
        boolean available = machineAccess && output != null && output.optBoolean(ownRun ? "canAdjust" : "canStart")
            && controller.productionSeconds(action) > 0 && controller.productionSeconds(action) <= maximum;
        apply.setEnabled(available); apply.setAlpha(available ? 1f : 0.45f);
        apply.setOnClickListener(v -> controller.confirmAction(ownRun ? "Change this run's total time?" : "Start drying?",
            ownRun ? "Elapsed time is kept. A total shorter than elapsed time stops the output. Wait for the controller to confirm the change."
            : "Load the chamber, close the door and keep the heater fan operating. Set the thermostat to " + target + " °C. The drying timer starts once temperature is reached; warm-up is limited to 10 minutes.",
            () -> controller.sendProductionCommand(command, controller.productionSeconds(action), ownRun ? run.optString("id") : "")));
        card.addView(apply, ui.fullWidth(ui.dp(52), 0, ui.dp(8), 0, 0));
        if (!available) card.addView(ui.text(output == null ? "Update and reconnect the machine boards and server."
            : !output.optString("reason").isEmpty() ? output.optString("reason")
            : controller.productionSeconds(action) <= 0 ? "Set a duration first." : controller.productionSeconds(action) > maximum ? "Choose a time within the operating limit." : "Waiting for machine controls.",
            12, AppColors.MUTED, Typeface.NORMAL));
        card.addView(ui.text("Pump trigger: paper entering the shredder", 13, AppColors.INK, Typeface.BOLD),
            ui.fullWidth(-2, 0, ui.dp(16), 0, ui.dp(6)));
        card.addView(ui.text("Drying needs a separate start after loading the chamber. The paper sensor does not start the heater.", 12, AppColors.MUTED, Typeface.NORMAL),
            ui.fullWidth(-2, 0, 0, 0, ui.dp(6)));
        card.addView(ui.text("Temperature is set on the digital thermostat.", 12, AppColors.MUTED, Typeface.NORMAL));
        Button stop = ui.outlineButton("Stop"); stop.setTextColor(AppColors.RED);
        JSONObject device = status.optJSONObject("device");
        stop.setEnabled(machineAccess && device != null && device.optBoolean("connected"));
        stop.setOnClickListener(v -> controller.sendDeviceCommand("stop"));
        card.addView(stop, ui.fullWidth(ui.dp(48), 0, ui.dp(12), 0, 0));
        root.addView(card, ui.fullWidth(-2, 0, ui.dp(12), 0, ui.dp(12)));
    }

    private String durationLabel(int seconds) {
        if (seconds <= 0) return "Not set";
        int hours = seconds / 3600, minutes = (seconds % 3600) / 60, remainder = seconds % 60;
        String label = hours > 0 ? hours + " hr" : "";
        if (minutes > 0) label += (label.isEmpty() ? "" : " ") + minutes + " min";
        if (remainder > 0) label += (label.isEmpty() ? "" : " ") + remainder + " sec";
        return label;
    }

    private void editProductionDuration(AppController controller, Ui ui, String action, String label) {
        int maximum = productionMaximum(controller, action);
        if (maximum <= 0) {
            new AlertDialog.Builder(controller.activity()).setTitle(label)
                .setMessage("A maximum time must be configured on the machine before choosing a drying duration. If setup is complete, reconnect and wait for the machine's latest report.")
                .setPositiveButton("Close", null).show();
            return;
        }
        int pickerMaximum = Math.min(maximum, Integer.MAX_VALUE / 1000);
        int saved = controller.productionSeconds(action);
        int initial = Math.max(0, Math.min(saved, pickerMaximum));
        LinearLayout content = ui.column();
        content.setPadding(ui.dp(16), ui.dp(12), ui.dp(16), ui.dp(8));
        TextView preview = ui.text("", 30, AppColors.GREEN_DARK, Typeface.BOLD);
        preview.setGravity(Gravity.CENTER);
        content.addView(preview, ui.fullWidth(-2, 0, 0, 0, ui.dp(8)));
        TextView hint = ui.text("Scroll to choose how long it runs", 13, AppColors.MUTED, Typeface.NORMAL);
        hint.setGravity(Gravity.CENTER);
        content.addView(hint);

        LinearLayout wheels = ui.row();
        NumberPicker hours = addDurationWheel(controller, ui, wheels, "Hours", pickerMaximum / 3600);
        NumberPicker minutes = addDurationWheel(controller, ui, wheels, "Minutes", 59);
        NumberPicker seconds = addDurationWheel(controller, ui, wheels, "Seconds", 59);
        NumberPicker[] pickers = {hours, minutes, seconds};
        hours.setValue(initial / 3600);
        minutes.setValue((initial % 3600) / 60);
        seconds.setValue(initial % 60);
        content.addView(wheels, ui.fullWidth(-2, 0, ui.dp(12), 0, ui.dp(8)));

        LinearLayout shortcuts = ui.row();
        int[] increments = {10, 60, 300};
        Button[] addButtons = new Button[increments.length];
        String[] addLabels = {"+10 sec", "+1 min", "+5 min"};
        for (int i = 0; i < increments.length; i++) {
            Button button = ui.outlineButton(addLabels[i]);
            button.setMinWidth(0);
            button.setMinimumWidth(0);
            button.setPadding(ui.dp(2), ui.dp(4), ui.dp(2), ui.dp(4));
            LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(0, ui.dp(48), 1);
            if (i > 0) params.setMarginStart(ui.dp(6));
            shortcuts.addView(button, params);
            addButtons[i] = button;
        }
        content.addView(shortcuts);
        content.addView(ui.text(maximum > 0 ? "Maximum: " + durationLabel(maximum)
            : "The machine's maximum time has not been set yet.", 12, AppColors.MUTED, Typeface.NORMAL),
            ui.fullWidth(-2, 0, ui.dp(12), 0, ui.dp(6)));
        content.addView(ui.text("Saving does not start the machine. For a running cycle, use Apply total time after saving; elapsed time is kept.",
            12, AppColors.MUTED, Typeface.NORMAL));
        TextView error = ui.text(saved > pickerMaximum ? "Saved time exceeds the limit. Choose a shorter time." : "",
            13, AppColors.RED, Typeface.NORMAL);
        error.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
        content.addView(error, ui.fullWidth(-2, 0, ui.dp(8), 0, 0));
        ScrollView scroll = new ScrollView(controller.activity());
        scroll.addView(content, ui.matchWrap());
        AlertDialog dialog = new AlertDialog.Builder(controller.activity()).setTitle(label).setView(scroll)
            .setNegativeButton("Cancel", null).setNeutralButton("Clear", (d, which) -> {
                controller.saveProductionSeconds(action, 0);
            }).setPositiveButton("Save time", null).create();
        boolean[] scrolling = new boolean[pickers.length];
        Runnable refresh = () -> {
            // Constrain the smaller units at the limit, including limits such as 1 min 30 sec.
            minutes.setMaxValue(Math.min(59, (pickerMaximum - hours.getValue() * 3600) / 60));
            seconds.setMaxValue(Math.min(59, pickerMaximum - hours.getValue() * 3600 - minutes.getValue() * 60));
            int total = durationSeconds(hours, minutes, seconds);
            preview.setText(String.format(Locale.getDefault(), "%02d : %02d : %02d", hours.getValue(), minutes.getValue(), seconds.getValue()));
            preview.setContentDescription("Total duration: " + (total == 0 ? "zero seconds" : durationLabel(total)));
            boolean idle = !scrolling[0] && !scrolling[1] && !scrolling[2];
            Button save = dialog.getButton(AlertDialog.BUTTON_POSITIVE);
            if (save != null) save.setEnabled(total > 0 && idle);
            for (int i = 0; i < addButtons.length; i++) {
                boolean enabled = idle && total + increments[i] <= pickerMaximum;
                addButtons[i].setEnabled(enabled);
                addButtons[i].setAlpha(enabled ? 1f : 0.45f);
            }
        };
        for (int i = 0; i < pickers.length; i++) {
            final int index = i;
            pickers[i].setOnValueChangedListener((picker, previous, next) -> { error.setText(""); refresh.run(); });
            pickers[i].setOnScrollListener((picker, state) -> {
                scrolling[index] = state != NumberPicker.OnScrollListener.SCROLL_STATE_IDLE;
                refresh.run();
            });
        }
        for (int i = 0; i < addButtons.length; i++) {
            final int increment = increments[i];
            addButtons[i].setOnClickListener(v -> {
                int total = Math.min(pickerMaximum, durationSeconds(hours, minutes, seconds) + increment);
                // Restore ranges before setting values, then apply the limit to the new selection.
                minutes.setMaxValue(59); seconds.setMaxValue(59);
                hours.setValue(total / 3600); minutes.setValue((total % 3600) / 60); seconds.setValue(total % 60);
                error.setText(""); refresh.run();
            });
        }
        dialog.setOnShowListener(d -> {
            refresh.run();
            dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v -> {
                int total = durationSeconds(hours, minutes, seconds);
                // Recheck the latest reported limit in case it changed while the picker was open.
                int latestMaximum = productionMaximum(controller, action);
                if (latestMaximum > 0 && total > latestMaximum) {
                    error.setText("Maximum is now " + durationLabel(latestMaximum) + ". Choose a shorter time."); return;
                }
                if (total < 1) { error.setText("Choose a duration above zero."); return; }
                controller.saveProductionSeconds(action, total); dialog.dismiss();
            });
        });
        dialog.show();
    }

    private int productionMaximum(AppController controller, String action) {
        JSONObject controls = controller.session().pagePayload("deviceControl").optJSONObject("productionControls");
        JSONObject output = controls == null ? null : controls.optJSONObject(action);
        return output == null ? 0 : output.optInt("maxSeconds");
    }

    private int durationSeconds(NumberPicker hours, NumberPicker minutes, NumberPicker seconds) {
        return hours.getValue() * 3600 + minutes.getValue() * 60 + seconds.getValue();
    }

    private NumberPicker addDurationWheel(AppController controller, Ui ui, LinearLayout row, String unit, int maximum) {
        LinearLayout column = ui.column();
        column.setGravity(Gravity.CENTER_HORIZONTAL);
        TextView label = ui.text(unit, 13, AppColors.MUTED, Typeface.BOLD);
        label.setGravity(Gravity.CENTER);
        NumberPicker picker = new NumberPicker(controller.activity());
        picker.setId(View.generateViewId());
        picker.setMinValue(0); picker.setMaxValue(maximum);
        picker.setWrapSelectorWheel(false);
        picker.setFormatter(value -> String.format(Locale.getDefault(), "%02d", value));
        picker.setDescendantFocusability(ViewGroup.FOCUS_BLOCK_DESCENDANTS);
        picker.setContentDescription(unit);
        label.setLabelFor(picker.getId());
        column.addView(label, ui.matchWrap());
        column.addView(picker, new LinearLayout.LayoutParams(-1, ui.dp(150)));
        row.addView(column, new LinearLayout.LayoutParams(0, -2, 1));
        return picker;
    }

    private void addMachineControls(LinearLayout root, AppController controller, Ui ui) {
        JSONObject status = controller.session().pagePayload("deviceControl");
        boolean enabled = controller.hasMachineAccess();
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
        JSONObject production = status.optJSONObject("productionControls");
        JSONObject cycle = production == null ? null : production.optJSONObject("run");
        boolean pumpPrepared = production != null && production.optBoolean("fresh") && cycle != null
            && "pump".equals(cycle.optString("output")) && "waiting_for_paper".equals(cycle.optString("state"));

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
        start.setEnabled(startAvailable && checksPassed && pumpPrepared);
        start.setOnClickListener(view -> controller.confirmAction(
            "Start forming briquettes?",
            "The gates close first. Wait for Ready for paper before feeding. Paper at the shredder inlet starts shredding, mixing and the preset water dose. Drying starts separately after loading the chamber.",
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
        if (!pumpPrepared && startableState) card.addView(ui.text("Enable the preset pump below, then use Reset before Start forming.",
            13, AppColors.MUTED, Typeface.NORMAL), ui.fullWidth(-2, 0, ui.dp(8), 0, ui.dp(12)));

        Button autoCheck = ui.greenButton("Check machine");
        autoCheck.setOnClickListener(view -> showFunctionalChecks(controller, ui));
        card.addView(autoCheck, ui.fullWidth(ui.dp(48), 0, 0, 0, ui.dp(6)));
        card.addView(ui.text("Run each component briefly and follow its checklist. Use an empty machine and remain beside the emergency stop.",
            12, AppColors.MUTED, Typeface.NORMAL),
            ui.fullWidth(LinearLayout.LayoutParams.WRAP_CONTENT, 0, 0, 0, ui.dp(8)));

        if (!enabled) {
            Button signIn = ui.outlineButton(controller.isDemoAdmin() ? "Machine access" : "Sign in for machine access");
            signIn.setOnClickListener(view -> controller.signInForMachineAccess());
            card.addView(signIn, ui.fullWidth(ui.dp(48), 0, ui.dp(8), 0, 0));
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
                boolean available = controller.hasMachineAccess() && health != null
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
