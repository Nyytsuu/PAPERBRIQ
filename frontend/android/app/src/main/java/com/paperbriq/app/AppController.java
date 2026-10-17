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
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
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
    private static final String DEVICE_ID = "paperbriq-esp32";
    private static final String EVENT_CHANNEL_ID = "paperbriq_machine_events";
    private static final String ERROR_CHANNEL_ID = "paperbriq_machine_errors";

    private final Activity activity;
    private final ApiClient apiClient;
    private final ExecutorService executor = Executors.newSingleThreadExecutor();
    private final AppSession session = new AppSession();
    private final Ui ui;
    private final SharedPreferences preferences;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private final Set<String> seenDeviceEventIds = new HashSet<>();
    private String controlKey = "";
    private boolean deviceEventsInitialized = false;
    private boolean commandSubmitting = false;
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
        ScrollView refreshedScroll = (ScrollView) new AppShell().create(this, pageKey);
        View refreshedContent = refreshedScroll.getChildAt(0);
        refreshedScroll.removeView(refreshedContent);

        currentScroll.suppressLayout(true);
        currentScroll.removeAllViews();
        currentScroll.addView(refreshedContent);
        currentScroll.suppressLayout(false);
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
                try {
                    backendHealth = apiClient.getJson("/api/health");
                } catch (Exception ignored) {
                    backendHealth = new JSONObject();
                }
                if (canControlMachine()) {
                    try {
                        controlStatus = apiClient.getJson("/api/device/control-status?deviceId=" + DEVICE_ID);
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
                activity.runOnUiThread(() -> {
                    if (nextControlStatus != null) {
                        processDeviceEvents(nextControlStatus);
                    }
                    if (pageKey.equals(session.currentPage)) {
                        renderPage(pageKey, true);
                    }
                });
            } catch (Exception error) {
                activity.runOnUiThread(() -> {
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
            case "history":
                return "/api/history";
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
                payload.put("role", role);
                JSONObject response = apiClient.postJson("/api/auth/login", payload);
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
        controlKey = "";
        seenDeviceEventIds.clear();
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

    public boolean hasControlKey() {
        return !controlKey.trim().isEmpty();
    }

    public void showControlKeyDialog(Runnable afterSaved) {
        EditText input = ui.editText("Operator control key", true);
        input.setText(controlKey);

        AlertDialog dialog = new AlertDialog.Builder(activity)
            .setTitle("Machine control key")
            .setMessage("Enter the CONTROL_API_KEY configured on Render. It is kept only until this app closes or you log out.")
            .setView(input)
            .setNegativeButton("Cancel", null)
            .setPositiveButton("Use key", null)
            .create();

        dialog.setOnShowListener(openDialog -> {
            styleDialogButtons(dialog);
            dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(view -> {
                String nextKey = input.getText().toString().trim();
                if (nextKey.isEmpty()) {
                    input.setError("Control key is required");
                    return;
                }
                controlKey = nextKey;
                dialog.dismiss();
                Toast.makeText(activity, "Control key ready for this session.", Toast.LENGTH_SHORT).show();
                if (afterSaved != null) afterSaved.run();
                if ("process".equals(session.currentPage)) renderPage("process", true);
            });
        });
        dialog.show();
    }

    public void sendDeviceCommand(String command) {
        String normalized = String.valueOf(command).trim().toLowerCase(Locale.ROOT);
        if (!canControlMachine()) {
            Toast.makeText(activity, "Only administrators and barangay staff can control the machine.", Toast.LENGTH_LONG).show();
            return;
        }
        if (!("start".equals(normalized) || "stop".equals(normalized) || "reset".equals(normalized))) {
            Toast.makeText(activity, "Unsupported machine command.", Toast.LENGTH_LONG).show();
            return;
        }
        if (!hasControlKey()) {
            showControlKeyDialog(() -> sendDeviceCommand(normalized));
            return;
        }
        if (commandSubmitting) {
            Toast.makeText(activity, "A machine command is already being submitted.", Toast.LENGTH_SHORT).show();
            return;
        }

        commandSubmitting = true;
        executor.execute(() -> {
            try {
                JSONObject payload = new JSONObject();
                payload.put("deviceId", DEVICE_ID);
                payload.put("command", normalized);
                payload.put("source", "android");
                if (session.user != null) {
                    payload.put("issuedBy", session.user.optString("email", session.user.optString("fullName", "android-operator")));
                }
                Map<String, String> headers = new HashMap<>();
                headers.put("X-Control-Key", controlKey);
                JSONObject response = apiClient.postJson("/api/device/commands", payload, headers);
                JSONObject queued = response.optJSONObject("command");
                String status = queued != null ? queued.optString("status", "pending") : "pending";
                activity.runOnUiThread(() -> {
                    commandSubmitting = false;
                    String label = "stop".equals(normalized) ? "Software Stop" : capitalize(normalized);
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
