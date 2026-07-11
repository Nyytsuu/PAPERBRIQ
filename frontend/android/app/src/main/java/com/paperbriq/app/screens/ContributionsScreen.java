package com.paperbriq.app.screens;

import android.widget.LinearLayout;

import com.paperbriq.app.AppColors;
import com.paperbriq.app.AppController;
import com.paperbriq.app.ui.Ui;

public class ContributionsScreen implements AppScreen {
    @Override
    public String key() {
        return "contributions";
    }

    @Override
    public String navLabel() {
        return "Contributors";
    }

    @Override
    public String title() {
        return "User Contributions";
    }

    @Override
    public String subtitle() {
        return "Manage user paper donations and earned points.";
    }

    @Override
    public void render(LinearLayout root, AppController controller) {
        Ui ui = controller.ui();
        ui.addGrid(root, new android.view.View[]{
            ui.statCard("Total Contributors", "15", "+3 from last week", AppColors.GREEN),
            ui.statCard("Paper Donated", "45 kg", "+12 kg from last week", AppColors.BLUE),
            ui.statCard("Briquettes Produced", "8 / 20", "40% of weekly target", AppColors.AMBER)
        }, 1);

        root.addView(ui.sectionTitle("Search and Filters", ""));
        ui.addGrid(root, new android.view.View[]{
            ui.filterCard("Search", "Contributor, barangay, or scan ID"),
            ui.filterCard("Barangay", "All Barangays"),
            ui.filterCard("Status", "All Status"),
            ui.filterCard("Date", "May 12 - May 18, 2026")
        }, 1);

        root.addView(ui.sectionTitle("Contributors", ""));
        root.addView(ui.simpleList(new String[][]{
            {"Juan", "Juan Dela Cruz", "San Isidro - 5.0 kg", "50 pts"},
            {"Maria", "Maria Santos", "Newhope - 4.5 kg", "45 pts"},
            {"Pedro", "Pedro Reyes", "San Isidro - 6.0 kg", "60 pts"},
            {"Ana", "Ana Lopez", "Del Pilar - 3.0 kg", "30 pts"},
            {"Mark", "Mark Villanueva", "Newhope - 2.5 kg", "25 pts"},
            {"Liza", "Liza Garcia", "San Isidro - 4.0 kg", "40 pts"},
            {"Ramon", "Ramon Bautista", "Del Pilar - 1.5 kg", "15 pts"},
            {"Karla", "Karla Mendoza", "Newhope - 3.5 kg", "35 pts"},
            {"Diego", "Diego Cruz", "San Isidro - 2.0 kg", "20 pts"},
            {"Faith", "Faith Domingo", "Del Pilar - 4.5 kg", "45 pts"}
        }));

        root.addView(ui.sectionTitle("Top Contributors This Week", "View All"));
        ui.addGrid(root, new android.view.View[]{
            ScreenPieces.contributorCard(ui, "1", "Pedro Reyes", "San Isidro", "6.0 kg", "60 pts"),
            ScreenPieces.contributorCard(ui, "2", "Juan Dela Cruz", "San Isidro", "5.0 kg", "50 pts"),
            ScreenPieces.contributorCard(ui, "3", "Maria Santos", "Newhope", "4.5 kg", "45 pts"),
            ScreenPieces.contributorCard(ui, "4", "Faith Domingo", "Del Pilar", "4.5 kg", "45 pts"),
            ScreenPieces.contributorCard(ui, "5", "Ana Lopez", "Del Pilar", "3.0 kg", "30 pts")
        }, 1);
    }
}
