package com.paperbriq.app;

import android.app.Activity;
import android.app.AlertDialog;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import com.paperbriq.app.data.ApiClient;
import com.paperbriq.app.data.AppSession;
import com.paperbriq.app.ui.AppShell;
import com.paperbriq.app.ui.LoginScreen;
import com.paperbriq.app.ui.Ui;

import org.json.JSONObject;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class AppController {
    private final Activity activity;
    private final ApiClient apiClient;
    private final ExecutorService executor = Executors.newSingleThreadExecutor();
    private final AppSession session = new AppSession();
    private final Ui ui;

    public AppController(Activity activity) {
        this.activity = activity;
        this.apiClient = new ApiClient();
        this.ui = new Ui(activity);
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

    public void showLogin() {
        activity.setContentView(new LoginScreen().create(this));
    }

    public void showPage(String pageKey) {
        session.currentPage = pageKey;
        activity.setContentView(new AppShell().create(this, pageKey));
    }

    public void login(String email, String password, String role, Button loginButton, TextView messageView) {
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

    public void showAccessDialog(String role) {
        EditText name = ui.editText("Full name", false);
        EditText email = ui.editText("Email address", false);
        EditText barangay = ui.editText("Barangay or community", false);

        LinearLayout form = ui.column();
        form.setPadding(ui.dp(4), ui.dp(8), ui.dp(4), 0);
        form.addView(name, ui.fullWidth(ui.dp(52), 0, ui.dp(8), 0, ui.dp(8)));
        form.addView(email, ui.fullWidth(ui.dp(52), 0, 0, 0, ui.dp(8)));
        form.addView(barangay, ui.fullWidth(ui.dp(52), 0, 0, 0, 0));

        new AlertDialog.Builder(activity)
            .setTitle("Request Access")
            .setView(form)
            .setNegativeButton("Close", null)
            .setPositiveButton("Send", (dialog, which) -> sendAccessRequest(
                name.getText().toString(),
                email.getText().toString(),
                barangay.getText().toString(),
                role
            ))
            .show();
    }

    private void sendAccessRequest(String name, String email, String barangay, String role) {
        executor.execute(() -> {
            try {
                JSONObject payload = new JSONObject();
                payload.put("fullName", name);
                payload.put("email", email);
                payload.put("requestedRole", role);
                payload.put("barangay", barangay);
                payload.put("message", "Android app access request");
                apiClient.postJson("/api/access-requests", payload);
                activity.runOnUiThread(() -> Toast.makeText(activity, "Access request sent.", Toast.LENGTH_SHORT).show());
            } catch (Exception error) {
                activity.runOnUiThread(() -> Toast.makeText(activity, error.getMessage(), Toast.LENGTH_LONG).show());
            }
        });
    }

    public void destroy() {
        executor.shutdownNow();
    }
}
