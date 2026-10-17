package com.paperbriq.app.data;

import com.paperbriq.app.BuildConfig;

import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;

public class ApiClient {
    private static final String[] LOCAL_API_BASE_URLS = {
        "http://127.0.0.1:4173",
        "http://192.168.1.3:4173",
        "http://10.0.2.2:4173"
    };
    private static final int TIMEOUT_MS = 2500;
    private final List<String> apiBaseUrls;
    private final String configuredApiBaseUrl;

    public ApiClient() {
        List<String> urls = new ArrayList<>();
        String configuredUrl = String.valueOf(BuildConfig.PAPERBRIQ_API_BASE_URL).trim();
        if (!configuredUrl.isEmpty()) {
            configuredApiBaseUrl = configuredUrl.replaceAll("/+$", "");
            urls.add(configuredApiBaseUrl);
        } else {
            configuredApiBaseUrl = "";
            Collections.addAll(urls, LOCAL_API_BASE_URLS);
        }
        apiBaseUrls = Collections.unmodifiableList(urls);
    }

    public JSONObject getJson(String endpoint) throws Exception {
        return getJson(endpoint, Collections.emptyMap());
    }

    public JSONObject getJson(String endpoint, Map<String, String> headers) throws Exception {
        Exception lastError = null;
        for (String baseUrl : apiBaseUrls) {
            try {
                return getJsonFrom(baseUrl, endpoint, headers);
            } catch (BackendResponseException error) {
                throw error;
            } catch (Exception error) {
                lastError = error;
            }
        }
        throw backendConnectionError(lastError);
    }

    public JSONObject postJson(String endpoint, JSONObject payload) throws Exception {
        return postJson(endpoint, payload, Collections.emptyMap());
    }

    public JSONObject postJson(String endpoint, JSONObject payload, Map<String, String> headers) throws Exception {
        Exception lastError = null;
        for (String baseUrl : apiBaseUrls) {
            try {
                return postJsonTo(baseUrl, endpoint, payload, headers);
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
        for (String baseUrl : apiBaseUrls) {
            try {
                return writeJsonTo(baseUrl, endpoint, payload, "PUT", Collections.emptyMap());
            } catch (BackendResponseException error) {
                throw error;
            } catch (Exception error) {
                lastError = error;
            }
        }
        throw backendConnectionError(lastError);
    }

    private JSONObject getJsonFrom(String baseUrl, String endpoint, Map<String, String> headers) throws Exception {
        URL url = new URL(baseUrl + endpoint);
        HttpURLConnection connection = (HttpURLConnection) url.openConnection();
        connection.setRequestMethod("GET");
        connection.setRequestProperty("Accept", "application/json");
        applyHeaders(connection, headers);
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

    private JSONObject postJsonTo(String baseUrl, String endpoint, JSONObject payload, Map<String, String> headers) throws Exception {
        return writeJsonTo(baseUrl, endpoint, payload, "POST", headers);
    }

    private JSONObject writeJsonTo(String baseUrl, String endpoint, JSONObject payload, String method, Map<String, String> headers) throws Exception {
        URL url = new URL(baseUrl + endpoint);
        HttpURLConnection connection = (HttpURLConnection) url.openConnection();
        connection.setRequestMethod(method);
        connection.setRequestProperty("Content-Type", "application/json; charset=utf-8");
        connection.setRequestProperty("Accept", "application/json");
        applyHeaders(connection, headers);
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

    private void applyHeaders(HttpURLConnection connection, Map<String, String> headers) {
        if (headers == null) return;
        for (Map.Entry<String, String> entry : headers.entrySet()) {
            String name = String.valueOf(entry.getKey()).trim();
            String value = String.valueOf(entry.getValue()).trim();
            if (!name.isEmpty() && !value.isEmpty()) {
                connection.setRequestProperty(name, value);
            }
        }
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
        if (!configuredApiBaseUrl.isEmpty()) {
            return new Exception(
                "Cannot connect to the PaperBriq Render backend at " + configuredApiBaseUrl +
                    ". Check internet access and confirm /api/health is online.",
                cause
            );
        }
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
