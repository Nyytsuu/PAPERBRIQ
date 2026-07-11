package com.paperbriq.app.screens;

import android.graphics.Color;
import android.graphics.Typeface;
import android.widget.LinearLayout;

import com.paperbriq.app.AppColors;
import com.paperbriq.app.AppController;
import com.paperbriq.app.ui.Ui;

import org.json.JSONObject;

public class DashboardScreen implements AppScreen {
    @Override
    public String key() {
        return "dashboard";
    }

    @Override
    public String navLabel() {
        return "Dashboard";
    }

    @Override
    public String title() {
        return "Dashboard";
    }

    @Override
    public String subtitle() {
        return "An IoT-based paper waste briquette system with QR incentives.";
    }

    @Override
    public void render(LinearLayout root, AppController controller) {
        Ui ui = controller.ui();
        JSONObject totals = controller.session().totals();

        LinearLayout status = ui.card(20);
        status.setPadding(ui.dp(18), ui.dp(18), ui.dp(18), ui.dp(18));
        status.setBackground(ui.round(AppColors.GREEN, ui.dp(20), Color.TRANSPARENT, 0));
        status.addView(ui.text("System Status", 14, Color.WHITE, Typeface.BOLD));
        status.addView(ui.text("Running", 34, Color.WHITE, Typeface.BOLD));
        status.addView(ui.text("All PaperBriq systems are operating smoothly.", 14, Color.argb(225, 255, 255, 255), Typeface.NORMAL));
        root.addView(status, ui.fullWidth(LinearLayout.LayoutParams.WRAP_CONTENT, 0, 0, 0, ui.dp(12)));

        ui.addGrid(root, new android.view.View[]{
            ui.statCard("Rainwater Level", "78%", "Storage: 780 L / 1000 L", AppColors.BLUE),
            ui.statCard("Battery Level", "92%", "Storage: 23.0 V", AppColors.GREEN),
            ui.statCard("Power Output", "240 w", "Solar power active", AppColors.BLUE),
            ui.statCard("Active Contributors", ui.formatNumber(totals.optInt("contributors", 15)), "This week", AppColors.GREEN)
        }, 2);

        root.addView(ui.sectionTitle("Process Progress", "Live"));
        root.addView(ScreenPieces.processStepsCard(ui));

        ui.addGrid(root, new android.view.View[]{
            ui.progressCard("Overall Progress", "65%", "Keep going. Production is on track."),
            ui.infoListCard("Run Summary", new String[][]{
                {"Estimated Time", "1 hr 20 mins"},
                {"Total to Produce", "20 bricks"},
                {"Completed", "8 bricks"}
            })
        }, 1);

        root.addView(ui.sectionTitle("Process Log", "View All"));
        root.addView(ui.simpleList(new String[][]{
            {"10:35 AM", "Shredding Paper Started", "Process"},
            {"10:50 AM", "Rainwater Added", "Resource"},
            {"11:15 AM", "Pulping Mixture Began", "Process"},
            {"11:45 AM", "Paper Collection Completed", "Completed"},
            {"12:03 PM", "System Check OK", "System"}
        }));

        root.addView(ui.sectionTitle("Quick Actions", ""));
        ui.addGrid(root, new android.view.View[]{
            ui.actionCard("Start Process", "Play"),
            ui.actionCard("Stop Process", "Stop"),
            ui.actionCard("Generate Report", "Report"),
            ui.actionCard("Export Data", "Export")
        }, 2);

        root.addView(ui.sectionTitle("Weekly Summary", ""));
        ui.addGrid(root, new android.view.View[]{
            ui.statCard("Paper Waste Collected", ui.formatNumber(totals.optDouble("paperKg", 45)) + " kg", "This week", AppColors.GREEN),
            ui.statCard("Briquettes Produced", "8 / 20", "This week", AppColors.AMBER),
            ui.statCard("Water Saved", "120 L", "This week", AppColors.BLUE),
            ui.statCard("Active Contributors", "15", "This week", AppColors.GREEN)
        }, 2);
    }
}
