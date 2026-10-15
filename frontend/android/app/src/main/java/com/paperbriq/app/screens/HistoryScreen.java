package com.paperbriq.app.screens;

import android.widget.LinearLayout;

import com.paperbriq.app.AppColors;
import com.paperbriq.app.AppController;
import com.paperbriq.app.ui.Ui;

import org.json.JSONArray;
import org.json.JSONObject;

public class HistoryScreen implements AppScreen {
    @Override
    public String key() {
        return "history";
    }

    @Override
    public String navLabel() {
        return "Event History";
    }

    @Override
    public String title() {
        return "System History";
    }

    @Override
    public String subtitle() {
        return "Review previous machine sessions, alerts, and system events.";
    }

    @Override
    public void render(LinearLayout root, AppController controller) {
        Ui ui = controller.ui();
        JSONObject payload = controller.session().pagePayload("history");
        JSONObject stats = payload.optJSONObject("stats");
        if (stats == null) stats = new JSONObject();
        JSONArray events = payload.optJSONArray("events");

        ui.addGrid(root, new android.view.View[]{
            ui.statCard("Total Sessions", ui.formatNumber(stats.optDouble("totalSessions", 0)), "From process runs", AppColors.GREEN),
            ui.statCard("Completed Runs", ui.formatNumber(stats.optDouble("completedRuns", 0)), stats.optString("successRate", "0%") + " success rate", AppColors.GREEN),
            ui.statCard("Total Runtime", stats.optString("totalRuntime", "0 mins"), "All recorded runs", AppColors.GREEN)
        }, 1);

        root.addView(ui.sectionTitle("Recent Activities", "View All"));
        root.addView(ui.simpleList(eventRows(events, 4)));

        root.addView(ui.sectionTitle("All Events", ""));
        root.addView(ui.simpleList(eventRows(events, 30)));
    }

    private String[][] eventRows(JSONArray events, int limit) {
        if (events == null || events.length() == 0) {
            return new String[][]{{"--", "No history events in MySQL yet", "Waiting for system events", "Empty"}};
        }
        int count = Math.min(events.length(), limit);
        String[][] rows = new String[count][4];
        for (int index = 0; index < count; index++) {
            JSONObject event = events.optJSONObject(index);
            if (event == null) event = new JSONObject();
            rows[index][0] = event.optString("time", "--");
            rows[index][1] = event.optString("title", "System event");
            rows[index][2] = event.optString("description", "");
            rows[index][3] = event.optString("category", "Event");
        }
        return rows;
    }
}
