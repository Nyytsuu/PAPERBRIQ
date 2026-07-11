package com.paperbriq.app.screens;

import android.view.Gravity;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.TextView;

import com.paperbriq.app.AppColors;
import com.paperbriq.app.ui.Ui;

public final class ScreenPieces {
    private ScreenPieces() {
    }

    public static View processStepsCard(Ui ui) {
        return ui.simpleList(new String[][]{
            {"Done", "Collect Paper", "Collected: 45 kg / 60 kg"},
            {"Live", "Shred Paper", "Shredding at 78%"},
            {"Live", "Pulp and Mix", "Pulping mixture at 60%"},
            {"Next", "Press and Mold", "0 / 20 briquettes"},
            {"Next", "Drying", "Waiting to start"}
        });
    }

    public static View rewardCard(Ui ui, String name, String cost) {
        LinearLayout card = ui.card(16);
        card.setGravity(Gravity.CENTER);
        card.setPadding(ui.dp(14), ui.dp(14), ui.dp(14), ui.dp(14));
        card.addView(ui.text("Reward", 16, AppColors.GREEN, android.graphics.Typeface.BOLD));
        TextView label = ui.text(name, 14, AppColors.INK, android.graphics.Typeface.BOLD);
        label.setGravity(Gravity.CENTER);
        card.addView(label);
        card.addView(ui.text(cost, 14, AppColors.GREEN, android.graphics.Typeface.BOLD));
        card.addView(ui.outlineButton("Redeem"), new LinearLayout.LayoutParams(ui.dp(112), ui.dp(42)));
        return card;
    }

    public static View contributorCard(Ui ui, String rank, String name, String barangay, String weight, String points) {
        LinearLayout card = ui.card(18);
        card.setGravity(Gravity.CENTER);
        card.setPadding(ui.dp(14), ui.dp(14), ui.dp(14), ui.dp(14));
        card.addView(ui.pill(rank, AppColors.AMBER));
        TextView person = ui.text(name, 15, AppColors.INK, android.graphics.Typeface.BOLD);
        person.setGravity(Gravity.CENTER);
        card.addView(person);
        TextView place = ui.text("Barangay " + barangay, 12, AppColors.MUTED, android.graphics.Typeface.NORMAL);
        place.setGravity(Gravity.CENTER);
        card.addView(place);
        card.addView(ui.text(weight + "  |  " + points, 15, AppColors.GREEN_DARK, android.graphics.Typeface.BOLD));
        return card;
    }
}
