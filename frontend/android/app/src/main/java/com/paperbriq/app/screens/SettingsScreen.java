package com.paperbriq.app.screens;

import android.widget.LinearLayout;

import com.paperbriq.app.AppController;
import com.paperbriq.app.ui.Ui;

public class SettingsScreen implements AppScreen {
    @Override
    public String key() {
        return "settings";
    }

    @Override
    public String navLabel() {
        return "Settings";
    }

    @Override
    public String title() {
        return "Settings";
    }

    @Override
    public String subtitle() {
        return "Manage account, machine, QR, and notification settings.";
    }

    @Override
    public void render(LinearLayout root, AppController controller) {
        Ui ui = controller.ui();
        ui.addGrid(root, new android.view.View[]{
            settingsCard(ui, "Profile Information", new String[][]{
                {"Full Name", "Juan Dela Cruz"},
                {"Email Address", "juan.delacruz@example.com"},
                {"Phone Number", "+63 912 345 6789"},
                {"Role", "Contributor"}
            }),
            settingsCard(ui, "Account Settings", new String[][]{
                {"Username", "juan.delacruz"},
                {"Account Type", "Contributor"},
                {"Member Since", "May 15, 2024"},
                {"Time Zone", "(GMT+8) Asia/Manila"}
            }),
            settingsCard(ui, "Machine Preferences", new String[][]{
                {"Default Briquette Size", "Standard (42x22 cm)"},
                {"Auto Start Machine", "On"},
                {"Shred Level", "Medium"},
                {"Drying Temperature", "55 C optimal"}
            }),
            settingsCard(ui, "Notification Settings", new String[][]{
                {"Email Notifications", "On"},
                {"SMS Notifications", "On"},
                {"Process Alerts", "On"},
                {"Maintenance Reminders", "On"}
            }),
            settingsCard(ui, "QR Code Settings", new String[][]{
                {"QR Code Display", "On"},
                {"Auto Refresh QR", "Off"},
                {"QR Code Expiry", "24 hours"},
                {"QR Code Download", "On"}
            }),
            settingsCard(ui, "Security Settings", new String[][]{
                {"Change Password", "Open"},
                {"Two-Factor Authentication", "On"},
                {"Login Activity", "View"},
                {"Session Timeout", "30 minutes"}
            })
        }, 1);

        root.addView(ui.sectionTitle("Appearance and Language", ""));
        root.addView(settingsCard(ui, "Display", new String[][]{
            {"Theme", "Light"},
            {"Language", "English (US)"},
            {"Date Format", "MM/DD/YYYY"}
        }));
    }

    private android.view.View settingsCard(Ui ui, String title, String[][] rows) {
        LinearLayout card = ui.card(18);
        card.setPadding(ui.dp(16), ui.dp(16), ui.dp(16), ui.dp(16));
        card.addView(ui.text(title, 17, com.paperbriq.app.AppColors.INK, android.graphics.Typeface.BOLD));
        for (String[] row : rows) {
            card.addView(ui.infoRow(row[0], row[1]));
        }
        card.addView(ui.greenButton("Save Changes"), new LinearLayout.LayoutParams(ui.dp(150), ui.dp(44)));
        return card;
    }
}
