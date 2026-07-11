package com.paperbriq.app.data;

import org.json.JSONObject;

public class AppSession {
    public JSONObject user;
    public JSONObject summary;
    public String currentPage = "dashboard";

    public JSONObject totals() {
        JSONObject totals = summary != null ? summary.optJSONObject("totals") : null;
        if (totals != null) return totals;

        JSONObject fallback = new JSONObject();
        try {
            fallback.put("paperKg", 45);
            fallback.put("briquettes", 8);
            fallback.put("contributors", 15);
            fallback.put("co2KgAvoided", 120);
        } catch (Exception ignored) {
        }
        return fallback;
    }
}
