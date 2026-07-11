package com.paperbriq.app.ui;

import android.graphics.Typeface;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import com.paperbriq.app.AppColors;
import com.paperbriq.app.AppController;
import com.paperbriq.app.screens.AppScreen;
import com.paperbriq.app.screens.ScreenRegistry;

public class AppShell {
    public View create(AppController controller, String pageKey) {
        Ui ui = controller.ui();
        AppScreen screen = ScreenRegistry.get(pageKey);

        ScrollView scroll = new ScrollView(controller.activity());
        scroll.setFillViewport(true);
        scroll.setBackgroundColor(AppColors.PAPER);

        LinearLayout root = ui.column();
        root.setPadding(ui.dp(16), ui.dp(20), ui.dp(16), ui.dp(20));
        root.setBackgroundColor(AppColors.PAPER);
        scroll.addView(root, ui.matchWrap());

        root.addView(header(controller, screen));
        root.addView(nav(controller));
        screen.render(root, controller);

        TextView footer = ui.text("2026 PaperBriq. All rights reserved.", 12, android.graphics.Color.rgb(142, 151, 166), Typeface.NORMAL);
        footer.setGravity(Gravity.CENTER);
        footer.setPadding(0, ui.dp(18), 0, 0);
        root.addView(footer);
        return scroll;
    }

    private View header(AppController controller, AppScreen screen) {
        Ui ui = controller.ui();
        LinearLayout header = ui.column();
        header.setPadding(0, 0, 0, ui.dp(14));

        LinearLayout top = ui.row();
        top.setGravity(Gravity.CENTER_VERTICAL);
        top.addView(ui.brandMark(44, 12), new LinearLayout.LayoutParams(ui.dp(44), ui.dp(44)));

        LinearLayout copy = ui.column();
        copy.setPadding(ui.dp(12), 0, 0, 0);
        copy.addView(ui.text("PAPERBRIQ", 20, AppColors.GREEN_DARK, Typeface.BOLD));
        copy.addView(ui.text("Paper waste briquette operations", 12, AppColors.MUTED, Typeface.NORMAL));
        top.addView(copy, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));

        Button scan = ui.outlineButton("Scan QR");
        scan.setOnClickListener(v -> controller.showPage("scan"));
        top.addView(scan, new LinearLayout.LayoutParams(ui.dp(112), ui.dp(46)));
        header.addView(top);

        TextView titleView = ui.text(screen.title(), 26, AppColors.INK, Typeface.BOLD);
        titleView.setPadding(0, ui.dp(18), 0, ui.dp(4));
        header.addView(titleView);
        header.addView(ui.text(screen.subtitle(), 14, AppColors.MUTED, Typeface.NORMAL));
        return header;
    }

    private View nav(AppController controller) {
        Ui ui = controller.ui();
        HorizontalScrollView scroll = new HorizontalScrollView(controller.activity());
        scroll.setHorizontalScrollBarEnabled(false);

        LinearLayout row = ui.row();
        row.setPadding(0, ui.dp(4), 0, ui.dp(14));
        for (AppScreen screen : ScreenRegistry.navScreens()) {
            row.addView(navChip(controller, screen));
        }
        scroll.addView(row);
        return scroll;
    }

    private View navChip(AppController controller, AppScreen screen) {
        Ui ui = controller.ui();
        Button button = new Button(controller.activity());
        boolean active = screen.key().equals(controller.session().currentPage);
        button.setText(screen.navLabel());
        button.setAllCaps(false);
        button.setTextSize(13);
        button.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        button.setTextColor(active ? AppColors.GREEN_DARK : AppColors.MUTED);
        button.setBackground(ui.round(active ? AppColors.GREEN_SOFT : android.graphics.Color.WHITE, ui.dp(18), active ? AppColors.GREEN : AppColors.LINE, 1));
        button.setOnClickListener(v -> controller.showPage(screen.key()));

        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, ui.dp(42));
        params.setMargins(0, 0, ui.dp(8), 0);
        button.setLayoutParams(params);
        return button;
    }
}
