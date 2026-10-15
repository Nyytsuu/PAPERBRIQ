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
    private static final String[] API_BASE_URLS = {
        "http://127.0.0.1:4173",
        "http://192.168.1.3:4173",
        "http://10.0.2.2:4173"
    };
    private static final int TIMEOUT_MS = 2500;

    public JSONObject getJson(String endpoint) throws Exception {
        Exception lastError = null;
        for (String baseUrl : API_BASE_URLS) {
            try {
                return getJsonFrom(baseUrl, endpoint);
            } catch (BackendResponseException error) {
                throw error;
            } catch (Exception error) {
                lastError = error;
            }
        }
        throw backendConnectionError(lastError);
    }

    public JSONObject postJson(String endpoint, JSONObject payload) throws Exception {
        Exception lastError = null;
        for (String baseUrl : API_BASE_URLS) {
            try {
                return postJsonTo(baseUrl, endpoint, payload);
            } catch (BackendResponseException error) {
                throw error;
            } catch (Exception error) {
                lastError = error;
            }
        }
        throw backendConnectionError(lastError);
    }

    public JSONObject putJson(String endpoint, JSONObject payload) throws Exception {
        Exception lastError = null;
        for (String baseUrl : API_BASE_URLS) {
            try {
                return writeJsonTo(baseUrl, endpoint, payload, "PUT");
            } catch (BackendResponseException error) {
                throw error;
            } catch (Exception error) {
                lastError = error;
            }
        }
        throw backendConnectionError(lastError);
    }

    private JSONObject getJsonFrom(String baseUrl, String endpoint) throws Exception {
        URL url = new URL(baseUrl + endpoint);
        HttpURLConnection connection = (HttpURLConnection) url.openConnection();
        connection.setRequestMethod("GET");
        connection.setRequestProperty("Accept", "application/json");
        connection.setConnectTimeout(TIMEOUT_MS);
        connection.setReadTimeout(TIMEOUT_MS);

        int status = connection.getResponseCode();
        String response = readStream(status >= 400 ? connection.getErrorStream() : connection.getInputStream());
        JSONObject json = response.isEmpty() ? new JSONObject() : new JSONObject(response);
        if (status >= 400) {
            throw new BackendResponseException(json.optString("error", "PaperBriq API request failed."));
        }
        return json;
    }

    private JSONObject postJsonTo(String baseUrl, String endpoint, JSONObject payload) throws Exception {
        return writeJsonTo(baseUrl, endpoint, payload, "POST");
    }

    private JSONObject writeJsonTo(String baseUrl, String endpoint, JSONObject payload, String method) throws Exception {
        URL url = new URL(baseUrl + endpoint);
        HttpURLConnection connection = (HttpURLConnection) url.openConnection();
        connection.setRequestMethod(method);
        connection.setRequestProperty("Content-Type", "application/json; charset=utf-8");
        connection.setRequestProperty("Accept", "application/json");
        connection.setDoOutput(true);
        connection.setConnectTimeout(TIMEOUT_MS);
        connection.setReadTimeout(TIMEOUT_MS);

        byte[] body = payload.toString().getBytes(StandardCharsets.UTF_8);
        try (OutputStream output = connection.getOutputStream()) {
            output.write(body);
        }

        int status = connection.getResponseCode();
        String response = readStream(status >= 400 ? connection.getErrorStream() : connection.getInputStream());
        JSONObject json = response.isEmpty() ? new JSONObject() : new JSONObject(response);
        if (status >= 400) {
            throw new BackendResponseException(json.optString("error", "PaperBriq API request failed."));
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

    private Exception backendConnectionError(Exception cause) {
        return new Exception(
            "Cannot connect to PaperBriq backend. Start npm with 'npm start', then run 'npm run android:reverse -- 12889374AD000249' for a USB phone.",
            cause
        );
    }

    private static class BackendResponseException extends Exception {
        BackendResponseException(String message) {
            super(message);
        }
    }
}
