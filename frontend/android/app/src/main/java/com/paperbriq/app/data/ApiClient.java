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
import java.util.HashMap;
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
    private volatile OperatorSession operatorSession;

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

    public boolean hasOperatorSession() {
        return operatorSession != null;
    }

    public String machineKeyOrigin() {
        return configuredApiBaseUrl;
    }

    public JSONObject getMachineKeyStatus(String endpoint, String key) throws Exception {
        return getJsonFrom(requireMachineKeyOrigin(), endpoint, Collections.singletonMap("X-Control-Key", key));
    }

    public JSONObject login(JSONObject payload, String enrollmentKey) throws Exception {
        if (enrollmentKey.isEmpty()) return postJson("/api/auth/login", payload);
        return postJsonTo(requireMachineKeyOrigin(), "/api/auth/login", payload,
            Collections.singletonMap("X-Control-Key", enrollmentKey));
    }

    public String temporaryMachineKey() {
        OperatorSession active = operatorSession;
        return active == null ? "" : active.cookie.substring("paperbriqOperator=".length());
    }

    private String requireMachineKeyOrigin() {
        if (!configuredApiBaseUrl.startsWith("https://")) {
            throw new IllegalStateException("Machine key setup requires the configured HTTPS backend.");
        }
        return configuredApiBaseUrl;
    }

    public JSONObject postOperatorJson(String endpoint, JSONObject payload, Map<String, String> headers) throws Exception {
        OperatorSession active = operatorSession;
        if (active == null) throw new IllegalStateException("Sign in again with an approved machine operator account.");
        Map<String, String> requestHeaders = new HashMap<>(headers);
        requestHeaders.put("Cookie", active.cookie);
        return postJsonTo(active.baseUrl, endpoint, payload, requestHeaders);
    }

    public Runnable endOperatorSession() {
        OperatorSession active = operatorSession;
        operatorSession = null;
        return () -> {
            if (active == null) return;
            try {
                postJsonTo(active.baseUrl, "/api/auth/logout", new JSONObject(), Collections.singletonMap("Cookie", active.cookie));
            } catch (Exception ignored) {
                // Local access is already cleared. The server session expires independently.
            }
        };
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
        if (headers.containsKey("X-Control-Key")) connection.setInstanceFollowRedirects(false);
        OperatorSession active = operatorSession;
        if (active != null && baseUrl.equals(active.baseUrl) && endpoint.startsWith("/api/device/control-status?")) {
            connection.setInstanceFollowRedirects(false);
            connection.setRequestProperty("Cookie", active.cookie);
        }
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
        boolean login = "/api/auth/login".equals(endpoint);
        if (login || headers.containsKey("Cookie") || headers.containsKey("X-Control-Key")) {
            connection.setInstanceFollowRedirects(false);
        }
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
        if (status < 200 || status >= 300) {
            throw new BackendResponseException(json.optString("error", "PaperBriq API request failed."));
        }
        if (login) {
            String header = connection.getHeaderField("Set-Cookie");
            String cookie = header == null ? "" : header.split(";", 2)[0];
            boolean secure = "https".equals(url.getProtocol()) ||
                ("http".equals(url.getProtocol()) && url.getHost().matches("localhost|127\\.0\\.0\\.1|10\\.0\\.2\\.2"));
            operatorSession = secure && cookie.matches("paperbriqOperator=[a-f0-9]{64}")
                ? new OperatorSession(baseUrl, cookie) : null;
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
        return new Exception(
            "Cannot update PaperBriq. Check your internet connection and try again.",
            cause
        );
    }

    private static class BackendResponseException extends Exception {
        BackendResponseException(String message) {
            super(message);
        }
    }

    private static final class OperatorSession {
        final String baseUrl;
        final String cookie;

        OperatorSession(String baseUrl, String cookie) {
            this.baseUrl = baseUrl;
            this.cookie = cookie;
        }
    }
}
