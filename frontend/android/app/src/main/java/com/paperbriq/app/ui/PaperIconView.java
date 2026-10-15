package com.paperbriq.app.ui;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.view.View;

import java.util.Locale;

public class PaperIconView extends View {
    private final String name;
    private final int color;
    private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);

    public PaperIconView(Context context, String name, int color) {
        super(context);
        this.name = String.valueOf(name == null ? "leaf" : name).toLowerCase(Locale.ROOT);
        this.color = color;
        setWillNotDraw(false);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        float size = Math.min(getWidth(), getHeight());
        if (size <= 0) return;

        canvas.save();
        canvas.translate((getWidth() - size) / 2f, (getHeight() - size) / 2f);
        canvas.scale(size / 24f, size / 24f);

        stroke.setColor(color);
        stroke.setStyle(Paint.Style.STROKE);
        stroke.setStrokeWidth(1.9f);
        stroke.setStrokeCap(Paint.Cap.ROUND);
        stroke.setStrokeJoin(Paint.Join.ROUND);

        fill.setColor(color);
        fill.setStyle(Paint.Style.FILL);

        drawIcon(canvas);
        canvas.restore();
    }

    private void drawIcon(Canvas canvas) {
        if (has("dashboard", "home", "overview")) {
            line(canvas, 4, 11, 12, 5);
            line(canvas, 12, 5, 20, 11);
            line(canvas, 6.5f, 10.5f, 6.5f, 19);
            line(canvas, 17.5f, 10.5f, 17.5f, 19);
            line(canvas, 6.5f, 19, 17.5f, 19);
            rect(canvas, 10, 14, 14, 19);
            return;
        }
        if (has("process", "chart", "progress")) {
            line(canvas, 5, 19, 5, 5);
            line(canvas, 5, 19, 20, 19);
            Path path = new Path();
            path.moveTo(7, 16);
            path.lineTo(10, 12);
            path.lineTo(13, 14);
            path.lineTo(17, 8);
            path.lineTo(20, 10);
            canvas.drawPath(path, stroke);
            return;
        }
        if (has("iot", "wifi", "sensor", "packet", "sync", "network")) {
            arc(canvas, 4, 7, 20, 21, 215, 110);
            arc(canvas, 7, 10, 17, 20, 220, 100);
            arc(canvas, 10, 13, 14, 19, 225, 90);
            canvas.drawCircle(12, 20, 1.1f, fill);
            return;
        }
        if (has("staff", "user", "contributors", "people", "active")) {
            canvas.drawCircle(9, 8, 2.8f, stroke);
            arc(canvas, 4, 13, 14, 23, 205, 130);
            canvas.drawCircle(16, 9, 2.3f, stroke);
            arc(canvas, 13, 14, 21, 22, 215, 110);
            return;
        }
        if (has("reward", "rewards", "gift", "points", "available", "redeemed")) {
            rect(canvas, 5, 9, 19, 20);
            rect(canvas, 4, 6, 20, 10);
            line(canvas, 12, 6, 12, 20);
            arc(canvas, 6.5f, 3.5f, 12, 8.5f, 10, 230);
            arc(canvas, 12, 3.5f, 17.5f, 8.5f, -60, 230);
            return;
        }
        if (has("report", "reports", "data")) {
            rect(canvas, 6, 4, 18, 20);
            line(canvas, 9, 16, 9, 12);
            line(canvas, 12, 16, 12, 9);
            line(canvas, 15, 16, 15, 11);
            line(canvas, 8, 7, 14, 7);
            return;
        }
        if (has("history", "time", "clock", "runtime", "session")) {
            canvas.drawCircle(12, 12, 7.5f, stroke);
            line(canvas, 12, 8, 12, 12.5f);
            line(canvas, 12, 12.5f, 15.5f, 14.5f);
            return;
        }
        if (has("settings", "gear", "rules")) {
            canvas.drawCircle(12, 12, 3.5f, stroke);
            for (int index = 0; index < 8; index++) {
                double angle = Math.toRadians(index * 45);
                float x1 = 12 + (float) Math.cos(angle) * 6.2f;
                float y1 = 12 + (float) Math.sin(angle) * 6.2f;
                float x2 = 12 + (float) Math.cos(angle) * 8.2f;
                float y2 = 12 + (float) Math.sin(angle) * 8.2f;
                line(canvas, x1, y1, x2, y2);
            }
            return;
        }
        if (has("scan", "qr", "code")) {
            qrBox(canvas, 5, 5);
            qrBox(canvas, 14, 5);
            qrBox(canvas, 5, 14);
            rect(canvas, 14, 14, 16, 16);
            rect(canvas, 18, 18, 20, 20);
            line(canvas, 19, 14, 19, 16);
            line(canvas, 14, 20, 16, 20);
            return;
        }
        if (has("rain", "water", "flow")) {
            Path drop = new Path();
            drop.moveTo(12, 4);
            drop.cubicTo(8, 9, 6.5f, 12, 6.5f, 15);
            drop.cubicTo(6.5f, 19, 9.2f, 21, 12, 21);
            drop.cubicTo(14.8f, 21, 17.5f, 19, 17.5f, 15);
            drop.cubicTo(17.5f, 12, 16, 9, 12, 4);
            canvas.drawPath(drop, stroke);
            return;
        }
        if (has("battery")) {
            rect(canvas, 5, 7, 18, 17);
            rect(canvas, 18, 10, 20, 14);
            canvas.drawRect(8, 10, 13.5f, 14, fill);
            return;
        }
        if (has("power", "energy", "bolt")) {
            Path bolt = new Path();
            bolt.moveTo(13, 3);
            bolt.lineTo(6.5f, 13);
            bolt.lineTo(12, 13);
            bolt.lineTo(10.5f, 21);
            bolt.lineTo(17.5f, 10);
            bolt.lineTo(12, 10);
            bolt.close();
            canvas.drawPath(bolt, fill);
            return;
        }
        if (has("paper", "collection", "collected")) {
            rect(canvas, 7, 4, 17, 20);
            line(canvas, 9, 8, 15, 8);
            line(canvas, 9, 12, 15, 12);
            line(canvas, 9, 16, 13, 16);
            return;
        }
        if (has("briquette", "brick", "produce", "mold")) {
            brick(canvas, 5, 7, 6, 3);
            brick(canvas, 12.5f, 7, 6, 3);
            brick(canvas, 8.5f, 11.5f, 6, 3);
            brick(canvas, 5, 16, 6, 3);
            brick(canvas, 12.5f, 16, 6, 3);
            return;
        }
        if (has("play", "start")) {
            Path play = new Path();
            play.moveTo(8, 5);
            play.lineTo(19, 12);
            play.lineTo(8, 19);
            play.close();
            canvas.drawPath(play, fill);
            return;
        }
        if (has("stop")) {
            canvas.drawRoundRect(new RectF(7, 7, 17, 17), 2, 2, fill);
            return;
        }
        if (has("pause")) {
            canvas.drawRoundRect(new RectF(7, 6, 10, 18), 1.5f, 1.5f, fill);
            canvas.drawRoundRect(new RectF(14, 6, 17, 18), 1.5f, 1.5f, fill);
            return;
        }
        if (has("check", "completed", "safety", "locked")) {
            canvas.drawCircle(12, 12, 8, stroke);
            line(canvas, 8, 12.5f, 11, 15.5f);
            line(canvas, 11, 15.5f, 17, 8.5f);
            return;
        }
        if (has("export", "download")) {
            line(canvas, 12, 4, 12, 15);
            line(canvas, 8, 11, 12, 15);
            line(canvas, 16, 11, 12, 15);
            line(canvas, 6, 19, 18, 19);
            return;
        }

        Path leaf = new Path();
        leaf.moveTo(5, 14);
        leaf.cubicTo(9, 4, 18, 4, 20, 5);
        leaf.cubicTo(19, 14, 12, 19, 5, 14);
        canvas.drawPath(leaf, stroke);
        line(canvas, 7, 14, 16, 8);
    }

    private boolean has(String... parts) {
        for (String part : parts) {
            if (name.contains(part)) return true;
        }
        return false;
    }

    private void line(Canvas canvas, float x1, float y1, float x2, float y2) {
        canvas.drawLine(x1, y1, x2, y2, stroke);
    }

    private void rect(Canvas canvas, float left, float top, float right, float bottom) {
        canvas.drawRoundRect(new RectF(left, top, right, bottom), 1.5f, 1.5f, stroke);
    }

    private void brick(Canvas canvas, float left, float top, float width, float height) {
        canvas.drawRoundRect(new RectF(left, top, left + width, top + height), 0.9f, 0.9f, stroke);
    }

    private void qrBox(Canvas canvas, float left, float top) {
        rect(canvas, left, top, left + 5, top + 5);
        canvas.drawRect(left + 1.7f, top + 1.7f, left + 3.3f, top + 3.3f, fill);
    }

    private void arc(Canvas canvas, float left, float top, float right, float bottom, float start, float sweep) {
        canvas.drawArc(new RectF(left, top, right, bottom), start, sweep, false, stroke);
    }
}
