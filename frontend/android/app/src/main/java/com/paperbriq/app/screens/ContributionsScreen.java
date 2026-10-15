package com.paperbriq.app.screens;

import android.widget.LinearLayout;

import com.paperbriq.app.AppColors;
import com.paperbriq.app.AppController;
import com.paperbriq.app.ui.Ui;

import org.json.JSONArray;
import org.json.JSONObject;

public class ContributionsScreen implements AppScreen {
    @Override
    public String key() {
        return "contributions";
    }

    @Override
    public String navLabel() {
        return "Contributors";
    }

    @Override
    public String title() {
        return "User Contributions";
    }

    @Override
    public String subtitle() {
        return "Manage user paper donations and earned points.";
    }

    @Override
    public void render(LinearLayout root, AppController controller) {
        Ui ui = controller.ui();
        JSONObject payload = controller.session().pagePayload("contributions");
        JSONObject metrics = payload.optJSONObject("metrics");
        if (metrics == null) metrics = new JSONObject();
        JSONArray contributors = payload.optJSONArray("contributors");

        ui.addGrid(root, new android.view.View[]{
            ui.statCard("Total Contributors", ui.formatNumber(metrics.optDouble("totalContributors", 0)), "From contributor records", AppColors.GREEN),
            ui.statCard("Paper Donated", ui.formatNumber(metrics.optDouble("paperDonatedThisWeekKg", 0)) + " kg", "Collected in the last 7 days", AppColors.BLUE),
            ui.statCard(
                "Briquettes Produced",
                ui.formatNumber(metrics.optDouble("briquettesProducedThisWeek", 0)) + " / " + ui.formatNumber(metrics.optDouble("weeklyTarget", 0)),
                "From batch records",
                AppColors.AMBER
            )
        }, 1);

        root.addView(ui.sectionTitle("Search and Filters", ""));
        ui.addGrid(root, new android.view.View[]{
            ui.filterCard("Search", "Contributor, barangay, or scan ID"),
            ui.filterCard("Barangay", "All Barangays"),
            ui.filterCard("Status", "All Status"),
            ui.filterCard("Date", "Live database records")
        }, 1);

        root.addView(ui.sectionTitle("Contributors", ""));
        root.addView(ui.simpleList(contributorRows(contributors, ui)));

        root.addView(ui.sectionTitle("Top Contributors This Week", "View All"));
        ui.addGrid(root, contributorCards(ui, contributors), 1);
    }

    private String[][] contributorRows(JSONArray contributors, Ui ui) {
        if (contributors == null || contributors.length() == 0) {
            return new String[][]{{"--", "No contributors in MySQL yet", "Waiting for collection records", "Empty"}};
        }

        int count = Math.min(contributors.length(), 10);
        String[][] rows = new String[count][4];
        for (int index = 0; index < count; index++) {
            JSONObject contributor = contributors.optJSONObject(index);
            if (contributor == null) contributor = new JSONObject();
            String name = contributor.optString("name", "Contributor");
            String barangay = contributor.optString("barangay", "Unassigned");
            double paperKg = contributor.optDouble("paperDonatedKg", 0);
            double points = contributor.optDouble("points", 0);
            rows[index][0] = initials(name);
            rows[index][1] = name;
            rows[index][2] = barangay + " - " + ui.formatNumber(paperKg) + " kg - " + ui.formatNumber(points) + " pts";
            rows[index][3] = contributor.optString("status", "Recorded");
        }
        return rows;
    }

    private android.view.View[] contributorCards(Ui ui, JSONArray contributors) {
        if (contributors == null || contributors.length() == 0) {
            return new android.view.View[]{ui.infoListCard("Top Contributors", new String[][]{{"Records", "No contributor totals yet"}})};
        }

        int count = Math.min(contributors.length(), 5);
        android.view.View[] cards = new android.view.View[count];
        for (int index = 0; index < count; index++) {
            JSONObject contributor = contributors.optJSONObject(index);
            if (contributor == null) contributor = new JSONObject();
            cards[index] = ScreenPieces.contributorCard(
                ui,
                String.valueOf(index + 1),
                contributor.optString("name", "Contributor"),
                contributor.optString("barangay", "Unassigned"),
                ui.formatNumber(contributor.optDouble("paperDonatedKg", 0)) + " kg",
                ui.formatNumber(contributor.optDouble("points", 0)) + " pts"
            );
        }
        return cards;
    }

    private String initials(String name) {
        String[] parts = name.trim().split("\\s+");
        if (parts.length == 0 || parts[0].isEmpty()) return "--";
        String first = parts[0].substring(0, 1).toUpperCase();
        if (parts.length == 1) return first;
        return first + parts[parts.length - 1].substring(0, 1).toUpperCase();
    }
}
