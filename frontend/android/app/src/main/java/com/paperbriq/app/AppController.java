package com.paperbriq.app;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.Manifest;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.graphics.Bitmap;
import android.graphics.Color;
import android.graphics.Typeface;
import android.os.Handler;
import android.os.Looper;
import android.os.Build;
import android.os.SystemClock;
import android.text.method.PasswordTransformationMethod;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import com.google.zxing.BarcodeFormat;
import com.google.zxing.WriterException;
import com.google.zxing.common.BitMatrix;
import com.google.zxing.qrcode.QRCodeWriter;
import com.google.mlkit.vision.barcode.common.Barcode;
import com.google.mlkit.vision.codescanner.GmsBarcodeScanner;
import com.google.mlkit.vision.codescanner.GmsBarcodeScannerOptions;
import com.google.mlkit.vision.codescanner.GmsBarcodeScanning;
import com.paperbriq.app.data.ApiClient;
import com.paperbriq.app.data.AppSession;
import com.paperbriq.app.data.MachineKeyStore;
import com.paperbriq.app.ui.AppShell;
import com.paperbriq.app.ui.LoginScreen;
import com.paperbriq.app.ui.Ui;

import org.json.JSONObject;
import org.json.JSONArray;

import java.io.File;
import java.io.FileOutputStream;
import java.io.FileWriter;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class AppController {
    private static final String PREFS_NAME = "paperbriq_session";
    private static final String PREF_USER = "user";
    private static final String PREF_SUMMARY = "summary";
    private static final long LIVE_UPDATE_MS = 5000L;
    private static final long MACHINE_CHECK_VALID_MS = 5 * 60 * 1000L;
    private static final String DEVICE_ID = "paperbriq-esp32";
    private static final String DEVICE_STATUS = "/api/device/control-status?deviceId=" + DEVICE_ID;
    private static final String EVENT_CHANNEL_ID = "paperbriq_machine_events";
    private static final String ERROR_CHANNEL_ID = "paperbriq_machine_errors";

    private final Activity activity;
    private final ApiClient apiClient;
    private final ExecutorService executor = Executors.newSingleThreadExecutor();
    private final AppSession session = new AppSession();
    private final Ui ui;
    private final SharedPreferences preferences;
    private final MachineKeyStore machineKeyStore;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private final Set<String> seenDeviceEventIds = new HashSet<>();
    private boolean deviceEventsInitialized = false;
    private boolean commandSubmitting = false;
    private boolean machineCheckRunning = false;
    private JSONObject machineChecks;
    private long machineChecksReceivedAt;
    private long machineCheckConfirmedAt;
    private boolean liveUpdatesStarted = false;
    private final Runnable liveUpdater = new Runnable() {
        @Override
        public void run() {
            if (!liveUpdatesStarted || session.user == null) return;
            String pageKey = session.currentPage;
            if (endpointFor(pageKey) != null) {
                loadPagePayload(pageKey, true);
            }
            mainHandler.postDelayed(this, LIVE_UPDATE_MS);
        }
    };

    public AppController(Activity activity) {
        this.activity = activity;
        this.apiClient = new ApiClient();
        this.ui = new Ui(activity);
        this.preferences = activity.getSharedPreferences(PREFS_NAME, Activity.MODE_PRIVATE);
        this.machineKeyStore = new MachineKeyStore(activity);
        createNotificationChannels();
    }

    public Activity activity() {
        return activity;
    }

    public Ui ui() {
        return ui;
    }

    public AppSession session() {
        return session;
    }

    public void start() {
        if (restoreSavedSession()) {
            showPage("dashboard");
            return;
        }
        showLogin();
    }

    public void showLogin() {
        stopLiveUpdates();
        activity.setContentView(new LoginScreen().create(this));
    }

    public void showPage(String pageKey) {
        session.currentPage = pageKey;
        renderPage(pageKey);
        loadPagePayload(pageKey, false);
        startLiveUpdates();
    }

    private void renderPage(String pageKey) {
        renderPage(pageKey, false);
    }

    private void renderPage(String pageKey, boolean preserveScroll) {
        ScrollView currentScroll = activity.findViewById(AppShell.CONTENT_SCROLL_ID);
        if (!preserveScroll || currentScroll == null) {
            activity.setContentView(new AppShell().create(this, pageKey));
            return;
        }

        int previousScrollY = currentScroll.getScrollY();
        View refreshedContent = new AppShell().createContent(this, pageKey);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) currentScroll.suppressLayout(true);
        currentScroll.removeAllViews();
        currentScroll.addView(refreshedContent);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) currentScroll.suppressLayout(false);
        currentScroll.post(() -> currentScroll.scrollTo(0, previousScrollY));
    }

    private void loadPagePayload(String pageKey, boolean silent) {
        String endpoint = endpointFor(pageKey);
        if (endpoint == null) return;

        executor.execute(() -> {
            try {
                JSONObject payload = apiClient.getJson(endpoint);
                JSONObject controlStatus = null;
                JSONObject backendHealth = null;
                JSONObject dashboardIot = null;
                long controlRequestedAt = SystemClock.elapsedRealtime();
                try {
                    backendHealth = apiClient.getJson("/api/health");
                } catch (Exception ignored) {
                    backendHealth = new JSONObject();
                }
                // Monitoring is read-only and must also work for non-operator accounts.
                {
                    try {
                        controlRequestedAt = SystemClock.elapsedRealtime();
                        controlStatus = getMachineControlStatus();
                    } catch (Exception controlError) {
                        controlStatus = new JSONObject();
                        controlStatus.put("enabled", false);
                        controlStatus.put("error", controlError.getMessage());
                    }
                }
                if ("dashboard".equals(pageKey)) {
                    try {
                        dashboardIot = apiClient.getJson("/api/iot");
                    } catch (Exception ignored) {
                        dashboardIot = new JSONObject();
                    }
                }
                session.putPagePayload(pageKey, payload);
                session.putPagePayload("backendHealth", backendHealth);
                if (controlStatus != null) {
                    session.putPagePayload("deviceControl", controlStatus);
                }
                if (dashboardIot != null) {
                    session.putPagePayload("iot", dashboardIot);
                }
                final JSONObject nextControlStatus = controlStatus;
                final long requestedAt = controlRequestedAt;
                activity.runOnUiThread(() -> {
                    if (nextControlStatus != null) {
                        acceptMachineChecks(nextControlStatus, requestedAt);
                        processDeviceEvents(nextControlStatus);
                    }
                    if (pageKey.equals(session.currentPage)) {
                        renderPage(pageKey, true);
                    }
                });
            } catch (Exception error) {
                activity.runOnUiThread(() -> {
                    machineChecks = null;
                    clearMachineCheck();
                    if ("process".equals(session.currentPage)) renderPage("process", true);
                    if (!silent && pageKey.equals(session.currentPage)) {
                        Toast.makeText(activity, error.getMessage(), Toast.LENGTH_LONG).show();
                    }
                });
            }
        });
    }

    private String endpointFor(String pageKey) {
        switch (pageKey) {
            case "dashboard":
                return "/api/dashboard";
            case "process":
                return "/api/process";
            case "iot":
                return "/api/iot";
            case "contributions":
                return "/api/contributions";
            case "rewards":
                return "/api/rewards";
            case "reports":
                return "/api/reports";
            case "scan":
                return "/api/qr-scans";
            case "settings":
                return "/api/settings";
            default:
                return null;
        }
    }

    public void login(String email, String password, String role, boolean keepLoggedIn, Button loginButton, TextView messageView) {
        if (email.trim().isEmpty() || password.isEmpty()) {
            messageView.setText("Email and password are required.");
            return;
        }

        loginButton.setEnabled(false);
        loginButton.setText("Signing in");
        messageView.setText("");

        executor.execute(() -> {
            try {
                JSONObject payload = new JSONObject();
                payload.put("email", email.trim());
                payload.put("password", password);
                if (role != null && !role.trim().isEmpty()) {
                    payload.put("role", role);
                }
                JSONObject response = apiClient.login(payload, machineKeyStore.read(apiClient.machineKeyOrigin()));
                session.user = response.optJSONObject("user");
                session.summary = response.optJSONObject("summary");
                if (keepLoggedIn) {
                    saveSession();
                } else {
                    clearSavedSession();
                }
                activity.runOnUiThread(() -> {
                    loginButton.setEnabled(true);
                    loginButton.setText("Log In");
                    showPage("dashboard");
                });
            } catch (Exception error) {
                activity.runOnUiThread(() -> {
                    loginButton.setEnabled(true);
                    loginButton.setText("Log In");
                    messageView.setText(error.getMessage());
                });
            }
        });
    }

    public void logout() {
        clearSavedSession();
        session.clear();
        executor.execute(apiClient.endOperatorSession());
        seenDeviceEventIds.clear();
        clearMachineCheck();
        machineChecks = null;
        deviceEventsInitialized = false;
        Toast.makeText(activity, "Logged out.", Toast.LENGTH_SHORT).show();
        showLogin();
    }

    public void showSignupDialog() {
        EditText name = ui.editText("Full name", false);
        EditText email = ui.editText("Email address", false);
        EditText barangay = ui.editText("Barangay or community", false);
        EditText password = ui.editText("Password", true);
        EditText confirmPassword = ui.editText("Confirm password", true);

        LinearLayout form = ui.column();
        form.setPadding(ui.dp(4), ui.dp(8), ui.dp(4), 0);
        form.addView(name, ui.fullWidth(ui.dp(52), 0, ui.dp(8), 0, ui.dp(8)));
        form.addView(email, ui.fullWidth(ui.dp(52), 0, 0, 0, ui.dp(8)));
        form.addView(barangay, ui.fullWidth(ui.dp(52), 0, 0, 0, ui.dp(8)));
        form.addView(password, ui.fullWidth(ui.dp(52), 0, 0, 0, ui.dp(8)));
        form.addView(confirmPassword, ui.fullWidth(ui.dp(52), 0, 0, 0, 0));

        AlertDialog dialog = new AlertDialog.Builder(activity)
            .setTitle("Create User Account")
            .setView(form)
            .setNegativeButton("Close", null)
            .setPositiveButton("Create", (clickedDialog, which) -> signup(
                name.getText().toString(),
                email.getText().toString(),
                barangay.getText().toString(),
                password.getText().toString(),
                confirmPassword.getText().toString()
            ))
            .create();
        dialog.setOnShowListener(openDialog -> styleDialogButtons(dialog));
        dialog.show();
    }

    public void showForgotPasswordDialog(String defaultEmail, String role) {
        EditText email = ui.editText("Email address", false);
        email.setText(String.valueOf(defaultEmail == null ? "" : defaultEmail).trim());
        TextView message = ui.text("Gmail delivery can be connected later.", 13, AppColors.MUTED, android.graphics.Typeface.BOLD);

        LinearLayout form = ui.column();
        form.setPadding(ui.dp(4), ui.dp(8), ui.dp(4), 0);
        form.addView(email, ui.fullWidth(ui.dp(52), 0, 0, 0, ui.dp(10)));
        form.addView(message);

        AlertDialog dialog = new AlertDialog.Builder(activity)
            .setTitle("Forgot password?")
            .setMessage("Enter your PaperBriq account email and we will prepare reset instructions.")
            .setView(form)
            .setNegativeButton("Close", null)
            .setPositiveButton("Send reset", null)
            .create();

        dialog.setOnShowListener(openDialog -> {
            styleDialogButtons(dialog);
            Button sendButton = dialog.getButton(AlertDialog.BUTTON_POSITIVE);
            sendButton.setOnClickListener(v -> {
                String cleanEmail = email.getText().toString().trim();
                if (cleanEmail.isEmpty()) {
                    message.setTextColor(AppColors.RED);
                    message.setText("Enter your email address first.");
                    return;
                }

                sendButton.setEnabled(false);
                message.setTextColor(AppColors.MUTED);
                message.setText("Sending reset request...");

                executor.execute(() -> {
                    try {
                        JSONObject payload = new JSONObject();
                        payload.put("email", cleanEmail);
                        payload.put("role", role);
                        JSONObject response = apiClient.postJson("/api/auth/forgot-password", payload);
                        activity.runOnUiThread(() -> {
                            sendButton.setEnabled(true);
                            message.setTextColor(AppColors.GREEN);
                            message.setText(response.optString("message", "Reset request sent."));
                        });
                    } catch (Exception error) {
                        activity.runOnUiThread(() -> {
                            sendButton.setEnabled(true);
                            message.setTextColor(AppColors.RED);
                            message.setText(error.getMessage());
                        });
                    }
                });
            });
        });

        dialog.show();
    }

    private void signup(String name, String email, String barangay, String password, String confirmPassword) {
        if (name.trim().isEmpty() || email.trim().isEmpty() || barangay.trim().isEmpty() || password.isEmpty()) {
            Toast.makeText(activity, "Name, email, barangay, and password are required.", Toast.LENGTH_LONG).show();
            return;
        }

        if (!password.equals(confirmPassword)) {
            Toast.makeText(activity, "Passwords do not match.", Toast.LENGTH_LONG).show();
            return;
        }

        executor.execute(() -> {
            try {
                JSONObject payload = new JSONObject();
                payload.put("fullName", name.trim());
                payload.put("email", email.trim());
                payload.put("barangay", barangay.trim());
                payload.put("password", password);
                payload.put("role", "contributor");
                JSONObject response = apiClient.postJson("/api/auth/signup", payload);
                session.user = response.optJSONObject("user");
                session.summary = response.optJSONObject("summary");
                activity.runOnUiThread(() -> {
                    Toast.makeText(activity, "Account created.", Toast.LENGTH_SHORT).show();
                    showPage("dashboard");
                });
            } catch (Exception error) {
                activity.runOnUiThread(() -> Toast.makeText(activity, error.getMessage(), Toast.LENGTH_LONG).show());
            }
        });
    }

    public boolean canControlMachine() {
        if (session.user == null) return false;
        String role = session.user.optString("role", "");
        return "admin".equals(role) || "barangay_staff".equals(role);
    }

    public boolean hasMachineAccess() {
        return apiClient.hasOperatorSession() &&
            session.pagePayload("deviceControl").optBoolean("operatorAuthorized");
    }

    public boolean isDemoAdmin() {
        JSONObject user = session.user;
        return user != null && "demo-admin".equals(user.optString("id")) && "admin".equals(user.optString("role"));
    }

    private JSONObject getMachineControlStatus() throws Exception {
        return apiClient.getJson(DEVICE_STATUS);
    }

    private void acceptMachineChecks(JSONObject control, long requestedAt) {
        machineChecks = control.optJSONObject("machineChecks");
        // Count request time as part of the result's age, not additional validity.
        machineChecksReceivedAt = requestedAt;
    }

    public JSONObject machineCheckResults() { return machineChecks; }

    public boolean isMachineCheckRunning() { return machineCheckRunning; }

    public boolean automaticMachineChecksReady() {
        long now = SystemClock.elapsedRealtime();
        return machineCheckConfirmedAt > 0 && now - machineCheckConfirmedAt < MACHINE_CHECK_VALID_MS
            && machineChecks != null && machineChecks.optBoolean("passed", false)
            && now - machineChecksReceivedAt < machineChecks.optLong("validForMs", 0);
    }

    public void checkMachine(Runnable showResults) {
        if (!canControlMachine() || machineCheckRunning) return;
        machineCheckRunning = true;
        machineCheckConfirmedAt = 0;
        machineChecks = null;
        if ("process".equals(session.currentPage)) renderPage("process", true);
        executor.execute(() -> {
            long requestedAt = SystemClock.elapsedRealtime();
            try {
                JSONObject control = getMachineControlStatus();
                activity.runOnUiThread(() -> {
                    machineCheckRunning = false;
                    acceptMachineChecks(control, requestedAt);
                    machineCheckConfirmedAt = SystemClock.elapsedRealtime();
                    session.putPagePayload("deviceControl", control);
                    if ("process".equals(session.currentPage)) {
                        renderPage("process", true);
                        showResults.run();
                    }
                });
            } catch (Exception error) {
                activity.runOnUiThread(() -> {
                    machineCheckRunning = false;
                    machineChecks = null;
                    clearMachineCheck();
                    if ("process".equals(session.currentPage)) {
                        renderPage("process", true);
                        showResults.run();
                    }
                });
            }
        });
    }

    private void clearMachineCheck() {
        machineCheckConfirmedAt = 0;
    }

    public void signInForMachineAccess() {
        if (isDemoAdmin()) {
            JSONObject status = session.pagePayload("deviceControl");
            if (apiClient.hasOperatorSession()) {
                showDemoMachineSession();
            } else if (status.has("enabled") && !status.optBoolean("temporaryMachineKeysSupported")) {
                showInfoDialog("Backend update needed",
                    "Render is running a version that does not issue temporary machine keys. Deploy the updated PaperBriq backend, then sign out and back in.");
            } else if (machineKeyStore.hasSavedApproval()) {
                new AlertDialog.Builder(activity)
                    .setTitle("Temporary key unavailable")
                    .setMessage("This phone has saved approval, but the server did not issue a temporary key for this sign-in. Sign in again, or recheck the phone approval if the Render key changed.")
                    .setPositiveButton("Sign in again", (dialog, which) -> logout())
                    .setNeutralButton("Check approval", (dialog, which) -> showDemoMachineKeyDialog())
                    .setNegativeButton("Close", null)
                    .show();
            } else {
                showDemoMachineKeyDialog();
            }
            return;
        }
        confirmAction("Sign in again?", "Use an approved operator account to control PaperBriq.", this::logout);
    }

    private void showDemoMachineSession() {
        LinearLayout details = ui.column();
        details.setPadding(ui.dp(20), 0, ui.dp(20), ui.dp(12));
        details.addView(ui.text("Session code", 14, AppColors.MUTED, Typeface.NORMAL), ui.matchWrap());
        TextView code = ui.text(apiClient.machineSessionCode(), 28, AppColors.INK, Typeface.BOLD);
        code.setTypeface(Typeface.MONOSPACE);
        details.addView(code, ui.matchWrap());
        new AlertDialog.Builder(activity)
            .setTitle(hasMachineAccess() ? "Machine access active" : "Machine access")
            .setMessage("This code identifies your sign-in. PaperBriq authorizes commands automatically.")
            .setView(details)
            .setNeutralButton("Forget phone", (ignored, which) -> {
                machineKeyStore.clear();
                logout();
            })
            .setPositiveButton("Close", null)
            .show();
    }

    private void showDemoMachineKeyDialog() {
        EditText input = ui.editText("Phone approval key", true);
        CheckBox showKey = new CheckBox(activity);
        showKey.setText("Show key");
        showKey.setOnCheckedChangeListener((view, checked) -> {
            input.setTransformationMethod(checked ? null : PasswordTransformationMethod.getInstance());
            input.setSelection(input.length());
        });
        LinearLayout form = ui.column();
        form.setPadding(ui.dp(20), 0, ui.dp(20), 0);
        form.addView(input, ui.fullWidth(ui.dp(52), 0, ui.dp(8), 0, 0));
        form.addView(showKey, ui.matchWrap());
        AlertDialog dialog = new AlertDialog.Builder(activity)
            .setTitle("Machine access")
            .setMessage("Approve this phone once with the CONTROL_API_KEY from Render. Then sign in to get a temporary machine key.")
            .setView(form)
            .setPositiveButton("Save key", null)
            .setNeutralButton("Forget key", (ignored, which) -> {
                machineKeyStore.clear();
                logout();
            })
            .setNegativeButton("Cancel", null)
            .create();
        dialog.setOnShowListener(ignored -> {
            styleDialogButtons(dialog);
            Button save = dialog.getButton(AlertDialog.BUTTON_POSITIVE);
            save.setOnClickListener(view -> {
                String key = input.getText().toString().trim();
                if (key.isEmpty()) {
                    input.setError("Enter the machine key.");
                    return;
                }
                if (key.chars().anyMatch(character -> character < 32 || character == 127)) {
                    input.setError("Enter the machine key on one line.");
                    return;
                }
                save.setEnabled(false);
                showKey.setChecked(false);
                showKey.setEnabled(false);
                dialog.setCancelable(false);
                dialog.getButton(AlertDialog.BUTTON_NEUTRAL).setEnabled(false);
                dialog.getButton(AlertDialog.BUTTON_NEGATIVE).setEnabled(false);
                executor.execute(() -> {
                    try {
                        JSONObject status = apiClient.getMachineKeyStatus(DEVICE_STATUS, key);
                        if (!status.optBoolean("temporaryMachineKeysSupported")) {
                            throw new IllegalStateException("Deploy the updated backend before saving the machine key.");
                        }
                        if (!status.optBoolean("enabled")) {
                            throw new IllegalStateException("Enable remote control and set CONTROL_API_KEY on Render.");
                        }
                        if (!status.optBoolean("machineKeyAuthorized")) {
                            throw new IllegalStateException("The machine key did not match Render.");
                        }
                        machineKeyStore.save(apiClient.machineKeyOrigin(), key);
                        activity.runOnUiThread(() -> {
                            if (!isDemoAdmin()) return;
                            dialog.dismiss();
                            confirmAction("Phone approved", "Sign in again to activate your temporary machine key.", this::logout);
                        });
                    } catch (Exception error) {
                        activity.runOnUiThread(() -> {
                            input.setError(error.getMessage());
                            showKey.setEnabled(true);
                            save.setEnabled(true);
                            dialog.setCancelable(true);
                            dialog.getButton(AlertDialog.BUTTON_NEUTRAL).setEnabled(true);
                            dialog.getButton(AlertDialog.BUTTON_NEGATIVE).setEnabled(true);
                        });
                    }
                });
            });
        });
        dialog.show();
        if (dialog.getWindow() != null) dialog.getWindow().addFlags(WindowManager.LayoutParams.FLAG_SECURE);
    }

    public void sendDeviceCommand(String command) {
        sendDeviceCommand(command, 0);
    }

    public int productionSeconds(String action) {
        JSONObject controls = session.pagePayload("deviceControl").optJSONObject("productionControls");
        JSONObject settings = controls == null ? null : controls.optJSONObject("settings");
        return settings == null ? 0 : settings.optInt(action + "Seconds", 0);
    }

    public void saveProductionSeconds(String action, int seconds) {
        if (!canControlMachine() || !"drying".equals(action)
                || seconds < 0 || seconds > Integer.MAX_VALUE / 1000) return;
        if (!hasMachineAccess()) { signInForMachineAccess(); return; }
        executor.execute(() -> {
            try {
                JSONObject latest = getMachineControlStatus();
                JSONObject controls = latest.optJSONObject("productionControls");
                JSONObject previous = controls == null ? null : controls.optJSONObject("settings");
                if (previous == null) throw new IllegalStateException("Update the server before saving process settings.");
                JSONObject body = new JSONObject();
                body.put("deviceId", DEVICE_ID);
                body.put("dryingSeconds", seconds > 0 ? seconds : JSONObject.NULL);
                apiClient.postOperatorJson("/api/device/production-settings", body, java.util.Collections.emptyMap());
                activity.runOnUiThread(() -> {
                    Toast.makeText(activity, "Process time saved. Use the action button to apply it to the machine.", Toast.LENGTH_LONG).show();
                    loadPagePayload("process", true);
                });
            } catch (Exception error) {
                activity.runOnUiThread(() -> Toast.makeText(activity, error.getMessage(), Toast.LENGTH_LONG).show());
            }
        });
    }

    public void sendProductionCommand(String action, int seconds, String runId) {
        sendDeviceCommand(action, seconds, runId);
    }

    private void sendDeviceCommand(String command, int durationSeconds) {
        sendDeviceCommand(command, durationSeconds, "");
    }

    private void sendDeviceCommand(String command, int durationSeconds, String runId) {
        String normalized = String.valueOf(command).trim().toLowerCase(Locale.ROOT);
        boolean production = "pump_arm".equals(normalized) || "drying_start".equals(normalized)
            || "drying_time".equals(normalized) || "preheat_start".equals(normalized);
        boolean timed = "heater".equals(normalized) || "pump".equals(normalized);
        if (timed && (durationSeconds < 1 || durationSeconds > ("heater".equals(normalized) ? 10 : 3))) {
            Toast.makeText(activity, "Choose heater 1–10 seconds or pump 1–3 seconds.", Toast.LENGTH_LONG).show(); return;
        }
        if (!canControlMachine()) {
            Toast.makeText(activity, "Only administrators and barangay staff can control the machine.", Toast.LENGTH_LONG).show();
            return;
        }
        if (!("start".equals(normalized) || "stop".equals(normalized) || "reset".equals(normalized) || "self_test".equals(normalized) || timed || production)) {
            Toast.makeText(activity, "Unsupported machine command.", Toast.LENGTH_LONG).show();
            return;
        }
        if (("start".equals(normalized) || "self_test".equals(normalized)) && !automaticMachineChecksReady()) {
            Toast.makeText(activity, "Use Check machine and resolve failed connection or sensor checks before starting.", Toast.LENGTH_LONG).show();
            return;
        }
        if (!hasMachineAccess()) {
            signInForMachineAccess();
            return;
        }
        if (commandSubmitting) {
            Toast.makeText(activity, "A machine command is already being submitted.", Toast.LENGTH_SHORT).show();
            return;
        }

        commandSubmitting = true;
        executor.execute(() -> {
            try {
                if (timed) {
                    JSONObject latest = getMachineControlStatus();
                    JSONObject controls = latest.optJSONObject("timedControls");
                    JSONObject output = controls == null ? null : controls.optJSONObject(normalized);
                    if (!latest.optBoolean("enabled", false) || output == null || !output.optBoolean("available", false)) {
                        throw new IllegalStateException(output == null ? "Update and reconnect PaperBriq before starting."
                            : output.optString("reason", "Machine controls unavailable."));
                    }
                }
                if ("start".equals(normalized) || "self_test".equals(normalized)) {
                    JSONObject latest = getMachineControlStatus();
                    JSONObject checks = latest.optJSONObject("machineChecks");
                    if (!latest.optBoolean("enabled", false) || checks == null || !checks.optBoolean("passed", false)
                        || checks.optLong("validForMs", 0) <= 0
                        || ("self_test".equals(normalized) && !checks.optBoolean("componentTestsSupported", false))) {
                        activity.runOnUiThread(() -> {
                            machineChecks = null;
                            if ("process".equals(session.currentPage)) renderPage("process", true);
                        });
                        throw new IllegalStateException("Machine checks need attention. Use Check machine again before starting.");
                    }
                }
                JSONObject payload = new JSONObject();
                payload.put("deviceId", DEVICE_ID);
                payload.put("command", normalized);
                if (production && !"pump_arm".equals(normalized)) { payload.put("durationSeconds", durationSeconds); payload.put("runId", runId); }
                if (timed) { payload.put("durationSeconds", durationSeconds); payload.put("supervised", true); }
                if ("self_test".equals(normalized)) payload.put("unloadedSupervised", true);
                payload.put("source", "android");
                if (session.user != null) {
                    payload.put("issuedBy", session.user.optString("email", session.user.optString("fullName", "android-operator")));
                }
                Map<String, String> headers = new HashMap<>();
                headers.put("Idempotency-Key", java.util.UUID.randomUUID().toString());
                JSONObject response = apiClient.postOperatorJson("/api/device/commands", payload, headers);
                JSONObject queued = response.optJSONObject("command");
                String status = queued != null ? queued.optString("status", "pending") : "pending";
                activity.runOnUiThread(() -> {
                    commandSubmitting = false;
                    clearMachineCheck();
                    String label = "stop".equals(normalized) ? "Software Stop" : "self_test".equals(normalized) ? "Component tests"
                        : "pump_arm".equals(normalized) ? "Pump cycle" : "drying_start".equals(normalized) ? "Drying" : "preheat_start".equals(normalized) ? "Preheating"
                        : normalized.endsWith("_time") ? "Time change" : capitalize(normalized);
                    Toast.makeText(activity, label + " queued (" + status + "). Waiting for the device acknowledgement.", Toast.LENGTH_LONG).show();
                    loadPagePayload("process", true);
                });
            } catch (Exception error) {
                activity.runOnUiThread(() -> {
                    commandSubmitting = false;
                    Toast.makeText(activity, error.getMessage(), Toast.LENGTH_LONG).show();
                });
            }
        });
    }

    public void confirmAction(String title, String message, Runnable confirmedAction) {
        AlertDialog dialog = new AlertDialog.Builder(activity)
            .setTitle(title)
            .setMessage(message)
            .setNegativeButton("Cancel", null)
            .setPositiveButton("Confirm", (clickedDialog, which) -> {
                if (confirmedAction != null) confirmedAction.run();
            })
            .create();
        dialog.setOnShowListener(openDialog -> styleDialogButtons(dialog));
        dialog.show();
    }

    public void showRewardDetails(JSONObject reward) {
        if (reward == null) reward = new JSONObject();
        String name = reward.optString("name", "Reward");
        int stock = reward.optInt("stockQuantity", 0);
        LinearLayout body = ui.column();
        body.setPadding(ui.dp(4), ui.dp(8), ui.dp(4), 0);
        body.addView(ui.infoRow("Points", ui.formatNumber(reward.optDouble("pointsCost", 0)) + " pts"));
        body.addView(ui.infoRow("Stock", ui.formatNumber(stock)));
        body.addView(ui.infoRow("Category", reward.optString("category", "Reward")));
        String description = reward.optString("description", "No details added yet.");
        TextView details = ui.text(description, 14, AppColors.MUTED, Typeface.BOLD);
        details.setPadding(0, ui.dp(10), 0, 0);
        body.addView(details, ui.matchWrap());

        AlertDialog dialog = new AlertDialog.Builder(activity)
            .setTitle(name)
            .setMessage(stock > 0 ? "Redeem this reward using your available points?" : "This reward is currently out of stock.")
            .setView(body)
            .setNegativeButton("Close", null)
            .setPositiveButton("Redeem", null)
            .create();

        JSONObject selectedReward = reward;
        dialog.setOnShowListener(openDialog -> {
            styleDialogButtons(dialog);
            Button redeem = dialog.getButton(AlertDialog.BUTTON_POSITIVE);
            if (stock <= 0) {
                redeem.setEnabled(false);
                redeem.setText("Out of stock");
                return;
            }
            redeem.setOnClickListener(v -> redeemReward(selectedReward, dialog, redeem));
        });
        dialog.show();
    }

    private void redeemReward(JSONObject reward, AlertDialog dialog, Button redeemButton) {
        String rewardId = reward.optString("id", "");
        if (rewardId.trim().isEmpty()) {
            Toast.makeText(activity, "Reward ID is missing.", Toast.LENGTH_LONG).show();
            return;
        }

        redeemButton.setEnabled(false);
        redeemButton.setText("Redeeming");
        executor.execute(() -> {
            try {
                JSONObject payload = new JSONObject();
                payload.put("rewardId", rewardId);
                if (session.user != null) {
                    payload.put("email", session.user.optString("email", ""));
                    payload.put("role", session.user.optString("role", "contributor"));
                    payload.put("contributorName", session.user.optString("fullName", ""));
                }
                JSONObject response = apiClient.postJson("/api/rewards/redeem", payload);
                activity.runOnUiThread(() -> {
                    dialog.dismiss();
                    JSONObject redemption = response.optJSONObject("redemption");
                    String label = redemption != null ? redemption.optString("rewardName", reward.optString("name", "Reward")) : reward.optString("name", "Reward");
                    Toast.makeText(activity, label + " redeemed.", Toast.LENGTH_LONG).show();
                    showPage("rewards");
                });
            } catch (Exception error) {
                activity.runOnUiThread(() -> {
                    redeemButton.setEnabled(true);
                    redeemButton.setText("Redeem");
                    Toast.makeText(activity, error.getMessage(), Toast.LENGTH_LONG).show();
                });
            }
        });
    }

    public void showInfoDialog(String title, String message) {
        AlertDialog dialog = new AlertDialog.Builder(activity)
            .setTitle(title)
            .setMessage(message)
            .setPositiveButton("Done", null)
            .create();
        dialog.setOnShowListener(openDialog -> styleDialogButtons(dialog));
        dialog.show();
    }

    public void showUserQrDialog(String code, String contributorName) {
        String cleanCode = safeQrCode(code);
        LinearLayout body = ui.column();
        body.setGravity(Gravity.CENTER_HORIZONTAL);
        body.setPadding(ui.dp(6), ui.dp(8), ui.dp(6), 0);

        body.addView(buildQrPreview(cleanCode), new LinearLayout.LayoutParams(ui.dp(220), ui.dp(220)));
        TextView codeView = ui.text(cleanCode, 15, AppColors.GREEN_DARK, Typeface.BOLD);
        codeView.setGravity(Gravity.CENTER);
        codeView.setPadding(0, ui.dp(12), 0, 0);
        body.addView(codeView, ui.matchWrap());

        TextView note = ui.text("Show this to barangay staff when donating paper. Staff must scan and confirm the weight before points are added.", 13, AppColors.MUTED, Typeface.NORMAL);
        note.setGravity(Gravity.CENTER);
        note.setPadding(0, ui.dp(10), 0, 0);
        body.addView(note, ui.matchWrap());

        AlertDialog dialog = new AlertDialog.Builder(activity)
            .setTitle(String.valueOf(contributorName == null || contributorName.trim().isEmpty() ? "My QR Code" : contributorName))
            .setView(body)
            .setNegativeButton("Close", null)
            .setPositiveButton("Download", (clickedDialog, which) -> downloadUserQr(cleanCode))
            .create();
        dialog.setOnShowListener(openDialog -> styleDialogButtons(dialog));
        dialog.show();
    }

    public void downloadUserQr(String code) {
        String cleanCode = safeQrCode(code);
        executor.execute(() -> {
            try {
                File directory = activity.getExternalFilesDir(null);
                if (directory == null) directory = activity.getFilesDir();
                File imageFile = new File(directory, "paperbriq-user-qr.png");
                Bitmap qrBitmap = createQrBitmap(cleanCode, 720);
                try (FileOutputStream output = new FileOutputStream(imageFile, false)) {
                    qrBitmap.compress(Bitmap.CompressFormat.PNG, 100, output);
                }
                File textFile = new File(directory, "paperbriq-user-qr.txt");
                try (FileWriter writer = new FileWriter(textFile, false)) {
                    writer.write("PaperBriq Contributor QR\n");
                    writer.write(cleanCode);
                    writer.write("\n\nPNG QR image: ");
                    writer.write(imageFile.getAbsolutePath());
                    writer.write("\nShow this code to barangay staff when donating paper waste.\n");
                }
                String path = imageFile.getAbsolutePath();
                activity.runOnUiThread(() -> Toast.makeText(activity, "QR record saved: " + path, Toast.LENGTH_LONG).show());
            } catch (Exception error) {
                activity.runOnUiThread(() -> Toast.makeText(activity, "Unable to save QR record.", Toast.LENGTH_LONG).show());
            }
        });
    }

    public void createManualQrScan(String code) {
        String cleanCode = code.trim();
        if (cleanCode.isEmpty()) {
            Toast.makeText(activity, "Enter a contributor code first.", Toast.LENGTH_SHORT).show();
            return;
        }
        showQrDonationDialog(cleanCode);
    }

    public void startQrScanner() {
        try {
            GmsBarcodeScannerOptions options = new GmsBarcodeScannerOptions.Builder()
                .setBarcodeFormats(Barcode.FORMAT_QR_CODE)
                .enableAutoZoom()
                .build();
            GmsBarcodeScanner scanner = GmsBarcodeScanning.getClient(activity, options);
            Toast.makeText(activity, "Opening QR scanner...", Toast.LENGTH_SHORT).show();
            scanner.startScan()
                .addOnSuccessListener(barcode -> {
                    String rawValue = barcode.getRawValue();
                    if (rawValue == null || rawValue.trim().isEmpty()) {
                        Toast.makeText(activity, "No QR code value found.", Toast.LENGTH_LONG).show();
                        return;
                    }
                    showQrDonationDialog(rawValue.trim());
                })
                .addOnCanceledListener(() -> Toast.makeText(activity, "QR scan cancelled.", Toast.LENGTH_SHORT).show())
                .addOnFailureListener(error -> {
                    Toast.makeText(activity, "Scanner unavailable. Use manual entry.", Toast.LENGTH_LONG).show();
                    showQrDonationDialog("");
                });
        } catch (Exception error) {
            Toast.makeText(activity, "Scanner unavailable. Use manual entry.", Toast.LENGTH_LONG).show();
            showQrDonationDialog("");
        }
    }

    public void showQrDonationDialog(String scannedCode) {
        EditText code = ui.editText("Contributor QR code", false);
        code.setText(String.valueOf(scannedCode == null ? "" : scannedCode).trim());
        EditText contributor = ui.editText("Contributor name", false);
        EditText barangay = ui.editText("Barangay", false);
        EditText weight = ui.editText("Paper weight in kg", false);
        weight.setInputType(android.text.InputType.TYPE_CLASS_NUMBER | android.text.InputType.TYPE_NUMBER_FLAG_DECIMAL);

        JSONObject user = session.user != null ? session.user : new JSONObject();
        barangay.setText(user.optString("barangay", ""));
        TextView message = ui.text("Points are calculated automatically after staff confirms the paper weight.", 13, AppColors.MUTED, Typeface.BOLD);

        LinearLayout form = ui.column();
        form.setPadding(ui.dp(4), ui.dp(8), ui.dp(4), 0);
        form.addView(code, ui.fullWidth(ui.dp(52), 0, 0, 0, ui.dp(8)));
        form.addView(contributor, ui.fullWidth(ui.dp(52), 0, 0, 0, ui.dp(8)));
        form.addView(barangay, ui.fullWidth(ui.dp(52), 0, 0, 0, ui.dp(8)));
        form.addView(weight, ui.fullWidth(ui.dp(52), 0, 0, 0, ui.dp(10)));
        form.addView(message);

        AlertDialog dialog = new AlertDialog.Builder(activity)
            .setTitle("Confirm paper donation")
            .setMessage("Staff must confirm the contributor QR and paper weight before points are recorded.")
            .setView(form)
            .setNegativeButton("Cancel", null)
            .setPositiveButton("Record donation", null)
            .create();

        dialog.setOnShowListener(openDialog -> {
            styleDialogButtons(dialog);
            Button record = dialog.getButton(AlertDialog.BUTTON_POSITIVE);
            record.setOnClickListener(v -> {
                String cleanCode = code.getText().toString().trim();
                String cleanWeight = weight.getText().toString().trim();
                double weightKg;

                try {
                    weightKg = Double.parseDouble(cleanWeight);
                } catch (Exception ignored) {
                    weightKg = 0;
                }

                if (cleanCode.isEmpty()) {
                    message.setTextColor(AppColors.RED);
                    message.setText("Contributor QR code is required.");
                    return;
                }

                if (weightKg <= 0) {
                    message.setTextColor(AppColors.RED);
                    message.setText("Enter the donated paper weight in kilograms.");
                    return;
                }

                record.setEnabled(false);
                message.setTextColor(AppColors.MUTED);
                message.setText("Recording donation...");
                recordQrDonation(
                    cleanCode,
                    contributor.getText().toString(),
                    barangay.getText().toString(),
                    weightKg,
                    dialog,
                    record,
                    message
                );
            });
        });

        dialog.show();
    }

    private void recordQrDonation(
        String code,
        String contributor,
        String barangay,
        double weightKg,
        AlertDialog dialog,
        Button recordButton,
        TextView messageView
    ) {
        String cleanCode = code.trim();
        String cleanContributor = contributor == null || contributor.trim().isEmpty() ? cleanCode : contributor.trim();
        String cleanBarangay = barangay == null || barangay.trim().isEmpty() ? "Unassigned" : barangay.trim();

        executor.execute(() -> {
            try {
                JSONObject payload = new JSONObject();
                payload.put("code", cleanCode);
                payload.put("contributor", cleanContributor);
                payload.put("barangay", cleanBarangay);
                payload.put("weightKg", weightKg);
                payload.put("source", "android-staff-scan");
                JSONObject response = apiClient.postJson("/api/qr-scans", payload);
                activity.runOnUiThread(() -> {
                    dialog.dismiss();
                    JSONObject scan = response.optJSONObject("scan");
                    int points = scan != null ? scan.optInt("pointsEarned", Math.round((float) weightKg * 10)) : Math.round((float) weightKg * 10);
                    Toast.makeText(activity, "Donation recorded: +" + points + " pts", Toast.LENGTH_LONG).show();
                    showPage("scan");
                });
            } catch (Exception error) {
                activity.runOnUiThread(() -> {
                    recordButton.setEnabled(true);
                    messageView.setTextColor(AppColors.RED);
                    messageView.setText(error.getMessage());
                });
            }
        });
    }

    public void saveSettings(JSONObject settings) {
        executor.execute(() -> {
            try {
                apiClient.putJson("/api/settings", settings);
                activity.runOnUiThread(() -> {
                    Toast.makeText(activity, "Settings saved.", Toast.LENGTH_SHORT).show();
                    showPage("settings");
                });
            } catch (Exception error) {
                activity.runOnUiThread(() -> Toast.makeText(activity, error.getMessage(), Toast.LENGTH_LONG).show());
            }
        });
    }

    private void processDeviceEvents(JSONObject controlStatus) {
        JSONArray events = controlStatus.optJSONArray("events");
        if (events == null) return;

        if (!deviceEventsInitialized) {
            for (int index = 0; index < events.length(); index++) {
                JSONObject event = events.optJSONObject(index);
                if (event != null) seenDeviceEventIds.add(deviceEventId(event, index));
            }
            deviceEventsInitialized = true;
            return;
        }

        for (int index = events.length() - 1; index >= 0; index--) {
            JSONObject event = events.optJSONObject(index);
            if (event == null) continue;
            String eventId = deviceEventId(event, index);
            if (!seenDeviceEventIds.add(eventId) || !shouldNotifyForEvent(event)) continue;

            String eventType = firstNonBlank(event.optString("type"), event.optString("eventType"), event.optString("event_type"), "MACHINE_EVENT");
            String severity = event.optString("severity", "info").toLowerCase(Locale.ROOT);
            String message = firstNonBlank(event.optString("message"), eventType.replace('_', ' '));
            String title = eventTitle(eventType, severity);
            boolean isError = "error".equals(severity) || "critical".equals(severity) || eventType.contains("FAULT");

            postMachineNotification(eventId, title, message, isError);
            if (isError) {
                if (!activity.isFinishing()) showInfoDialog(title, message);
            } else {
                Toast.makeText(activity, title + ": " + message, Toast.LENGTH_LONG).show();
            }
        }
    }

    private boolean shouldNotifyForEvent(JSONObject event) {
        String eventType = firstNonBlank(event.optString("type"), event.optString("eventType"), event.optString("event_type"), "").toUpperCase(Locale.ROOT);
        String severity = event.optString("severity", "info").toLowerCase(Locale.ROOT);
        return eventType.contains("MIXER") || eventType.contains("PROCESS") || eventType.contains("FAULT") ||
            "warning".equals(severity) || "error".equals(severity) || "critical".equals(severity);
    }

    private String deviceEventId(JSONObject event, int fallbackIndex) {
        return firstNonBlank(
            event.optString("id"),
            event.optString("eventId"),
            event.optString("event_id"),
            firstNonBlank(event.optString("type"), event.optString("eventType"), event.optString("event_type"), "event") + ":" +
                firstNonBlank(event.optString("createdAt"), event.optString("created_at"), String.valueOf(fallbackIndex))
        );
    }

    private String eventTitle(String eventType, String severity) {
        String normalized = String.valueOf(eventType).toUpperCase(Locale.ROOT);
        if (normalized.contains("MIXER_STARTED")) return "Mixer started";
        if (normalized.contains("MIXER_COMPLETED")) return "Mixer completed";
        if (normalized.contains("PROCESS_STARTED")) return "Process started";
        if (normalized.contains("PROCESS_COMPLETED")) return "Process completed";
        if (normalized.contains("STOPPED")) return "Machine stopped";
        if (normalized.contains("FAULT") || "error".equals(severity) || "critical".equals(severity)) return "Machine error";
        return "PaperBriq update";
    }

    private void createNotificationChannels() {
        NotificationManager manager = (NotificationManager) activity.getSystemService(Context.NOTIFICATION_SERVICE);
        if (manager == null) return;
        NotificationChannel events = new NotificationChannel(EVENT_CHANNEL_ID, "Machine events", NotificationManager.IMPORTANCE_DEFAULT);
        events.setDescription("Mixer and PaperBriq process updates");
        NotificationChannel errors = new NotificationChannel(ERROR_CHANNEL_ID, "Machine errors", NotificationManager.IMPORTANCE_HIGH);
        errors.setDescription("PaperBriq faults that require attention");
        manager.createNotificationChannel(events);
        manager.createNotificationChannel(errors);
    }

    private void postMachineNotification(String eventId, String title, String message, boolean isError) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
            activity.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            return;
        }
        NotificationManager manager = (NotificationManager) activity.getSystemService(Context.NOTIFICATION_SERVICE);
        if (manager == null) return;
        Intent intent = new Intent(activity, MainActivity.class);
        intent.setFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP);
        PendingIntent pendingIntent = PendingIntent.getActivity(
            activity,
            Math.abs(eventId.hashCode()),
            intent,
            PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE
        );
        Notification notification = new Notification.Builder(activity, isError ? ERROR_CHANNEL_ID : EVENT_CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_paperbriq)
            .setContentTitle(title)
            .setContentText(message)
            .setStyle(new Notification.BigTextStyle().bigText(message))
            .setContentIntent(pendingIntent)
            .setAutoCancel(true)
            .setCategory(isError ? Notification.CATEGORY_ERROR : Notification.CATEGORY_STATUS)
            .build();
        manager.notify(Math.abs(eventId.hashCode()), notification);
    }

    private String firstNonBlank(String... values) {
        for (String value : values) {
            if (value != null && !value.trim().isEmpty()) return value.trim();
        }
        return "";
    }

    private String capitalize(String value) {
        String clean = String.valueOf(value).trim();
        if (clean.isEmpty()) return clean;
        return clean.substring(0, 1).toUpperCase(Locale.ROOT) + clean.substring(1);
    }

    public void destroy() {
        stopLiveUpdates();
        executor.shutdownNow();
    }

    private boolean restoreSavedSession() {
        String userJson = preferences.getString(PREF_USER, "");
        if (userJson == null || userJson.trim().isEmpty()) return false;

        try {
            session.user = new JSONObject(userJson);
            String summaryJson = preferences.getString(PREF_SUMMARY, "");
            if (summaryJson != null && !summaryJson.trim().isEmpty()) {
                session.summary = new JSONObject(summaryJson);
            }
            return true;
        } catch (Exception error) {
            clearSavedSession();
            session.clear();
            return false;
        }
    }

    private void saveSession() {
        if (session.user == null) return;
        SharedPreferences.Editor editor = preferences.edit();
        editor.putString(PREF_USER, session.user.toString());
        editor.putString(PREF_SUMMARY, session.summary != null ? session.summary.toString() : "");
        editor.apply();
    }

    private void clearSavedSession() {
        preferences.edit().clear().apply();
    }

    private void startLiveUpdates() {
        if (liveUpdatesStarted || session.user == null) return;
        liveUpdatesStarted = true;
        mainHandler.removeCallbacks(liveUpdater);
        mainHandler.postDelayed(liveUpdater, LIVE_UPDATE_MS);
    }

    private void stopLiveUpdates() {
        liveUpdatesStarted = false;
        mainHandler.removeCallbacks(liveUpdater);
    }

    private View buildQrPreview(String code) {
        ImageView image = new ImageView(activity);
        image.setPadding(ui.dp(14), ui.dp(14), ui.dp(14), ui.dp(14));
        image.setBackground(ui.round(Color.WHITE, ui.dp(18), AppColors.LINE, 1));
        image.setScaleType(ImageView.ScaleType.FIT_CENTER);
        try {
            image.setImageBitmap(createQrBitmap(code, ui.dp(220)));
        } catch (WriterException error) {
            image.setImageDrawable(null);
        }
        return image;
    }

    private Bitmap createQrBitmap(String code, int sizePx) throws WriterException {
        int safeSize = Math.max(sizePx, 256);
        BitMatrix matrix = new QRCodeWriter().encode(safeQrCode(code), BarcodeFormat.QR_CODE, safeSize, safeSize);
        Bitmap bitmap = Bitmap.createBitmap(safeSize, safeSize, Bitmap.Config.ARGB_8888);
        for (int y = 0; y < safeSize; y++) {
            for (int x = 0; x < safeSize; x++) {
                bitmap.setPixel(x, y, matrix.get(x, y) ? AppColors.GREEN_DARK : Color.WHITE);
            }
        }
        return bitmap;
    }

    private String safeQrCode(String code) {
        String cleanCode = String.valueOf(code == null ? "" : code).trim();
        return cleanCode.isEmpty() ? "PBQ-USER-PENDING" : cleanCode;
    }

    private void styleDialogButtons(AlertDialog dialog) {
        Button positive = dialog.getButton(AlertDialog.BUTTON_POSITIVE);
        Button negative = dialog.getButton(AlertDialog.BUTTON_NEGATIVE);
        if (positive != null) {
            positive.setAllCaps(false);
            positive.setTextColor(Color.WHITE);
            positive.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
            positive.setBackground(ui.round(AppColors.GREEN, ui.dp(12), Color.TRANSPARENT, 0));
            positive.setPadding(ui.dp(14), 0, ui.dp(14), 0);
        }
        if (negative != null) {
            negative.setAllCaps(false);
            negative.setTextColor(AppColors.GREEN_DARK);
            negative.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
            negative.setBackground(ui.round(Color.WHITE, ui.dp(12), AppColors.LINE, 1));
            negative.setPadding(ui.dp(14), 0, ui.dp(14), 0);
        }
    }
}
