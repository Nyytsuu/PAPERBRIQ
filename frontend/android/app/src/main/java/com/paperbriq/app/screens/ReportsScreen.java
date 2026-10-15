package com.paperbriq.app.screens;

import android.widget.LinearLayout;

import com.paperbriq.app.AppColors;
import com.paperbriq.app.AppController;
import com.paperbriq.app.ui.Ui;

import org.json.JSONArray;
import org.json.JSONObject;

public class ReportsScreen implements AppScreen {
    @Override
    public String key() {
        return "reports";
    }

    @Override
    public String navLabel() {
        return "Performance";
    }

    @Override
    public String title() {
        return "Performance Reports";
    }

    @Override
    public String subtitle() {
        return "Production trends and environmental impact over time.";
    }

    @Override
    public void render(LinearLayout root, AppController controller) {
        Ui ui = controller.ui();
        JSONObject reports = controller.session().pagePayload("reports");
        JSONObject charts = reports.optJSONObject("charts");
        JSONObject chartMeta = reports.optJSONObject("chartMeta");
        JSONArray labels = chartMeta != null ? chartMeta.optJSONArray("labels") : null;
        if (charts == null) charts = new JSONObject();

        ui.addGrid(root, new android.view.View[]{
            ui.filterCard("Report Period", reports.optString("period", "Waiting for report records")),
            ui.filterCard("Source", chartMeta != null ? chartMeta.optString("source", "MySQL") : "MySQL")
        }, 1);
        ui.addGrid(root, statCards(ui, reports.optJSONArray("cards")), 2);

        root.addView(ui.sectionTitle("Charts", ""));
        ui.addGrid(root, new android.view.View[]{
            ui.barChartCard("Paper Collected (kg)", chartRows(charts.optJSONArray("paperCollected"), labels)),
            ui.barChartCard("Briquettes Produced (pcs)", chartRows(charts.optJSONArray("briquettesProduced"), labels)),
            ui.barChartCard("Water Saved (L)", chartRows(charts.optJSONArray("waterSaved"), labels))
        }, 1);

        ui.addGrid(root, new android.view.View[]{
            ui.infoListCard("Top Barangays", barangayRows(reports.optJSONArray("topBarangays"), ui)),
            ui.infoListCard("Environmental Impact", impactRows(reports.optJSONArray("impact"))),
            ui.infoListCard("Report Summary", new String[][]{
                {"Updated", reports.optString("displayTime", "--")},
                {"Source", chartMeta != null ? chartMeta.optString("source", "MySQL") : "MySQL"},
                {"Records", reports.optJSONArray("topBarangays") != null ? String.valueOf(reports.optJSONArray("topBarangays").length()) : "0"}
            })
        }, 1);
    }

    private android.view.View[] statCards(Ui ui, JSONArray cards) {
        if (cards == null || cards.length() == 0) {
            return new android.view.View[]{ui.statCard("No report data", "--", "Waiting for MySQL records", AppColors.GREEN)};
        }
        android.view.View[] views = new android.view.View[cards.length()];
        for (int index = 0; index < cards.length(); index++) {
            JSONObject card = cards.optJSONObject(index);
            if (card == null) card = new JSONObject();
            int color = index == 1 ? AppColors.AMBER : index == 2 ? AppColors.BLUE : AppColors.GREEN;
            views[index] = ui.statCard(card.optString("label", "Metric"), card.optString("value", "--"), card.optString("change", ""), color);
        }
        return views;
    }

    private String[][] chartRows(JSONArray values, JSONArray labels) {
        if (values == null || values.length() == 0) {
            return new String[][]{{"--", "0"}};
        }
        String[][] rows = new String[values.length()][2];
        for (int index = 0; index < values.length(); index++) {
            rows[index][0] = labels != null ? labels.optString(index, String.valueOf(index + 1)) : String.valueOf(index + 1);
            rows[index][1] = String.valueOf(values.optDouble(index, 0));
        }
        return rows;
    }

    private String[][] barangayRows(JSONArray barangays, Ui ui) {
        if (barangays == null || barangays.length() == 0) {
            return new String[][]{{"--", "No barangay records yet"}};
        }
        String[][] rows = new String[barangays.length()][2];
        for (int index = 0; index < barangays.length(); index++) {
            JSONObject item = barangays.optJSONObject(index);
            if (item == null) item = new JSONObject();
            rows[index][0] = item.optString("barangay", "Unassigned");
            rows[index][1] = ui.formatNumber(item.optDouble("paperKg", 0)) + " kg - " + ui.formatNumber(item.optInt("contributors", 0)) + " contributors";
        }
        return rows;
    }

    private String[][] impactRows(JSONArray impact) {
        if (impact == null || impact.length() == 0) {
            return new String[][]{{"--", "No impact records yet"}};
        }
        String[][] rows = new String[impact.length()][2];
        for (int index = 0; index < impact.length(); index++) {
            JSONObject item = impact.optJSONObject(index);
            if (item == null) item = new JSONObject();
            rows[index][0] = item.optString("label", "Impact");
            rows[index][1] = item.optString("value", "--");
        }
        return rows;
    }
}
