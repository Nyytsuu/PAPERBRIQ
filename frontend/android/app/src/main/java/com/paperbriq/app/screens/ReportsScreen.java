package com.paperbriq.app.screens;

import android.graphics.Typeface;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;

import com.paperbriq.app.AppColors;
import com.paperbriq.app.AppController;
import com.paperbriq.app.ui.Ui;

import org.json.JSONArray;
import org.json.JSONObject;

public class ReportsScreen implements AppScreen {
    private boolean lastMonthSelected;

    @Override
    public String key() {
        return "reports";
    }

    @Override
    public String navLabel() {
        return "Reports";
    }

    @Override
    public String title() {
        return "Reports";
    }

    @Override
    public String subtitle() {
        return "Paper collection and briquette production totals.";
    }

    @Override
    public void render(LinearLayout root, AppController controller) {
        Ui ui = controller.ui();
        JSONObject reports = controller.session().pagePayload("reports");
        JSONObject monthly = reports.optJSONObject("monthly");
        JSONObject period = monthly == null ? null : monthly.optJSONObject(lastMonthSelected ? "lastMonth" : "thisMonth");

        Button thisMonth = lastMonthSelected ? ui.outlineButton("This month") : ui.greenButton("This month");
        Button lastMonth = lastMonthSelected ? ui.greenButton("Last month") : ui.outlineButton("Last month");
        thisMonth.setSelected(!lastMonthSelected);
        lastMonth.setSelected(lastMonthSelected);
        thisMonth.setOnClickListener(v -> selectMonth(false, root, controller));
        lastMonth.setOnClickListener(v -> selectMonth(true, root, controller));
        ui.addGrid(root, new View[]{thisMonth, lastMonth}, 2);
        root.addView(ui.sectionTitle(period == null ? "Production summary" : period.optString("month"), ""));
        if (period != null) {
            root.addView(ui.text(period.optString("dateRange") + " · Updated " + reports.optString("displayTime", "—"),
                12, AppColors.MUTED, Typeface.NORMAL), ui.fullWidth(-2, 0, 0, 0, ui.dp(12)));
        }
        ui.addGrid(root, new View[]{
            ui.readingCard("Paper Collected", value(ui, period, "paperKg", " kg"), "", AppColors.GREEN),
            ui.readingCard("Briquettes Produced", value(ui, period, "briquettes", " pcs"), "", AppColors.AMBER)
        }, 2);
        root.addView(ui.sectionTitle("Weekly breakdown", ""));
        if (monthly != null && !monthly.optBoolean("recordsAvailable")) {
            root.addView(ui.text("Recorded data is unavailable.", 13, AppColors.MUTED, Typeface.NORMAL));
        }
        JSONArray weeks = period == null ? null : period.optJSONArray("weeks");
        if (weeks != null) {
            LinearLayout table = ui.card(16);
            table.setPadding(ui.dp(12), ui.dp(10), ui.dp(12), ui.dp(4));
            addWeekRow(ui, table, "Dates", "Paper (kg)", "Briquettes", true);
            for (int i = 0; i < weeks.length(); i++) {
                JSONObject week = weeks.optJSONObject(i);
                if (week == null) continue;
                boolean empty = week.isNull("paperKg") && week.isNull("briquettes");
                addWeekRow(ui, table, week.optString("label"),
                    empty ? "No records" : value(ui, week, "paperKg", ""),
                    empty ? "" : value(ui, week, "briquettes", ""), false);
            }
            root.addView(table, ui.fullWidth(-2, 0, ui.dp(4), 0, ui.dp(12)));
        }
        root.addView(ui.text("Paper used · Bond paper, 20 lb (about 75 gsm)", 12, AppColors.MUTED, Typeface.NORMAL));
    }

    private void selectMonth(boolean lastMonth, LinearLayout root, AppController controller) {
        if (lastMonthSelected == lastMonth) return;
        lastMonthSelected = lastMonth;
        root.removeAllViews();
        render(root, controller);
    }

    private void addWeekRow(Ui ui, LinearLayout table, String dates, String paper, String briquettes, boolean header) {
        LinearLayout row = ui.row();
        row.setPadding(0, ui.dp(7), 0, ui.dp(7));
        int style = header ? Typeface.BOLD : Typeface.NORMAL;
        row.addView(ui.text(dates, 13, AppColors.INK, style), new LinearLayout.LayoutParams(0, -2, 1.35f));
        row.addView(ui.text(paper, 13, AppColors.INK, style), new LinearLayout.LayoutParams(0, -2, 1));
        row.addView(ui.text(briquettes, 13, AppColors.INK, style), new LinearLayout.LayoutParams(0, -2, 1));
        table.addView(row, ui.fullWidth(-2, 0, 0, 0, 0));
    }

    private String value(Ui ui, JSONObject data, String key, String unit) {
        return data == null || data.isNull(key) ? "—" : ui.formatNumber(data.optDouble(key)) + unit;
    }
}
