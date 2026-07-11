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
        root.addView(scanner, ui.fullWidth(LinearLayout.LayoutParams.WRAP_CONTENT, 0, 0, 0, ui.dp(12)));

        ui.addGrid(root, new android.view.View[]{
            ui.infoListCard("Scan Status", new String[][]{
                {"Result", "Scan Successful"},
                {"Contributor", "Verified"},
                {"Record", "Data recorded"}
            }),
            contributorInfo(ui)
        }, 1);

        root.addView(ui.sectionTitle("Enter Code Manually", ""));
        LinearLayout manual = ui.row();
        manual.setGravity(Gravity.CENTER_VERTICAL);
        EditText code = ui.editText("Enter code here", false);
        manual.addView(code, new LinearLayout.LayoutParams(0, ui.dp(54), 1));
        android.widget.Button submit = ui.greenButton("Submit");
        submit.setOnClickListener(v -> Toast.makeText(controller.activity(), "Code submitted.", Toast.LENGTH_SHORT).show());
        LinearLayout.LayoutParams submitParams = new LinearLayout.LayoutParams(ui.dp(112), ui.dp(54));
        submitParams.setMargins(ui.dp(10), 0, 0, 0);
        manual.addView(submit, submitParams);
        root.addView(manual, ui.fullWidth(LinearLayout.LayoutParams.WRAP_CONTENT, 0, 0, 0, ui.dp(12)));

        root.addView(ui.sectionTitle("Recent Scans", "View All"));
        root.addView(ui.simpleList(new String[][]{
            {"10:35 AM", "Maria Santos - San Isidro", "+15 pts"},
            {"10:20 AM", "Pedro Dela Cruz - San Rafael", "+10 pts"},
            {"10:05 AM", "Ana Reyes - San Isidro", "+18 pts"},
            {"09:50 AM", "Luis Gonzales - San Roque", "+7 pts"},
            {"09:35 AM", "Carmen Villanueva - San Isidro", "+12 pts"}
        }));
    }

    private android.view.View contributorInfo(Ui ui) {
        LinearLayout card = ui.card(18);
        card.setPadding(ui.dp(16), ui.dp(16), ui.dp(16), ui.dp(16));
        card.addView(ui.text("Contributor Information", 18, AppColors.INK, Typeface.BOLD));
        card.addView(ui.text("Maria Santos", 20, AppColors.INK, Typeface.BOLD));
        card.addView(ui.pill("Verified Contributor", AppColors.GREEN));
        card.addView(ui.infoRow("Barangay", "San Isidro"));
        card.addView(ui.infoRow("Member Since", "March 15, 2024"));
        card.addView(ui.infoRow("Total Contributions", "12"));
        LinearLayout earned = ui.card(16);
        earned.setPadding(ui.dp(14), ui.dp(14), ui.dp(14), ui.dp(14));
        earned.setBackground(ui.round(AppColors.GREEN_SOFT, ui.dp(16), android.graphics.Color.TRANSPARENT, 0));
        earned.addView(ui.text("Points Earned", 16, AppColors.GREEN_DARK, Typeface.BOLD));
        earned.addView(ui.text("+15 pts", 28, AppColors.GREEN, Typeface.BOLD));
        earned.addView(ui.text("Thank you for recycling!", 13, AppColors.GREEN_DARK, Typeface.BOLD));
        card.addView(earned, ui.fullWidth(LinearLayout.LayoutParams.WRAP_CONTENT, 0, ui.dp(12), 0, 0));
        return card;
    }
}
