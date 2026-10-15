package com.paperbriq.app.data;

import org.json.JSONObject;

import java.util.HashMap;
import java.util.Map;

public class AppSession {
    public JSONObject user;
    public JSONObject summary;
    public String currentPage = "dashboard";
    private final Map<String, JSONObject> pagePayloads = new HashMap<>();

    public void clear() {
        user = null;
        summary = null;
        currentPage = "dashboard";
        pagePayloads.clear();
    }

    public void putPagePayload(String pageKey, JSONObject payload) {
        if (pageKey == null || payload == null) return;
        pagePayloads.put(pageKey, payload);
        if ("dashboard".equals(pageKey)) {
            JSONObject nextSummary = payload.optJSONObject("summary");
            if (nextSummary != null) summary = nextSummary;
        }
    }

    public JSONObject pagePayload(String pageKey) {
        JSONObject payload = pagePayloads.get(pageKey);
        return payload != null ? payload : new JSONObject();
    }

    public JSONObject totals() {
        JSONObject totals = summary != null ? summary.optJSONObject("totals") : null;
        if (totals != null) return totals;

        JSONObject fallback = new JSONObject();
        try {
            fallback.put("paperKg", 0);
            fallback.put("briquettes", 0);
            fallback.put("contributors", 0);
            fallback.put("co2KgAvoided", 0);
        } catch (Exception ignored) {
        }
        return fallback;
    }
}
