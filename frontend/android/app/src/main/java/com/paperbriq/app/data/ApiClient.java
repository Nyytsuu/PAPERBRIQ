package com.paperbriq.app.data;

import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;

public class ApiClient {
    private static final String API_BASE_URL = "http://10.0.2.2:4173";

    public JSONObject getJson(String endpoint) throws Exception {
        URL url = new URL(API_BASE_URL + endpoint);
        HttpURLConnection connection = (HttpURLConnection) url.openConnection();
        connection.setRequestMethod("GET");
        connection.setRequestProperty("Accept", "application/json");
        connection.setConnectTimeout(6000);
        connection.setReadTimeout(6000);

        int status = connection.getResponseCode();
        String response = readStream(status >= 400 ? connection.getErrorStream() : connection.getInputStream());
        JSONObject json = response.isEmpty() ? new JSONObject() : new JSONObject(response);
        if (status >= 400) {
            throw new Exception(json.optString("error", "PaperBriq API request failed."));
        }
        return json;
    }

    public JSONObject postJson(String endpoint, JSONObject payload) throws Exception {
        URL url = new URL(API_BASE_URL + endpoint);
        HttpURLConnection connection = (HttpURLConnection) url.openConnection();
        connection.setRequestMethod("POST");
        connection.setRequestProperty("Content-Type", "application/json; charset=utf-8");
        connection.setRequestProperty("Accept", "application/json");
        connection.setDoOutput(true);
        connection.setConnectTimeout(6000);
        connection.setReadTimeout(6000);

        byte[] body = payload.toString().getBytes(StandardCharsets.UTF_8);
        try (OutputStream output = connection.getOutputStream()) {
            output.write(body);
        }

        int status = connection.getResponseCode();
        String response = readStream(status >= 400 ? connection.getErrorStream() : connection.getInputStream());
        JSONObject json = response.isEmpty() ? new JSONObject() : new JSONObject(response);
        if (status >= 400) {
            throw new Exception(json.optString("error", "PaperBriq API request failed."));
        }
        return json;
    }

    private String readStream(InputStream stream) throws Exception {
        if (stream == null) return "";
        BufferedReader reader = new BufferedReader(new InputStreamReader(stream, StandardCharsets.UTF_8));
        StringBuilder builder = new StringBuilder();
        String lineText;
        while ((lineText = reader.readLine()) != null) {
            builder.append(lineText);
        }
        return builder.toString();
    }
}
