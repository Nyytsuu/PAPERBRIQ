package com.paperbriq.app.screens;

import android.graphics.Color;
import android.graphics.Typeface;
import android.widget.LinearLayout;

import com.paperbriq.app.AppColors;
import com.paperbriq.app.AppController;
import com.paperbriq.app.ui.Ui;

public class RewardsScreen implements AppScreen {
    @Override
    public String key() {
        return "rewards";
    }

    @Override
    public String navLabel() {
        return "Rewards";
    }

    @Override
    public String title() {
        return "Rewards & Points";
    }

    @Override
    public String subtitle() {
        return "Redeem points and monitor contributor incentives.";
    }

    @Override
    public void render(LinearLayout root, AppController controller) {
        Ui ui = controller.ui();
        LinearLayout total = ui.card(20);
        total.setPadding(ui.dp(18), ui.dp(18), ui.dp(18), ui.dp(18));
        total.setBackground(ui.round(AppColors.GREEN, ui.dp(20), Color.TRANSPARENT, 0));
        total.addView(ui.text("Total Points", 16, Color.WHITE, Typeface.BOLD));
        total.addView(ui.text("320 pts", 36, Color.WHITE, Typeface.BOLD));
        total.addView(ui.text("Keep recycling!", 14, Color.WHITE, Typeface.NORMAL));
        root.addView(total, ui.fullWidth(LinearLayout.LayoutParams.WRAP_CONTENT, 0, 0, 0, ui.dp(10)));

        ui.addGrid(root, new android.view.View[]{
            ui.statCard("Points Earned", "320 pts", "+80 pts vs last month", AppColors.GREEN),
            ui.statCard("Points Redeemed", "120 pts", "-20 pts vs last month", AppColors.RED),
            ui.statCard("Available Points", "320 pts", "Ready to redeem", AppColors.GREEN)
        }, 1);

        ui.addGrid(root, new android.view.View[]{
            ui.infoListCard("Points History", new String[][]{
                {"+45 pts", "Paper Collection"},
                {"+30 pts", "Pulping Mixture Contribution"},
                {"+25 pts", "System Check Bonus"},
                {"-60 pts", "Redeemed Reward"},
                {"+20 pts", "Paper Collection"},
                {"+15 pts", "Referral Bonus"}
            }),
            rewardTier(ui)
        }, 1);

        root.addView(ui.sectionTitle("Redeem Rewards", "View All"));
        ui.addGrid(root, new android.view.View[]{
            ScreenPieces.rewardCard(ui, "Eco Store Voucher P100", "100 pts"),
            ScreenPieces.rewardCard(ui, "Eco Store Voucher P250", "250 pts"),
            ScreenPieces.rewardCard(ui, "Eco Tote Bag", "300 pts"),
            ScreenPieces.rewardCard(ui, "Plant a Tree", "350 pts"),
            ScreenPieces.rewardCard(ui, "Eco Water Bottle", "400 pts")
        }, 1);

        ui.addGrid(root, new android.view.View[]{
            ui.infoListCard("Reward Categories", new String[][]{
                {"Vouchers and Discounts", "Save on eco-friendly products"},
                {"Eco Products", "Sustainable items for you"},
                {"Green Donations", "Support environmental causes"},
                {"Experience and Events", "Workshops, tours, and more"}
            }),
            ui.infoListCard("Top Contributors", new String[][]{
                {"Maria Santos", "1,250 pts"},
                {"Juan Dela Cruz", "980 pts"},
                {"Ana Reyes", "860 pts"},
                {"Liza Ramirez", "720 pts"},
                {"Mark Villanueva", "650 pts"}
            })
        }, 1);
    }

    private android.view.View rewardTier(Ui ui) {
        LinearLayout card = ui.card(18);
        card.setPadding(ui.dp(16), ui.dp(16), ui.dp(16), ui.dp(16));
        card.addView(ui.text("Next Reward Tier", 17, AppColors.INK, Typeface.BOLD));
        card.addView(ui.text("Silver Recycler", 16, AppColors.INK, Typeface.BOLD));
        card.addView(ui.text("500 pts", 26, AppColors.INK, Typeface.BOLD));
        card.addView(ui.progressBar(64, AppColors.GREEN), ui.fullWidth(ui.dp(10), 0, ui.dp(8), 0, ui.dp(8)));
        card.addView(ui.text("320 / 500 pts. 180 pts to reach Silver Recycler.", 13, AppColors.MUTED, Typeface.NORMAL));
        card.addView(ui.infoRow("Benefit", "5% bonus points"));
        card.addView(ui.infoRow("Benefit", "Priority in rewards"));
        card.addView(ui.infoRow("Benefit", "Exclusive PaperBriq badges"));
        return card;
    }
}
