package com.paperbriq.app.screens;

import android.widget.LinearLayout;

import com.paperbriq.app.AppColors;
import com.paperbriq.app.AppController;
import com.paperbriq.app.ui.Ui;

public class ProcessTrackingScreen implements AppScreen {
    @Override
    public String key() {
        return "process";
    }

    @Override
    public String navLabel() {
        return "Process";
    }

    @Override
    public String title() {
        return "Process Tracking";
    }

    @Override
    public String subtitle() {
        return "Track every stage from paper collection to briquette drying.";
    }

    @Override
    public void render(LinearLayout root, AppController controller) {
        Ui ui = controller.ui();
        root.addView(ui.sectionTitle("Active Batch", "PBQ-2026-0701-A"));
        root.addView(ScreenPieces.processStepsCard(ui));

        root.addView(ui.sectionTitle("Machine Queue", ""));
        root.addView(ui.simpleList(new String[][]{
            {"Collect", "Collect Paper", "45 kg / 60 kg", "Completed"},
            {"Shred", "Shred Paper", "Shredding at 78%", "In Progress"},
            {"Pulp", "Pulp and Mix", "Hydration at 60%", "In Progress"},
            {"Mold", "Press and Mold", "0 / 20 briquettes", "Not Started"},
            {"Dry", "Drying", "Waiting to start", "Not Started"}
        }));

        root.addView(ui.sectionTitle("Controls", ""));
        ui.addGrid(root, new android.view.View[]{
            ui.actionCard("Start Process", "Play"),
            ui.actionCard("Pause Batch", "Pause"),
            ui.actionCard("Add Rainwater", "Water"),
            ui.actionCard("Run Safety Check", "Check")
        }, 2);

        root.addView(ui.sectionTitle("Quality Checks", ""));
        root.addView(ui.infoListCard("Quality Checklist", new String[][]{
            {"Moisture", "8.5% optimal"},
            {"Mold Pressure", "2.3 bar"},
            {"Drying Temperature", "55 C"},
            {"Safety Interlock", "Locked"},
            {"Batch Status", "Ready for next stage"}
        }));

        ui.addGrid(root, new android.view.View[]{
            ui.statCard("Estimated Time", "1 hr 20 mins", "Remaining", AppColors.GREEN),
            ui.statCard("Total to Produce", "20 bricks", "Target output", AppColors.AMBER),
            ui.statCard("Completed", "8 bricks", "Current output", AppColors.GREEN)
        }, 1);
    }
}
