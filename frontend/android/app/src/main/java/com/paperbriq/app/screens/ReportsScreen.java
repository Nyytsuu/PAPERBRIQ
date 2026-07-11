package com.paperbriq.app.screens;

import android.widget.LinearLayout;

import com.paperbriq.app.AppColors;
import com.paperbriq.app.AppController;
import com.paperbriq.app.ui.Ui;

public class ReportsScreen implements AppScreen {
    @Override
    public String key() {
        return "reports";
    }

    @Override
    public String navLabel() {
        return "Reports";
    }

    @Override
    public String title() {
        return "Reports";
    }

    @Override
    public String subtitle() {
        return "Track performance, impact, and contributions over time.";
    }

    @Override
    public void render(LinearLayout root, AppController controller) {
        Ui ui = controller.ui();
        ui.addGrid(root, new android.view.View[]{
            ui.filterCard("Date Range", "May 01 - May 31, 2026"),
            ui.filterCard("Barangay", "All Barangays")
        }, 1);
        ui.addGrid(root, new android.view.View[]{
            ui.statCard("Paper Collected", "1,245 kg", "+18.6% vs Apr", AppColors.GREEN),
            ui.statCard("Briquettes Produced", "248 / 620", "+12.4% vs Apr", AppColors.AMBER),
            ui.statCard("Water Saved", "3,120 L", "+15.2% vs Apr", AppColors.BLUE),
            ui.statCard("Active Contributors", "86", "+10.3% vs Apr", AppColors.GREEN)
        }, 2);

        root.addView(ui.sectionTitle("Charts", ""));
        ui.addGrid(root, new android.view.View[]{
            ui.barChartCard("Paper Collected (kg)", new String[][]{{"May 1", "780"}, {"May 8", "1050"}, {"May 15", "920"}, {"May 22", "1280"}, {"May 29", "1180"}}),
            ui.barChartCard("Briquettes Produced (pcs)", new String[][]{{"May 1", "180"}, {"May 8", "210"}, {"May 15", "250"}, {"May 22", "170"}, {"May 29", "252"}}),
            ui.barChartCard("Water Saved (L)", new String[][]{{"May 1", "2000"}, {"May 8", "2400"}, {"May 15", "2920"}, {"May 22", "3100"}, {"May 29", "2960"}})
        }, 1);

        root.addView(ui.sectionTitle("Monthly Output Overview", ""));
        LinearLayout trend = ui.card(18);
        trend.setPadding(ui.dp(16), ui.dp(16), ui.dp(16), ui.dp(16));
        trend.addView(ui.text("Paper, briquettes, and water saved trend upward this month.", 14, AppColors.MUTED, android.graphics.Typeface.NORMAL));
        trend.addView(ui.sparkline(AppColors.GREEN), ui.fullWidth(ui.dp(42), 0, ui.dp(14), 0, ui.dp(8)));
        trend.addView(ui.sparkline(AppColors.AMBER), ui.fullWidth(ui.dp(42), 0, 0, 0, ui.dp(8)));
        trend.addView(ui.sparkline(AppColors.BLUE), ui.fullWidth(ui.dp(42), 0, 0, 0, 0));
        root.addView(trend);

        ui.addGrid(root, new android.view.View[]{
            ui.infoListCard("Top Barangays", new String[][]{
                {"San Isidro", "320 kg - 28 contributors"},
                {"San Roque", "285 kg - 22 contributors"},
                {"Poblacion", "245 kg - 18 contributors"},
                {"Santo Nino", "200 kg - 12 contributors"}
            }),
            ui.infoListCard("Environmental Impact", new String[][]{
                {"Trees Saved", "18 this month"},
                {"CO2 Reduced", "312 kg this month"},
                {"Energy Generated", "240 kWh"},
                {"Landfill Diversion", "1,245 kg"}
            }),
            ui.infoListCard("Report Summary", new String[][]{
                {"Period", "May 01 - May 31, 2026"},
                {"Data", "System records up-to-date"},
                {"Comparison", "Compared to Apr 2026"}
            })
        }, 1);
    }
}
