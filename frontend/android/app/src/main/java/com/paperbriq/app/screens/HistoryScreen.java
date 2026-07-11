package com.paperbriq.app.screens;

import android.widget.LinearLayout;

import com.paperbriq.app.AppColors;
import com.paperbriq.app.AppController;
import com.paperbriq.app.ui.Ui;

public class HistoryScreen implements AppScreen {
    @Override
    public String key() {
        return "history";
    }

    @Override
    public String navLabel() {
        return "History";
    }

    @Override
    public String title() {
        return "History";
    }

    @Override
    public String subtitle() {
        return "View past activities and system events.";
    }

    @Override
    public void render(LinearLayout root, AppController controller) {
        Ui ui = controller.ui();
        ui.addGrid(root, new android.view.View[]{
            ui.statCard("Total Sessions", "78", "All time", AppColors.GREEN),
            ui.statCard("Completed Runs", "64", "82% success rate", AppColors.GREEN),
            ui.statCard("Total Runtime", "1 hr 20 mins", "All completed runs", AppColors.GREEN)
        }, 1);

        root.addView(ui.sectionTitle("Recent Activities", "View All"));
        root.addView(ui.simpleList(new String[][]{
            {"Today", "System Check OK", "12:03 PM", "System"},
            {"Today", "Pulping Mixture Began", "11:15 AM", "Process"},
            {"Today", "Rainwater Added", "10:50 AM", "Resource"},
            {"Today", "QR Code Scanned", "09:30 AM", "QR"}
        }));

        root.addView(ui.sectionTitle("All Events", ""));
        root.addView(ui.simpleList(new String[][]{
            {"12:03 PM", "System Check OK", "All systems are operating normally", "System"},
            {"11:45 AM", "Paper Collection Completed", "Collected 45 kg / 60 kg", "Process Run"},
            {"11:15 AM", "Pulping Mixture Began", "Pulping mixture process started", "Process Run"},
            {"10:50 AM", "Rainwater Added", "Added 120 L of rainwater", "Resource"},
            {"09:30 AM", "QR Code Scanned", "User: Juan Dela Cruz", "QR Scan"},
            {"Yesterday", "Briquette Production Completed", "Produced 20 briquettes", "Process Run"},
            {"Yesterday", "Maintenance Performed", "Cleaned shredder blades", "Maintenance"},
            {"May 14", "Rainwater Added", "Added 100 L of rainwater", "Resource"},
            {"May 14", "Shredding Completed", "Output: 78%", "Process Run"},
            {"May 14", "Collect Paper Started", "Target: 60 kg", "Process Run"}
        }));
    }
}
