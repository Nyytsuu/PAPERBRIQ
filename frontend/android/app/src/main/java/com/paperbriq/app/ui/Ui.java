package com.paperbriq.app.ui;

import android.app.Activity;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;

import com.paperbriq.app.AppColors;
import com.paperbriq.app.R;

import java.util.Locale;

public class Ui {
    private final Activity activity;

    public Ui(Activity activity) {
        this.activity = activity;
    }

    public LinearLayout row() {
        LinearLayout layout = new LinearLayout(activity);
        layout.setOrientation(LinearLayout.HORIZONTAL);
        return layout;
    }

    public LinearLayout column() {
        LinearLayout layout = new LinearLayout(activity);
        layout.setOrientation(LinearLayout.VERTICAL);
        return layout;
    }

    public LinearLayout card(int radiusDp) {
        LinearLayout layout = column();
        layout.setBackground(round(Color.WHITE, dp(radiusDp), AppColors.LINE, 1));
        return layout;
    }

    public TextView text(String value, int sp, int color, int style) {
        TextView view = new TextView(activity);
        view.setText(value);
        view.setTextSize(sp);
        view.setTextColor(color);
        view.setTypeface(Typeface.DEFAULT, style);
        view.setIncludeFontPadding(true);
        return view;
    }

    public View brandMark(int sizeDp, int textSize) {
        ImageView mark = new ImageView(activity);
        mark.setImageResource(R.drawable.logo_paperbriq_mark);
        mark.setScaleType(ImageView.ScaleType.FIT_CENTER);
        mark.setAdjustViewBounds(false);
        mark.setContentDescription(null);
        mark.setPadding(dp(Math.max(2, sizeDp / 12)), dp(Math.max(2, sizeDp / 12)), dp(Math.max(2, sizeDp / 12)), dp(Math.max(2, sizeDp / 12)));
        return mark;
    }

    public Button greenButton(String label) {
        Button button = new Button(activity);
        button.setText(label);
        button.setAllCaps(false);
        button.setTextColor(Color.WHITE);
        button.setTextSize(15);
        button.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        button.setBackground(round(AppColors.GREEN, dp(14), Color.TRANSPARENT, 0));
        return button;
    }

    public Button outlineButton(String label) {
        Button button = new Button(activity);
        button.setText(label);
        button.setAllCaps(false);
        button.setTextColor(AppColors.GREEN);
        button.setTextSize(14);
        button.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        button.setBackground(round(Color.WHITE, dp(14), AppColors.GREEN, 1));
        return button;
    }

    public EditText editText(String hint, boolean password) {
        EditText input = new EditText(activity);
        input.setHint(hint);
        input.setTextSize(15);
        input.setSingleLine(true);
        input.setTextColor(AppColors.INK);
        input.setHintTextColor(Color.rgb(135, 148, 167));
        input.setPadding(dp(14), 0, dp(14), 0);
        input.setBackground(round(Color.WHITE, dp(14), AppColors.LINE, 1));
        input.setInputType(password ? InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD : InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_EMAIL_ADDRESS);
        return input;
    }

    public TextView pill(String label, int color) {
        TextView pill = text(label, 11, color, Typeface.BOLD);
        pill.setGravity(Gravity.CENTER);
        pill.setPadding(dp(9), dp(5), dp(9), dp(5));
        pill.setBackground(round(Color.argb(26, Color.red(color), Color.green(color), Color.blue(color)), dp(14), Color.TRANSPARENT, 0));
        return pill;
    }

    public View sectionTitle(String title, String action) {
        LinearLayout row = row();
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(0, dp(18), 0, dp(10));
        row.addView(text(title, 18, AppColors.INK, Typeface.BOLD), new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        if (!action.isEmpty()) {
            row.addView(pill(action, AppColors.GREEN));
        }
        return row;
    }

    public View statCard(String title, String value, String subtitle, int accent) {
        LinearLayout card = card(18);
        card.setPadding(dp(14), dp(14), dp(14), dp(14));
        card.addView(iconBadge(iconNameFor(title), accent, 42), new LinearLayout.LayoutParams(dp(42), dp(42)));
        card.addView(text(title, 13, AppColors.MUTED, Typeface.BOLD));
        card.addView(text(value, 24, AppColors.INK, Typeface.BOLD));
        card.addView(text(subtitle, 12, AppColors.MUTED, Typeface.NORMAL));
        return card;
    }

    public View sensorCard(String title, String value, String status, int accent) {
        LinearLayout card = (LinearLayout) statCard(title, value, status, accent);
        card.addView(sparkline(accent), fullWidth(dp(46), 0, dp(8), 0, 0));
        return card;
    }

    public View gaugeCard(String title, String value, String subtitle) {
        LinearLayout card = card(18);
        card.setGravity(Gravity.CENTER);
        card.setPadding(dp(14), dp(16), dp(14), dp(16));
        card.addView(iconBadge(iconNameFor(title), AppColors.GREEN, 40), new LinearLayout.LayoutParams(dp(40), dp(40)));
        card.addView(text(title, 14, AppColors.INK, Typeface.BOLD));
        TextView gauge = text(value, 22, AppColors.GREEN_DARK, Typeface.BOLD);
        gauge.setGravity(Gravity.CENTER);
        gauge.setBackground(round(AppColors.GREEN_SOFT, dp(42), AppColors.GREEN, 8));
        card.addView(gauge, new LinearLayout.LayoutParams(dp(92), dp(92)));
        card.addView(text(subtitle, 13, AppColors.MUTED, Typeface.NORMAL));
        card.addView(pill("Normal", AppColors.GREEN));
        return card;
    }

    public View actionCard(String title, String icon) {
        LinearLayout card = card(16);
        card.setGravity(Gravity.CENTER);
        card.setPadding(dp(12), dp(14), dp(12), dp(14));
        card.addView(iconBadge(iconNameFor(icon + " " + title), AppColors.GREEN, 44), new LinearLayout.LayoutParams(dp(44), dp(44)));
        TextView label = text(title, 13, AppColors.GREEN_DARK, Typeface.BOLD);
        label.setGravity(Gravity.CENTER);
        card.addView(label);
        return card;
    }

    public View filterCard(String label, String value) {
        LinearLayout card = card(14);
        card.setPadding(dp(14), dp(12), dp(14), dp(12));
        card.addView(text(label, 12, AppColors.MUTED, Typeface.BOLD));
        card.addView(text(value, 15, AppColors.INK, Typeface.BOLD));
        return card;
    }

    public View infoListCard(String title, String[][] rows) {
        LinearLayout card = card(18);
        card.setPadding(dp(16), dp(16), dp(16), dp(16));
        card.addView(text(title, 17, AppColors.INK, Typeface.BOLD));
        for (String[] row : rows) {
            card.addView(infoRow(row[0], row[1]));
        }
        return card;
    }

    public View infoRow(String label, String value) {
        LinearLayout row = row();
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(0, dp(10), 0, dp(8));
        row.addView(text(label, 13, AppColors.MUTED, Typeface.BOLD), new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        TextView valueView = text(value, 13, AppColors.INK, Typeface.BOLD);
        valueView.setGravity(Gravity.RIGHT);
        row.addView(valueView, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        return row;
    }

    public View simpleList(String[][] rows) {
        LinearLayout card = card(18);
        card.setPadding(dp(12), dp(8), dp(12), dp(8));
        for (String[] row : rows) {
            LinearLayout item = row();
            item.setGravity(Gravity.CENTER_VERTICAL);
            item.setPadding(0, dp(10), 0, dp(10));

            String status = row[row.length - 1];
            int color = status.toLowerCase(Locale.ROOT).contains("warning") ? AppColors.RED : AppColors.GREEN;
            LinearLayout leading = column();
            leading.setGravity(Gravity.CENTER);
            leading.addView(iconBadge(iconNameFor(row[0] + " " + row[1] + " " + status), color, 34), new LinearLayout.LayoutParams(dp(34), dp(34)));
            String leadingText = row[0].trim();
            if (!leadingText.isEmpty() && !"--".equals(leadingText) && !leadingText.matches("[A-Z]{1,3}|\\d+")) {
                TextView leadingLabel = text(row[0], 9, AppColors.MUTED, Typeface.BOLD);
                leadingLabel.setGravity(Gravity.CENTER);
                leading.addView(leadingLabel);
            }
            item.addView(leading, new LinearLayout.LayoutParams(dp(76), LinearLayout.LayoutParams.WRAP_CONTENT));

            LinearLayout copy = column();
            copy.addView(text(row[1], 14, AppColors.INK, Typeface.BOLD));
            if (row.length > 3) {
                copy.addView(text(row[2], 12, AppColors.MUTED, Typeface.NORMAL));
            }
            item.addView(copy, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));

            item.addView(pill(status, color));
            card.addView(item);
        }
        return card;
    }

    public View barChartCard(String title, String[][] rows) {
        LinearLayout card = card(18);
        card.setPadding(dp(16), dp(16), dp(16), dp(16));
        card.addView(text(title, 16, AppColors.INK, Typeface.BOLD));
        for (String[] row : rows) {
            double value = safeChartValue(row[1]);
            LinearLayout line = row();
            line.setGravity(Gravity.CENTER_VERTICAL);
            line.setPadding(0, dp(8), 0, 0);
            line.addView(text(row[0], 12, AppColors.MUTED, Typeface.BOLD), new LinearLayout.LayoutParams(dp(58), LinearLayout.LayoutParams.WRAP_CONTENT));
            TextView bar = text("", 1, Color.WHITE, Typeface.NORMAL);
            bar.setBackground(round(AppColors.GREEN, dp(5), Color.TRANSPARENT, 0));
            line.addView(bar, new LinearLayout.LayoutParams(Math.min(dp(190), Math.max(dp(42), dp((int) Math.round(value / 20)))), dp(10)));
            TextView valueText = text(row[1], 12, AppColors.MUTED, Typeface.BOLD);
            valueText.setPadding(dp(8), 0, 0, 0);
            line.addView(valueText);
            card.addView(line);
        }
        return card;
    }

    private double safeChartValue(String value) {
        try {
            return Math.max(0, Double.parseDouble(String.valueOf(value).replace(",", "").trim()));
        } catch (Exception ignored) {
            return 0;
        }
    }

    public View sparkline(int color) {
        LinearLayout line = row();
        line.setGravity(Gravity.BOTTOM);
        int[] heights = {12, 18, 14, 24, 16, 20, 28, 18, 22, 32};
        for (int height : heights) {
            TextView bar = text("", 1, color, Typeface.NORMAL);
            bar.setBackground(round(color, dp(4), Color.TRANSPARENT, 0));
            LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(0, dp(height), 1);
            params.setMargins(dp(2), 0, dp(2), 0);
            line.addView(bar, params);
        }
        return line;
    }

    public View progressBar(int percent, int color) {
        FrameLayout track = new FrameLayout(activity);
        track.setBackground(round(Color.rgb(224, 228, 232), dp(5), Color.TRANSPARENT, 0));
        TextView fill = text("", 1, color, Typeface.NORMAL);
        fill.setBackground(round(color, dp(5), Color.TRANSPARENT, 0));
        track.addView(fill, new FrameLayout.LayoutParams(dp(Math.max(12, percent * 2)), FrameLayout.LayoutParams.MATCH_PARENT));
        return track;
    }

    public View progressCard(String title, String value, String subtitle) {
        LinearLayout card = card(20);
        card.setGravity(Gravity.CENTER);
        card.setPadding(dp(18), dp(20), dp(18), dp(20));
        card.addView(text(title, 18, AppColors.INK, Typeface.BOLD));
        TextView circle = text(value, 36, AppColors.GREEN_DARK, Typeface.BOLD);
        circle.setGravity(Gravity.CENTER);
        circle.setBackground(round(Color.WHITE, dp(70), AppColors.GREEN, 10));
        card.addView(circle, new LinearLayout.LayoutParams(dp(140), dp(140)));
        TextView sub = text(subtitle, 14, AppColors.MUTED, Typeface.NORMAL);
        sub.setGravity(Gravity.CENTER);
        card.addView(sub);
        return card;
    }

    public void addGrid(LinearLayout root, View[] cards, int columns) {
        LinearLayout row = null;
        for (int i = 0; i < cards.length; i++) {
            if (i % columns == 0) {
                row = row();
                root.addView(row, fullWidth(LinearLayout.LayoutParams.WRAP_CONTENT, 0, 0, 0, dp(10)));
            }
            LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1);
            params.setMargins(dp(4), 0, dp(4), 0);
            cards[i].setLayoutParams(params);
            row.addView(cards[i]);
        }
    }

    public GradientDrawable round(int fill, int radius, int strokeColor, int strokeWidth) {
        GradientDrawable drawable = new GradientDrawable();
        drawable.setColor(fill);
        drawable.setCornerRadius(radius);
        if (strokeWidth > 0) {
            drawable.setStroke(strokeWidth, strokeColor);
        }
        return drawable;
    }

    public LinearLayout.LayoutParams fullWidth(int height, int left, int top, int right, int bottom) {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, height);
        params.setMargins(left, top, right, bottom);
        return params;
    }

    public LinearLayout.LayoutParams matchWrap() {
        return new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
    }

    public int dp(int value) {
        return Math.round(value * activity.getResources().getDisplayMetrics().density);
    }

    public String formatNumber(double value) {
        if (value == Math.rint(value)) {
            return String.format(Locale.US, "%,.0f", value);
        }
        return String.format(Locale.US, "%,.2f", value);
    }

    public View iconBadge(String name, int accent, int sizeDp) {
        FrameLayout badge = new FrameLayout(activity);
        badge.setPadding(dp(8), dp(8), dp(8), dp(8));
        badge.setBackground(round(Color.argb(32, Color.red(accent), Color.green(accent), Color.blue(accent)), dp(Math.max(12, sizeDp / 3)), Color.TRANSPARENT, 0));
        PaperIconView icon = new PaperIconView(activity, name, accent);
        badge.addView(icon, new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT, Gravity.CENTER));
        return badge;
    }

    public String iconNameFor(String label) {
        String value = String.valueOf(label).toLowerCase(Locale.ROOT);
        if (value.contains("dashboard") || value.contains("overview")) return "dashboard";
        if (value.contains("process") || value.contains("progress") || value.contains("batch") || value.contains("shred") || value.contains("pulp")) return "process";
        if (value.contains("iot") || value.contains("sensor") || value.contains("network") || value.contains("packet") || value.contains("sync") || value.contains("online")) return "iot";
        if (value.contains("contributor") || value.contains("staff") || value.contains("user") || value.contains("active")) return "contributors";
        if (value.contains("reward") || value.contains("point") || value.contains("gift") || value.contains("redeem")) return "rewards";
        if (value.contains("report") || value.contains("data") || value.contains("export")) return "reports";
        if (value.contains("history") || value.contains("time") || value.contains("runtime") || value.contains("session")) return "history";
        if (value.contains("setting") || value.contains("security") || value.contains("rule")) return "settings";
        if (value.contains("scan") || value.contains("qr") || value.contains("code")) return "scan";
        if (value.contains("rain") || value.contains("water") || value.contains("flow") || value.contains("tds")) return "water";
        if (value.contains("battery")) return "battery";
        if (value.contains("power") || value.contains("energy")) return "power";
        if (value.contains("paper") || value.contains("collection")) return "paper";
        if (value.contains("briquette") || value.contains("brick") || value.contains("mold")) return "briquette";
        if (value.contains("play") || value.contains("start")) return "play";
        if (value.contains("stop")) return "stop";
        if (value.contains("pause")) return "pause";
        if (value.contains("check") || value.contains("completed") || value.contains("locked") || value.contains("safety") || value.contains("ok")) return "check";
        if (value.contains("download")) return "export";
        return "leaf";
    }
}
