package com.paperbriq.app.ui;

import android.app.AlertDialog;
import android.animation.ValueAnimator;
import android.graphics.Color;
import android.graphics.Typeface;
import android.view.Gravity;
import android.view.View;
import android.view.accessibility.AccessibilityNodeInfo;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import com.paperbriq.app.AppColors;
import com.paperbriq.app.AppController;
import com.paperbriq.app.screens.AppScreen;
import com.paperbriq.app.screens.ScreenRegistry;

public class AppShell {
    public static final int CONTENT_SCROLL_ID = 0x50425251;

    public View create(AppController controller, String pageKey) {
        Ui ui = controller.ui();
        LinearLayout frame = ui.column();
        frame.setBackgroundColor(AppColors.PAPER);
        frame.setFitsSystemWindows(true);
        frame.addView(header(controller, ScreenRegistry.get(pageKey)), ui.matchWrap());
        ScrollView scroll = new ScrollView(controller.activity());
        scroll.setId(CONTENT_SCROLL_ID);
        scroll.setFillViewport(true);
        View content = createContent(controller, pageKey);
        scroll.addView(content, ui.matchWrap());
        if (ValueAnimator.areAnimatorsEnabled()) {
            content.setAlpha(0f);
            content.setTranslationY(ui.dp(6));
            content.animate().alpha(1f).translationY(0f).setDuration(180).start();
        }
        frame.addView(scroll, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1));
        frame.addView(navigation(controller, pageKey), ui.matchWrap());
        return frame;
    }

    public View createContent(AppController controller, String pageKey) {
        Ui ui = controller.ui();
        LinearLayout content = ui.column();
        content.setPadding(ui.dp(12), ui.dp(4), ui.dp(12), ui.dp(12));
        ScreenRegistry.get(pageKey).render(content, controller);
        return content;
    }

    private View header(AppController controller, AppScreen screen) {
        Ui ui = controller.ui();
        LinearLayout header = ui.row();
        header.setGravity(Gravity.CENTER_VERTICAL);
        header.setPadding(ui.dp(12), ui.dp(8), ui.dp(12), ui.dp(8));
        header.addView(ui.brandMark(40, 12), new LinearLayout.LayoutParams(ui.dp(40), ui.dp(40)));
        LinearLayout copy = ui.column();
        copy.setPadding(ui.dp(8), 0, ui.dp(8), 0);
        copy.addView(ui.text("PAPERBRIQ", 12, AppColors.GREEN, Typeface.BOLD));
        copy.addView(ui.text(screen.title(), 22, AppColors.INK, Typeface.BOLD));
        header.addView(copy, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        LinearLayout menu = ui.column();
        menu.setGravity(Gravity.CENTER);
        menu.setBackground(ui.round(Color.WHITE, ui.dp(16), AppColors.LINE, 1));
        menu.addView(new PaperIconView(controller.activity(), "menu", AppColors.GREEN_DARK),
            new LinearLayout.LayoutParams(ui.dp(24), ui.dp(22)));
        TextView menuLabel = ui.text("Menu", 10, AppColors.GREEN_DARK, Typeface.BOLD);
        menuLabel.setGravity(Gravity.CENTER);
        menu.addView(menuLabel);
        ui.interactive(menu, "Menu: account and machine access", () -> showMenu(controller), 16);
        header.addView(menu, new LinearLayout.LayoutParams(ui.dp(52), ui.dp(48)));
        return header;
    }

    private View navigation(AppController controller, String pageKey) {
        Ui ui = controller.ui();
        FrameLayout dock = new FrameLayout(controller.activity());
        dock.setPadding(ui.dp(12), ui.dp(8), ui.dp(12), ui.dp(10));
        dock.setClipChildren(false);
        dock.setClipToPadding(false);
        LinearLayout bar = ui.row();
        bar.setPadding(ui.dp(5), ui.dp(6), ui.dp(5), ui.dp(6));
        bar.setBackground(ui.round(Color.WHITE, ui.dp(26), AppColors.LINE, 1));
        bar.setElevation(ui.dp(6));
        for (AppScreen screen : ScreenRegistry.navScreens()) {
            boolean selected = screen.key().equals(pageKey);
            LinearLayout tab = ui.column();
            tab.setGravity(Gravity.CENTER);
            tab.setMinimumHeight(ui.dp(58));
            tab.setPadding(0, ui.dp(3), 0, ui.dp(3));
            tab.setSelected(selected);
            FrameLayout iconPill = new FrameLayout(controller.activity());
            iconPill.setBackground(ui.round(selected ? AppColors.GREEN_DARK : Color.TRANSPARENT,
                ui.dp(15), Color.TRANSPARENT, 0));
            iconPill.addView(new PaperIconView(controller.activity(), screen.key(), selected ? Color.WHITE : AppColors.MUTED),
                new FrameLayout.LayoutParams(ui.dp(25), ui.dp(25), Gravity.CENTER));
            tab.addView(iconPill, new LinearLayout.LayoutParams(ui.dp(44), ui.dp(32)));
            TextView label = ui.text(screen.navLabel(), 11, selected ? AppColors.GREEN_DARK : AppColors.MUTED, Typeface.BOLD);
            label.setGravity(Gravity.CENTER);
            tab.addView(label, ui.fullWidth(LinearLayout.LayoutParams.WRAP_CONTENT, 0, ui.dp(3), 0, 0));
            iconPill.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS);
            label.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
            tab.setAccessibilityDelegate(new View.AccessibilityDelegate() {
                @Override public void onInitializeAccessibilityNodeInfo(View host, AccessibilityNodeInfo info) {
                    super.onInitializeAccessibilityNodeInfo(host, info);
                    info.setClassName("android.widget.Button");
                    info.setSelected(selected);
                }
            });
            ui.interactive(tab, screen.navLabel() + (selected ? ", selected" : ""), () -> {
                if (!screen.key().equals(controller.session().currentPage)) controller.showPage(screen.key());
            }, 18);
            if (selected && ValueAnimator.areAnimatorsEnabled()) {
                iconPill.setScaleX(0.85f);
                iconPill.setScaleY(0.85f);
                iconPill.animate().scaleX(1f).scaleY(1f).setDuration(180).start();
            }
            LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1);
            bar.addView(tab, params);
        }
        dock.addView(bar, new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.WRAP_CONTENT));
        return dock;
    }

    private void showMenu(AppController controller) {
        boolean controls = controller.canControlMachine();
        String[] choices = controls ? new String[]{"Machine access", "Log out"} : new String[]{"Log out"};
        new AlertDialog.Builder(controller.activity())
            .setTitle("PaperBriq")
            .setItems(choices, (dialog, which) -> {
                if (controls && which == 0) {
                    controller.showControlKeyDialog(null);
                } else {
                    controller.confirmAction("Log out?", "You will need to sign in again.", controller::logout);
                }
            })
            .setNegativeButton("Close", null)
            .show();
    }
}
