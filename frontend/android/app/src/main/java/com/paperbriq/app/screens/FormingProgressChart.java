package com.paperbriq.app.screens;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.view.Gravity;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

import com.paperbriq.app.AppColors;
import com.paperbriq.app.ui.Ui;

import org.json.JSONArray;
import org.json.JSONObject;

final class FormingProgressChart {
    private static final int TRACK_COLOR = 0xFFE3EBE5;

    private FormingProgressChart() {}

    static View createCircle(Ui ui, JSONObject run) {
        JSONArray milestones = run == null ? null : run.optJSONArray("milestones");
        int total = milestones == null ? 0 : milestones.length();
        int completed = countCompleted(milestones);
        float fraction = total == 0 ? 0 : (float) completed / total;
        TextView value = ui.text(total == 0 ? "--" : Math.round(fraction * 100) + "%",
            23, AppColors.GREEN_DARK, Typeface.BOLD);
        value.setGravity(Gravity.CENTER);
        FrameLayout chart = new FrameLayout(value.getContext());
        chart.addView(new CircleTrack(value.getContext(), ui.dp(8), fraction),
            new FrameLayout.LayoutParams(-1, -1));
        chart.addView(value, new FrameLayout.LayoutParams(-1, -1));
        chart.setContentDescription(total == 0 ? "Waiting for progress reports"
            : completed + " of " + total + " forming milestones complete");
        return chart;
    }

    static View create(Ui ui, JSONObject run) {
        LinearLayout chart = ui.column();
        JSONArray milestones = run == null ? null : run.optJSONArray("milestones");
        if (milestones == null || milestones.length() == 0) {
            chart.addView(ui.text("Waiting for progress reports", 12, AppColors.MUTED, Typeface.NORMAL));
            return chart;
        }

        LinearLayout bars = ui.row();
        bars.setGravity(Gravity.BOTTOM);
        for (int index = 0; index < milestones.length(); index++) {
            JSONObject milestone = milestones.optJSONObject(index);
            boolean finished = milestone != null && milestone.optBoolean("completed");
            int color = finished ? AppColors.GREEN_DARK : TRACK_COLOR;
            View bar = new View(bars.getContext());
            bar.setBackground(ui.round(color, ui.dp(4), Color.TRANSPARENT, 0));
            LinearLayout.LayoutParams size = new LinearLayout.LayoutParams(0, ui.dp(12 + index * 5), 1);
            if (index > 0) size.leftMargin = ui.dp(4);
            bars.addView(bar, size);
        }
        chart.addView(bars, ui.matchWrap());
        String summary = countCompleted(milestones) + " of " + milestones.length() + " forming milestones complete";
        chart.addView(ui.text(summary, 12, AppColors.MUTED, Typeface.NORMAL),
            ui.fullWidth(-2, 0, ui.dp(5), 0, 0));
        return chart;
    }

    private static int countCompleted(JSONArray milestones) {
        if (milestones == null) return 0;
        int completed = 0;
        for (int index = 0; index < milestones.length(); index++) {
            JSONObject milestone = milestones.optJSONObject(index);
            if (milestone != null && milestone.optBoolean("completed")) completed++;
        }
        return completed;
    }

    private static final class CircleTrack extends View {
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final RectF bounds = new RectF();
        private final float fraction;

        CircleTrack(Context context, float strokeWidth, float fraction) {
            super(context);
            this.fraction = fraction;
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(strokeWidth);
            paint.setStrokeCap(Paint.Cap.ROUND);
        }

        @Override
        protected void onDraw(Canvas canvas) {
            float centerX = getWidth() / 2f;
            float centerY = getHeight() / 2f;
            float radius = Math.min(centerX, centerY) - paint.getStrokeWidth() / 2f;
            if (radius <= 0) return;
            paint.setColor(TRACK_COLOR);
            canvas.drawCircle(centerX, centerY, radius, paint);
            if (fraction <= 0) return;
            paint.setColor(AppColors.GREEN_DARK);
            bounds.set(centerX - radius, centerY - radius, centerX + radius, centerY + radius);
            canvas.drawArc(bounds, -90f, fraction * 360f, false, paint);
        }
    }
}
