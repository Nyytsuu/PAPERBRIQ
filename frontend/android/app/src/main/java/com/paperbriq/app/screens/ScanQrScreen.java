package com.paperbriq.app.screens;

import android.graphics.Typeface;
import android.view.Gravity;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import com.paperbriq.app.AppColors;
import com.paperbriq.app.AppController;
import com.paperbriq.app.ui.Ui;

import org.json.JSONArray;
import org.json.JSONObject;

public class ScanQrScreen implements AppScreen {
    @Override
    public String key() {
        return "scan";
    }

    @Override
    public String navLabel() {
        return "Scan QR";
    }

    @Override
    public String title() {
        return "Scan QR";
    }

    @Override
    public String subtitle() {
        return "Scan contributor QR codes to record paper waste.";
    }

    @Override
    public void render(LinearLayout root, AppController controller) {
        Ui ui = controller.ui();
        JSONObject payload = controller.session().pagePayload("scan");
        JSONObject scanStatus = payload.optJSONObject("scanStatus");
        if (scanStatus == null) scanStatus = new JSONObject();
        JSONObject contributor = payload.optJSONObject("contributor");
        JSONArray recent = payload.optJSONArray("recent");

        root.addView(ui.sectionTitle("Scan Contributor QR Code", ""));

        LinearLayout scanner = ui.card(22);
        scanner.setPadding(ui.dp(18), ui.dp(18), ui.dp(18), ui.dp(18));
        TextView frame = ui.text("QR", 72, AppColors.GREEN_DARK, Typeface.BOLD);
        frame.setGravity(Gravity.CENTER);
        frame.setBackground(ui.round(android.graphics.Color.rgb(238, 246, 240), ui.dp(18), AppColors.GREEN, 2));
        scanner.addView(frame, ui.fullWidth(ui.dp(260), 0, ui.dp(10), 0, ui.dp(12)));
        TextView guide = ui.text("Align the contributor QR code within the frame.", 14, AppColors.MUTED, Typeface.BOLD);
        guide.setGravity(Gravity.CENTER);
        scanner.addView(guide);
        android.widget.Button scanButton = ui.greenButton("Scan with Camera");
        scanButton.setOnClickListener(v -> controller.startQrScanner());
        scanner.addView(scanButton, ui.fullWidth(ui.dp(54), 0, ui.dp(14), 0, 0));
        root.addView(scanner, ui.fullWidth(LinearLayout.LayoutParams.WRAP_CONTENT, 0, 0, 0, ui.dp(12)));

        ui.addGrid(root, new android.view.View[]{
            ui.infoListCard("Scan Status", new String[][]{
                {"Result", scanStatus.optString("status", "waiting")},
                {"Message", scanStatus.optString("message", "No QR scans recorded yet.")},
                {"Source", "qr_scans table"}
            }),
            contributorInfo(ui, contributor)
        }, 1);

        root.addView(ui.sectionTitle("Enter Code Manually", ""));
        LinearLayout manual = ui.row();
        manual.setGravity(Gravity.CENTER_VERTICAL);
        EditText code = ui.editText("Enter code here", false);
        manual.addView(code, new LinearLayout.LayoutParams(0, ui.dp(54), 1));
        android.widget.Button submit = ui.greenButton("Confirm");
        submit.setOnClickListener(v -> controller.createManualQrScan(code.getText().toString()));
        LinearLayout.LayoutParams submitParams = new LinearLayout.LayoutParams(ui.dp(126), ui.dp(54));
        submitParams.setMargins(ui.dp(10), 0, 0, 0);
        manual.addView(submit, submitParams);
        root.addView(manual, ui.fullWidth(LinearLayout.LayoutParams.WRAP_CONTENT, 0, 0, 0, ui.dp(12)));

        root.addView(ui.sectionTitle("Recent Scans", "View All"));
        root.addView(ui.simpleList(scanRows(recent, ui)));
    }

    private android.view.View contributorInfo(Ui ui, JSONObject contributor) {
        LinearLayout card = ui.card(18);
        card.setPadding(ui.dp(16), ui.dp(16), ui.dp(16), ui.dp(16));
        card.addView(ui.text("Contributor Information", 18, AppColors.INK, Typeface.BOLD));
        if (contributor == null) {
            card.addView(ui.text("No scan selected", 20, AppColors.INK, Typeface.BOLD));
            card.addView(ui.pill("Waiting", AppColors.MUTED));
            card.addView(ui.infoRow("Database", "No QR scan records yet"));
            return card;
        }

        card.addView(ui.text(contributor.optString("name", "Contributor"), 20, AppColors.INK, Typeface.BOLD));
        card.addView(ui.pill("Verified Contributor", AppColors.GREEN));
        card.addView(ui.infoRow("Barangay", contributor.optString("barangay", "Unassigned")));
        card.addView(ui.infoRow("Member Since", contributor.optString("memberSince", "--")));
        card.addView(ui.infoRow("Total Contributions", String.valueOf(contributor.optInt("totalContributions", 0))));
        LinearLayout earned = ui.card(16);
        earned.setPadding(ui.dp(14), ui.dp(14), ui.dp(14), ui.dp(14));
        earned.setBackground(ui.round(AppColors.GREEN_SOFT, ui.dp(16), android.graphics.Color.TRANSPARENT, 0));
        earned.addView(ui.text("Points Earned", 16, AppColors.GREEN_DARK, Typeface.BOLD));
        earned.addView(ui.text("+" + contributor.optInt("pointsEarned", 0) + " pts", 28, AppColors.GREEN, Typeface.BOLD));
        earned.addView(ui.text("Thank you for recycling!", 13, AppColors.GREEN_DARK, Typeface.BOLD));
        card.addView(earned, ui.fullWidth(LinearLayout.LayoutParams.WRAP_CONTENT, 0, ui.dp(12), 0, 0));
        return card;
    }

    private String[][] scanRows(JSONArray recent, Ui ui) {
        if (recent == null || recent.length() == 0) {
            return new String[][]{{"--", "No QR scans in MySQL yet", "Use manual code or scanner", "Empty"}};
        }

        int count = Math.min(recent.length(), 8);
        String[][] rows = new String[count][4];
        for (int index = 0; index < count; index++) {
            JSONObject scan = recent.optJSONObject(index);
            if (scan == null) scan = new JSONObject();
            rows[index][0] = scan.optString("time", "--");
            rows[index][1] = scan.optString("contributor", "Contributor") + " - " + scan.optString("barangay", "Unassigned");
            rows[index][2] = ui.formatNumber(scan.optDouble("weightKg", 0)) + " kg - +" + ui.formatNumber(scan.optDouble("points", 0)) + " pts";
            rows[index][3] = scan.optString("status", "Recorded");
        }
        return rows;
    }
}
