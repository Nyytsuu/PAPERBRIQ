package com.paperbriq.app.screens;

import android.content.res.ColorStateList;
import android.graphics.Typeface;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;

import com.paperbriq.app.AppColors;
import com.paperbriq.app.AppController;
import com.paperbriq.app.ui.Ui;

import org.json.JSONObject;

import java.util.ArrayList;
import java.util.List;

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
        return "User Settings";
    }

    @Override
    public String subtitle() {
        return "Personal profile, QR, rewards, privacy, and app preferences.";
    }

    @Override
    public void render(LinearLayout root, AppController controller) {
        Ui ui = controller.ui();
        JSONObject settings = controller.session().pagePayload("settings");
        JSONObject userSettings = object(settings, "userSettings");
        JSONObject user = controller.session().user != null ? controller.session().user : new JSONObject();
        List<SettingBinding> bindings = new ArrayList<>();

        root.addView(noteCard(ui));
        root.addView(profileCard(ui, controller, userSettings, user, bindings), bottomSpace(ui));
        root.addView(qrCard(ui, controller, userSettings, user, bindings), bottomSpace(ui));
        root.addView(notificationCard(ui, userSettings, bindings), bottomSpace(ui));
        root.addView(rewardPreferenceCard(ui, userSettings, bindings), bottomSpace(ui));
        root.addView(contributionCard(ui, controller, userSettings, user, bindings), bottomSpace(ui));
        root.addView(privacyCard(ui, controller, userSettings, user, bindings), bottomSpace(ui));
        root.addView(appPreferenceCard(ui, userSettings, bindings), bottomSpace(ui));
        root.addView(helpCard(ui, controller), bottomSpace(ui));

        Button save = ui.greenButton("Save User Settings");
        save.setOnClickListener(v -> {
            JSONObject updated = copyObject(settings);
            try {
                updated.put("userSettings", collectUserSettings(userSettings, bindings));
            } catch (Exception ignored) {
                updated = settings;
            }

            JSONObject finalUpdated = updated;
            controller.confirmAction(
                "Save user settings?",
                "Only your personal app settings will be updated. Machine controls, reports, staff accounts, and reward rules stay protected.",
                () -> controller.saveSettings(finalUpdated)
            );
        });
        root.addView(save, ui.fullWidth(ui.dp(54), 0, 0, 0, 0));
    }

    private View noteCard(Ui ui) {
        LinearLayout card = ui.card(18);
        card.setPadding(ui.dp(16), ui.dp(16), ui.dp(16), ui.dp(16));
        card.addView(ui.text("Contributor account only", 17, AppColors.INK, Typeface.BOLD));
        card.addView(ui.text("Your information is used for contribution tracking, QR verification, and reward claiming. Admin and machine settings are not available in the user app.", 13, AppColors.MUTED, Typeface.NORMAL));
        return card;
    }

    private View profileCard(Ui ui, AppController controller, JSONObject settings, JSONObject user, List<SettingBinding> bindings) {
        LinearLayout card = editableCard(ui, "Profile Settings", "Update your personal information.");
        JSONObject profile = object(settings, "profile");

        addTextField(card, ui, bindings, "profile", "fullName", "Full name", pick(profile.optString("fullName", ""), user.optString("fullName", "")), false);
        addTextField(card, ui, bindings, "profile", "contactNumber", "Contact number", value(profile, "contactNumber", "+63 912 345 6789"), false);
        addTextField(card, ui, bindings, "profile", "email", "Email address", pick(profile.optString("email", ""), user.optString("email", "")), false);
        addTextField(card, ui, bindings, "profile", "barangay", "Barangay", pick(profile.optString("barangay", ""), user.optString("barangay", "")), false);
        addTextField(card, ui, bindings, "profile", "address", "Address (optional)", value(profile, "address", ""), false);
        addTextField(card, ui, bindings, "profile", "profilePicture", "Profile picture", value(profile, "profilePicture", "Use initials"), false);
        card.addView(actionButton(ui, "Change Password", () -> controller.showForgotPasswordDialog(user.optString("email", profile.optString("email", "")), "contributor")));
        return card;
    }

    private View qrCard(Ui ui, AppController controller, JSONObject settings, JSONObject user, List<SettingBinding> bindings) {
        LinearLayout card = editableCard(ui, "My QR Code", "Use this QR code when donating paper waste.");
        JSONObject qr = object(settings, "qr");
        String defaultCode = qrCodeFor(user);

        EditText qrCodeInput = addTextField(card, ui, bindings, "qr", "code", "Personal QR code", value(qr, "code", defaultCode), false);
        addToggleField(card, ui, bindings, "qr", "showForScanning", "Show QR code for scanning", bool(qr, "showForScanning", true));
        addToggleField(card, ui, bindings, "qr", "allowDownload", "Download QR code", bool(qr, "allowDownload", true));
        addToggleField(card, ui, bindings, "qr", "active", "QR code status: Active", bool(qr, "active", true));

        LinearLayout actions = ui.row();
        actions.setGravity(Gravity.CENTER_VERTICAL);
        actions.setPadding(0, ui.dp(12), 0, 0);
        actions.addView(actionButton(ui, "Show QR Code", () -> controller.showUserQrDialog(qrCodeInput.getText().toString(), user.optString("fullName", "Contributor"))), actionParams(ui, 0));
        actions.addView(actionButton(ui, "Download QR", () -> controller.downloadUserQr(qrCodeInput.getText().toString())), actionParams(ui, ui.dp(8)));
        card.addView(actions);

        card.addView(actionButton(ui, "Regenerate QR Code", () -> {
            qrCodeInput.setText(qrCodeFor(user) + "-" + System.currentTimeMillis());
            controller.showInfoDialog("QR regenerated", "Review the new QR code and tap Save User Settings to keep it.");
        }));
        return card;
    }

    private View notificationCard(Ui ui, JSONObject settings, List<SettingBinding> bindings) {
        LinearLayout card = editableCard(ui, "Notification Settings", "Choose what alerts you want to receive.");
        JSONObject notifications = object(settings, "notifications");

        addToggleField(card, ui, bindings, "notifications", "pointsEarned", "Points earned notification", bool(notifications, "pointsEarned", true));
        addToggleField(card, ui, bindings, "notifications", "rewardRedeemed", "Reward redeemed notification", bool(notifications, "rewardRedeemed", true));
        addToggleField(card, ui, bindings, "notifications", "newRewardAvailable", "New reward available", bool(notifications, "newRewardAvailable", true));
        addToggleField(card, ui, bindings, "notifications", "collectionSchedule", "Barangay collection schedule", bool(notifications, "collectionSchedule", true));
        addToggleField(card, ui, bindings, "notifications", "impactUpdates", "Environmental impact updates", bool(notifications, "impactUpdates", true));
        addToggleField(card, ui, bindings, "notifications", "weeklySummary", "Weekly summary", bool(notifications, "weeklySummary", false));
        return card;
    }

    private View rewardPreferenceCard(Ui ui, JSONObject settings, List<SettingBinding> bindings) {
        LinearLayout card = editableCard(ui, "Rewards Preferences", "Pick the reward types you are interested in.");
        JSONObject rewards = object(settings, "rewardPreferences");

        addToggleField(card, ui, bindings, "rewardPreferences", "schoolSupplies", "School supplies", bool(rewards, "schoolSupplies", true));
        addToggleField(card, ui, bindings, "rewardPreferences", "ecoProducts", "Eco-products", bool(rewards, "ecoProducts", true));
        addToggleField(card, ui, bindings, "rewardPreferences", "vouchers", "Vouchers", bool(rewards, "vouchers", true));
        addToggleField(card, ui, bindings, "rewardPreferences", "foodPacks", "Food packs", bool(rewards, "foodPacks", false));
        addToggleField(card, ui, bindings, "rewardPreferences", "plantingActivities", "Planting activities", bool(rewards, "plantingActivities", true));
        addToggleField(card, ui, bindings, "rewardPreferences", "communityCertificates", "Community certificates", bool(rewards, "communityCertificates", true));
        return card;
    }

    private View contributionCard(Ui ui, AppController controller, JSONObject settings, JSONObject user, List<SettingBinding> bindings) {
        LinearLayout card = editableCard(ui, "Contribution Settings", "Donation reminders and personal contribution access.");
        JSONObject contribution = object(settings, "contribution");

        addTextField(card, ui, bindings, "contribution", "defaultDropoff", "Default barangay drop-off", value(contribution, "defaultDropoff", user.optString("barangay", "Barangay San Isidro")), false);
        addTextField(card, ui, bindings, "contribution", "collectionReminder", "Preferred collection reminder", value(contribution, "collectionReminder", "Every Friday"), false);
        addToggleField(card, ui, bindings, "contribution", "showHistory", "Show contribution history", bool(contribution, "showHistory", true));

        LinearLayout actions = ui.row();
        actions.setGravity(Gravity.CENTER_VERTICAL);
        actions.setPadding(0, ui.dp(12), 0, 0);
        actions.addView(actionButton(ui, "View History", () -> controller.showPage("history")), actionParams(ui, 0));
        actions.addView(actionButton(ui, "Download Record", () -> controller.showInfoDialog("Contribution record", "Your contribution record download request has been prepared for this account.")), actionParams(ui, ui.dp(8)));
        card.addView(actions);
        return card;
    }

    private View privacyCard(Ui ui, AppController controller, JSONObject settings, JSONObject user, List<SettingBinding> bindings) {
        LinearLayout card = editableCard(ui, "Privacy and Security", "Protect your profile, QR verification, and rewards data.");
        JSONObject privacy = object(settings, "privacy");

        addToggleField(card, ui, bindings, "privacy", "showOnLeaderboard", "Show my name on leaderboard", bool(privacy, "showOnLeaderboard", true));
        addToggleField(card, ui, bindings, "privacy", "allowStaffQrProfile", "Allow staff to verify my QR profile", bool(privacy, "allowStaffQrProfile", true));
        addToggleField(card, ui, bindings, "privacy", "loginActivity", "Login activity alerts", bool(privacy, "loginActivity", true));

        LinearLayout actions = ui.row();
        actions.setGravity(Gravity.CENTER_VERTICAL);
        actions.setPadding(0, ui.dp(12), 0, 0);
        actions.addView(actionButton(ui, "Change Password", () -> controller.showForgotPasswordDialog(user.optString("email", ""), "contributor")), actionParams(ui, 0));
        actions.addView(actionButton(ui, "Delete Request", () -> controller.confirmAction(
            "Request account deletion?",
            "This records a request only. An admin must review it before any account data is removed.",
            () -> controller.showInfoDialog("Request recorded", "Your account deletion request is ready for admin review.")
        )), actionParams(ui, ui.dp(8)));
        card.addView(actions);
        return card;
    }

    private View appPreferenceCard(Ui ui, JSONObject settings, List<SettingBinding> bindings) {
        LinearLayout card = editableCard(ui, "App Preferences", "Basic display options for the user app.");
        JSONObject app = object(settings, "appPreferences");

        addTextField(card, ui, bindings, "appPreferences", "theme", "Theme", value(app, "theme", "Light"), false);
        addTextField(card, ui, bindings, "appPreferences", "language", "Language", value(app, "language", "English"), false);
        addTextField(card, ui, bindings, "appPreferences", "fontSize", "Font size", value(app, "fontSize", "Medium"), false);
        addTextField(card, ui, bindings, "appPreferences", "dateFormat", "Date format", value(app, "dateFormat", "MM/DD/YYYY"), false);
        return card;
    }

    private View helpCard(Ui ui, AppController controller) {
        LinearLayout card = editableCard(ui, "Help and Support", "Quick guides for donating paper and claiming rewards.");

        String[][] help = {
            {"How to Earn Points", "Donate paper at your barangay station. Staff scan your QR, enter the weight, and points are added after confirmation."},
            {"How to Redeem Rewards", "Open Rewards, choose an available item, and let barangay staff confirm the claim."},
            {"Contact Barangay Staff", "Ask your assigned barangay staff for QR scanning, claim assistance, or collection schedules."},
            {"Report a Problem", "Report incorrect points, QR issues, or account problems to barangay staff for review."}
        };

        for (String[] item : help) {
            card.addView(actionButton(ui, item[0], () -> controller.showInfoDialog(item[0], item[1])));
        }
        return card;
    }

    private LinearLayout editableCard(Ui ui, String title, String subtitle) {
        LinearLayout card = ui.card(18);
        card.setPadding(ui.dp(16), ui.dp(16), ui.dp(16), ui.dp(16));
        card.addView(ui.text(title, 17, AppColors.INK, Typeface.BOLD));
        card.addView(ui.text(subtitle, 13, AppColors.MUTED, Typeface.NORMAL));
        return card;
    }

    private EditText addTextField(LinearLayout card, Ui ui, List<SettingBinding> bindings, String group, String key, String label, String currentValue, boolean number) {
        LinearLayout field = ui.column();
        field.setPadding(0, ui.dp(12), 0, 0);
        field.addView(ui.text(label, 13, AppColors.MUTED, Typeface.BOLD));

        EditText input = ui.editText(label, false);
        input.setText(currentValue);
        input.setInputType(number ? InputType.TYPE_CLASS_NUMBER | InputType.TYPE_NUMBER_FLAG_DECIMAL : InputType.TYPE_CLASS_TEXT);
        field.addView(input, ui.fullWidth(ui.dp(50), 0, ui.dp(6), 0, 0));
        card.addView(field);
        bindings.add(SettingBinding.text(group, key, input));
        return input;
    }

    private void addToggleField(LinearLayout card, Ui ui, List<SettingBinding> bindings, String group, String key, String label, boolean checked) {
        LinearLayout row = ui.row();
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(0, ui.dp(12), 0, 0);

        LinearLayout copy = ui.column();
        copy.addView(ui.text(label, 14, AppColors.INK, Typeface.BOLD));
        TextView state = ui.text(checked ? "On" : "Off", 12, AppColors.MUTED, Typeface.NORMAL);
        copy.addView(state);
        row.addView(copy, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));

        CheckBox checkBox = new CheckBox(card.getContext());
        checkBox.setChecked(checked);
        checkBox.setButtonTintList(ColorStateList.valueOf(AppColors.GREEN));
        checkBox.setOnCheckedChangeListener((button, isChecked) -> state.setText(isChecked ? "On" : "Off"));
        row.addView(checkBox, new LinearLayout.LayoutParams(ui.dp(48), ui.dp(48)));

        card.addView(row);
        bindings.add(SettingBinding.toggle(group, key, checkBox));
    }

    private Button actionButton(Ui ui, String label, Runnable action) {
        Button button = ui.outlineButton(label);
        button.setOnClickListener(v -> action.run());
        return button;
    }

    private LinearLayout.LayoutParams actionParams(Ui ui, int leftMargin) {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(0, ui.dp(46), 1);
        params.setMargins(leftMargin, 0, 0, 0);
        return params;
    }

    private LinearLayout.LayoutParams bottomSpace(Ui ui) {
        return ui.fullWidth(LinearLayout.LayoutParams.WRAP_CONTENT, 0, ui.dp(12), 0, 0);
    }

    private JSONObject collectUserSettings(JSONObject original, List<SettingBinding> bindings) {
        JSONObject updated = copyObject(original);
        for (SettingBinding binding : bindings) {
            try {
                JSONObject group = object(updated, binding.group);
                if (binding.isToggle) {
                    group.put(binding.key, binding.checkBox.isChecked());
                } else {
                    group.put(binding.key, binding.input.getText().toString().trim());
                }
                updated.put(binding.group, group);
            } catch (Exception ignored) {
                return updated;
            }
        }
        return updated;
    }

    private JSONObject object(JSONObject parent, String key) {
        JSONObject value = parent.optJSONObject(key);
        return value != null ? value : new JSONObject();
    }

    private JSONObject copyObject(JSONObject source) {
        try {
            return new JSONObject(source.toString());
        } catch (Exception ignored) {
            return new JSONObject();
        }
    }

    private String value(JSONObject object, String key, String fallback) {
        String value = object.optString(key, "");
        return value.isEmpty() ? fallback : value;
    }

    private boolean bool(JSONObject object, String key, boolean fallback) {
        if (!object.has(key)) return fallback;
        return object.optBoolean(key, fallback);
    }

    private String pick(String primary, String fallback) {
        if (primary != null && !primary.trim().isEmpty()) return primary;
        if (fallback != null && !fallback.trim().isEmpty()) return fallback;
        return "";
    }

    private String qrCodeFor(JSONObject user) {
        String email = user.optString("email", "contributor@paperbriq.local").trim().toLowerCase();
        String token = Integer.toHexString(Math.abs(email.hashCode())).toUpperCase();
        return "PBQ-USER-" + token;
    }

    private static class SettingBinding {
        final String group;
        final String key;
        final EditText input;
        final CheckBox checkBox;
        final boolean isToggle;

        private SettingBinding(String group, String key, EditText input, CheckBox checkBox, boolean isToggle) {
            this.group = group;
            this.key = key;
            this.input = input;
            this.checkBox = checkBox;
            this.isToggle = isToggle;
        }

        static SettingBinding text(String group, String key, EditText input) {
            return new SettingBinding(group, key, input, null, false);
        }

        static SettingBinding toggle(String group, String key, CheckBox checkBox) {
            return new SettingBinding(group, key, null, checkBox, true);
        }
    }
}
