package com.paperbriq.app.screens;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Color;
import android.graphics.Typeface;
import android.util.Base64;
import android.view.Gravity;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;

import com.paperbriq.app.AppColors;
import com.paperbriq.app.AppController;
import com.paperbriq.app.ui.Ui;

import org.json.JSONArray;
import org.json.JSONObject;

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
        JSONObject rewardsData = controller.session().pagePayload("rewards");
        JSONObject totals = rewardsData.optJSONObject("totals");
        if (totals == null) totals = new JSONObject();
        JSONArray history = rewardsData.optJSONArray("history");
        JSONArray rewards = rewardsData.optJSONArray("rewards");

        LinearLayout total = ui.card(20);
        total.setPadding(ui.dp(18), ui.dp(18), ui.dp(18), ui.dp(18));
        total.setBackground(ui.round(AppColors.GREEN, ui.dp(20), Color.TRANSPARENT, 0));
        total.addView(ui.text("Total Points", 16, Color.WHITE, Typeface.BOLD));
        total.addView(ui.text(ui.formatNumber(totals.optDouble("totalPoints", 0)) + " pts", 36, Color.WHITE, Typeface.BOLD));
        total.addView(ui.text("From point transaction records", 14, Color.WHITE, Typeface.NORMAL));
        root.addView(total, ui.fullWidth(LinearLayout.LayoutParams.WRAP_CONTENT, 0, 0, 0, ui.dp(10)));

        ui.addGrid(root, new android.view.View[]{
            ui.statCard("Points Earned", ui.formatNumber(totals.optDouble("pointsEarnedThisMonth", 0)) + " pts", "This month", AppColors.GREEN),
            ui.statCard("Points Redeemed", ui.formatNumber(totals.optDouble("pointsRedeemedThisMonth", 0)) + " pts", "This month", AppColors.RED),
            ui.statCard("Available Points", ui.formatNumber(totals.optDouble("availablePoints", 0)) + " pts", "From MySQL", AppColors.GREEN)
        }, 1);

        ui.addGrid(root, new android.view.View[]{
            ui.infoListCard("Points History", historyRows(history, ui)),
            ui.infoListCard("Reward Monitor", monitorRows(rewardsData.optJSONObject("monitor"), ui))
        }, 1);

        root.addView(ui.sectionTitle("Redeem Rewards", "View All"));
        ui.addGrid(root, rewardCards(ui, controller, rewards), 2);

        ui.addGrid(root, new android.view.View[]{
            ui.infoListCard("Reward Categories", categoryRows(rewards)),
            ui.infoListCard("Redemption Stats", redemptionRows(rewardsData.optJSONArray("redemptionStats"), ui))
        }, 1);
    }

    private String[][] historyRows(JSONArray history, Ui ui) {
        if (history == null || history.length() == 0) {
            return new String[][]{{"--", "No point transactions yet"}};
        }
        String[][] rows = new String[history.length()][2];
        for (int index = 0; index < history.length(); index++) {
            JSONObject item = history.optJSONObject(index);
            if (item == null) item = new JSONObject();
            rows[index][0] = ui.formatNumber(item.optDouble("points", 0)) + " pts";
            rows[index][1] = item.optString("title", "Point transaction") + " - " + item.optString("happenedAt", "");
        }
        return rows;
    }

    private android.view.View[] rewardCards(Ui ui, AppController controller, JSONArray rewards) {
        if (rewards == null || rewards.length() == 0) {
            return new android.view.View[]{emptyRewardCard(ui)};
        }
        android.view.View[] views = new android.view.View[rewards.length()];
        for (int index = 0; index < rewards.length(); index++) {
            JSONObject reward = rewards.optJSONObject(index);
            if (reward == null) reward = new JSONObject();
            JSONObject selectedReward = reward;
            LinearLayout card = ui.card(18);
            card.setGravity(Gravity.CENTER);
            card.setPadding(ui.dp(10), ui.dp(10), ui.dp(10), ui.dp(12));
            card.setClickable(true);
            card.addView(rewardImage(ui, controller, reward), ui.fullWidth(ui.dp(132), 0, 0, 0, ui.dp(8)));
            TextView points = ui.text(ui.formatNumber(reward.optDouble("pointsCost", 0)) + " pts", 18, AppColors.GREEN_DARK, Typeface.BOLD);
            points.setGravity(Gravity.CENTER);
            card.addView(points, ui.matchWrap());
            card.setOnClickListener(v -> controller.showRewardDetails(selectedReward));
            views[index] = card;
        }
        return views;
    }

    private View emptyRewardCard(Ui ui) {
        LinearLayout card = ui.card(18);
        card.setGravity(Gravity.CENTER);
        card.setPadding(ui.dp(18), ui.dp(18), ui.dp(18), ui.dp(18));
        card.addView(ui.iconBadge("rewards", AppColors.GREEN, 72), new LinearLayout.LayoutParams(ui.dp(72), ui.dp(72)));
        TextView label = ui.text("No rewards yet", 16, AppColors.INK, Typeface.BOLD);
        label.setGravity(Gravity.CENTER);
        card.addView(label, ui.matchWrap());
        card.addView(ui.text("Ask barangay staff to add items.", 13, AppColors.MUTED, Typeface.BOLD));
        return card;
    }

    private View rewardImage(Ui ui, AppController controller, JSONObject reward) {
        FrameLayout shell = new FrameLayout(controller.activity());
        shell.setPadding(ui.dp(8), ui.dp(8), ui.dp(8), ui.dp(8));
        shell.setBackground(ui.round(AppColors.GREEN_SOFT, ui.dp(16), Color.TRANSPARENT, 0));
        Bitmap bitmap = decodeRewardBitmap(reward.optString("imageUrl", ""));
        if (bitmap != null) {
            ImageView image = new ImageView(controller.activity());
            image.setImageBitmap(bitmap);
            image.setScaleType(ImageView.ScaleType.CENTER_CROP);
            shell.addView(image, new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT, Gravity.CENTER));
        } else {
            shell.addView(ui.iconBadge(ui.iconNameFor(reward.optString("name", "reward")), AppColors.GREEN, 88), new FrameLayout.LayoutParams(ui.dp(88), ui.dp(88), Gravity.CENTER));
        }
        return shell;
    }

    private Bitmap decodeRewardBitmap(String imageUrl) {
        String value = String.valueOf(imageUrl == null ? "" : imageUrl).trim();
        if (value.isEmpty()) return null;
        int comma = value.indexOf(",");
        String encoded = comma >= 0 ? value.substring(comma + 1) : value;
        try {
            byte[] bytes = Base64.decode(encoded, Base64.DEFAULT);
            return BitmapFactory.decodeByteArray(bytes, 0, bytes.length);
        } catch (Exception ignored) {
            return null;
        }
    }

    private String[][] monitorRows(JSONObject monitor, Ui ui) {
        if (monitor == null) monitor = new JSONObject();
        return new String[][]{
            {"Reward Items", ui.formatNumber(monitor.optDouble("rewardItems", 0))},
            {"Total Stock", ui.formatNumber(monitor.optDouble("totalStock", 0))},
            {"Pending Claims", ui.formatNumber(monitor.optDouble("pendingClaims", 0))}
        };
    }

    private String[][] categoryRows(JSONArray rewards) {
        if (rewards == null || rewards.length() == 0) {
            return new String[][]{{"--", "No reward categories yet"}};
        }
        java.util.LinkedHashMap<String, Integer> counts = new java.util.LinkedHashMap<>();
        for (int index = 0; index < rewards.length(); index++) {
            JSONObject reward = rewards.optJSONObject(index);
            if (reward == null) reward = new JSONObject();
            String category = reward.optString("category", "Uncategorized");
            counts.put(category, counts.containsKey(category) ? counts.get(category) + 1 : 1);
        }
        String[][] rows = new String[counts.size()][2];
        int index = 0;
        for (java.util.Map.Entry<String, Integer> entry : counts.entrySet()) {
            rows[index][0] = entry.getKey();
            rows[index][1] = entry.getValue() + " item" + (entry.getValue() == 1 ? "" : "s");
            index++;
        }
        return rows;
    }

    private String[][] redemptionRows(JSONArray stats, Ui ui) {
        if (stats == null || stats.length() == 0) {
            return new String[][]{{"--", "No redemptions yet"}};
        }
        String[][] rows = new String[stats.length()][2];
        for (int index = 0; index < stats.length(); index++) {
            JSONObject item = stats.optJSONObject(index);
            if (item == null) item = new JSONObject();
            rows[index][0] = item.optString("status", "Status");
            rows[index][1] = ui.formatNumber(item.optDouble("total", 0)) + " claims - " + ui.formatNumber(item.optDouble("points", 0)) + " pts";
        }
        return rows;
    }
}
