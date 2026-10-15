package com.paperbriq.app.ui;

import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.PopupWindow;
import android.widget.ScrollView;
import android.widget.TextView;

import com.paperbriq.app.AppColors;
import com.paperbriq.app.AppController;
import com.paperbriq.app.screens.AppScreen;
import com.paperbriq.app.screens.ScreenRegistry;

import org.json.JSONObject;

public class AppShell {
    public static final int CONTENT_SCROLL_ID = 0x50425251;

    public View create(AppController controller, String pageKey) {
        Ui ui = controller.ui();
        AppScreen screen = ScreenRegistry.get(pageKey);

        ScrollView scroll = new ScrollView(controller.activity());
        scroll.setId(CONTENT_SCROLL_ID);
        scroll.setFillViewport(true);
        scroll.setBackgroundColor(AppColors.PAPER);

        LinearLayout root = ui.column();
        root.setPadding(ui.dp(16), ui.dp(18), ui.dp(16), ui.dp(24));
        root.setBackgroundColor(AppColors.PAPER);
        scroll.addView(root, ui.matchWrap());

        root.addView(header(controller, screen));
        screen.render(root, controller);

        TextView footer = ui.text("PaperBriq Monitoring • Live data every 5 seconds", 12, android.graphics.Color.rgb(112, 132, 119), Typeface.BOLD);
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
        View brandButton = ui.brandMark(48, 14);
        brandButton.setClickable(true);
        brandButton.setFocusable(true);
        brandButton.setContentDescription("Open monitoring navigation");
        brandButton.setOnClickListener(v -> showSidebar(controller));
        top.addView(brandButton, new LinearLayout.LayoutParams(ui.dp(48), ui.dp(48)));

        LinearLayout copy = ui.column();
        copy.setPadding(ui.dp(12), 0, 0, 0);
        copy.addView(ui.text("PAPERBRIQ", 20, AppColors.GREEN_DARK, Typeface.BOLD));
        copy.addView(ui.text("MONITORING SYSTEM", 11, AppColors.GREEN, Typeface.BOLD));
        top.addView(copy, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));

        TextView connection = ui.pill("●  LIVE", AppColors.GREEN);
        connection.setGravity(Gravity.CENTER);
        top.addView(connection, new LinearLayout.LayoutParams(ui.dp(88), ui.dp(40)));
        header.addView(top);

        LinearLayout titleRow = ui.row();
        titleRow.setGravity(Gravity.CENTER_VERTICAL);
        titleRow.setPadding(0, ui.dp(18), 0, 0);

        TextView titleView = ui.text(screen.title(), 26, AppColors.INK, Typeface.BOLD);
        titleRow.addView(titleView, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));

        header.addView(titleRow);

        TextView subtitle = ui.text(screen.subtitle(), 13, AppColors.MUTED, Typeface.NORMAL);
        subtitle.setPadding(0, ui.dp(4), 0, 0);
        header.addView(subtitle);

        JSONObject payload = controller.session().pagePayload(controller.session().currentPage);
        String liveText = payload.optString("displayTime", "");
        LinearLayout liveRow = ui.row();
        liveRow.setGravity(Gravity.CENTER_VERTICAL);
        liveRow.setPadding(0, ui.dp(8), 0, 0);
        liveRow.addView(ui.pill("Read only", AppColors.BLUE));
        TextView syncText = ui.text(
            liveText.isEmpty() ? "Refreshes every 5 seconds while open" : "Last sync " + liveText,
            12,
            AppColors.MUTED,
            Typeface.BOLD
        );
        syncText.setPadding(ui.dp(8), 0, 0, 0);
        liveRow.addView(syncText, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        header.addView(liveRow);
        return header;
    }

    private void showSidebar(AppController controller) {
        Ui ui = controller.ui();
        ScrollView drawerScroll = new ScrollView(controller.activity());
        drawerScroll.setFillViewport(false);
        drawerScroll.setVerticalScrollBarEnabled(true);
        drawerScroll.setBackground(ui.round(AppColors.PAPER, 0, Color.TRANSPARENT, 0));

        LinearLayout panel = ui.column();
        panel.setPadding(ui.dp(18), ui.dp(24), ui.dp(18), ui.dp(18));
        panel.setBackground(ui.round(AppColors.PAPER, 0, Color.TRANSPARENT, 0));
        drawerScroll.addView(
            panel,
            new ScrollView.LayoutParams(ScrollView.LayoutParams.MATCH_PARENT, ScrollView.LayoutParams.WRAP_CONTENT)
        );

        PopupWindow popup = new PopupWindow(
            drawerScroll,
            ui.dp(304),
            LinearLayout.LayoutParams.MATCH_PARENT,
            true
        );
        popup.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
        popup.setOutsideTouchable(true);
        popup.setElevation(ui.dp(14));

        LinearLayout brand = ui.row();
        brand.setGravity(Gravity.CENTER_VERTICAL);
        brand.addView(ui.brandMark(46, 12), new LinearLayout.LayoutParams(ui.dp(46), ui.dp(46)));

        LinearLayout copy = ui.column();
        copy.setPadding(ui.dp(12), 0, 0, 0);
        copy.addView(ui.text("PAPERBRIQ", 20, AppColors.GREEN_DARK, Typeface.BOLD));
        copy.addView(ui.text("MONITORING CENTER", 11, AppColors.GREEN, Typeface.BOLD));
        brand.addView(copy, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        panel.addView(brand, ui.fullWidth(LinearLayout.LayoutParams.WRAP_CONTENT, 0, 0, 0, ui.dp(18)));

        JSONObject dashboard = controller.session().pagePayload("dashboard");
        JSONObject systemStatus = dashboard.optJSONObject("systemStatus");
        if (systemStatus == null) systemStatus = new JSONObject();

        LinearLayout live = ui.card(20);
        live.setPadding(ui.dp(16), ui.dp(16), ui.dp(16), ui.dp(16));
        live.setBackground(ui.round(AppColors.GREEN_DARK, ui.dp(18), Color.TRANSPARENT, 0));
        LinearLayout liveTop = ui.row();
        liveTop.setGravity(Gravity.CENTER_VERTICAL);
        liveTop.addView(ui.iconBadge("iot", AppColors.XANTHOPHYLLS, 38), new LinearLayout.LayoutParams(ui.dp(38), ui.dp(38)));
        LinearLayout liveCopy = ui.column();
        liveCopy.setPadding(ui.dp(10), 0, 0, 0);
        liveCopy.addView(ui.text(systemStatus.optString("status", "Connecting to monitor"), 16, Color.WHITE, Typeface.BOLD));
        liveCopy.addView(ui.text(systemStatus.optString("message", "Live machine status and sensor health."), 11, Color.argb(220, 255, 255, 255), Typeface.NORMAL));
        liveTop.addView(liveCopy, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        live.addView(liveTop);
        live.setOnClickListener(v -> {
            popup.dismiss();
            controller.showPage("iot");
        });
        panel.addView(live, ui.fullWidth(LinearLayout.LayoutParams.WRAP_CONTENT, 0, 0, 0, ui.dp(14)));

        for (AppScreen screen : ScreenRegistry.navScreens()) {
            panel.addView(sidebarItem(controller, screen, popup));
        }

        Button close = ui.outlineButton("Close navigation");
        close.setOnClickListener(v -> popup.dismiss());
        panel.addView(close, ui.fullWidth(ui.dp(48), 0, ui.dp(12), 0, 0));

        Button logout = ui.outlineButton("Log Out");
        logout.setOnClickListener(v -> {
            popup.dismiss();
            controller.confirmAction(
                "Log out?",
                "This will clear the saved login on this phone.",
                controller::logout
            );
        });
        panel.addView(logout, ui.fullWidth(ui.dp(48), 0, ui.dp(10), 0, 0));

        popup.showAtLocation(controller.activity().getWindow().getDecorView(), Gravity.START | Gravity.TOP, 0, 0);
    }

    private View sidebarItem(AppController controller, AppScreen screen, PopupWindow popup) {
        Ui ui = controller.ui();
        boolean active = screen.key().equals(controller.session().currentPage);

        LinearLayout item = ui.row();
        item.setGravity(Gravity.CENTER_VERTICAL);
        item.setPadding(ui.dp(12), ui.dp(10), ui.dp(12), ui.dp(10));
        item.setClickable(true);
        item.setFocusable(true);
        item.setBackground(ui.round(active ? AppColors.GREEN_SOFT : Color.WHITE, ui.dp(16), active ? AppColors.GREEN : AppColors.LINE, 1));
        item.setOnClickListener(v -> {
            popup.dismiss();
            controller.showPage(screen.key());
        });

        int iconColor = active ? Color.WHITE : AppColors.GREEN;
        int iconBackground = active ? AppColors.GREEN : AppColors.GREEN_SOFT;
        View icon = ui.iconBadge(ui.iconNameFor(screen.key() + " " + screen.title()), iconColor, 42);
        icon.setBackground(ui.round(iconBackground, ui.dp(14), Color.TRANSPARENT, 0));
        item.addView(icon, new LinearLayout.LayoutParams(ui.dp(42), ui.dp(42)));

        LinearLayout copy = ui.column();
        copy.setPadding(ui.dp(12), 0, ui.dp(8), 0);
        copy.addView(ui.text(screen.navLabel(), 15, active ? AppColors.GREEN_DARK : AppColors.INK, Typeface.BOLD));
        String subtitle = screen.subtitle();
        if (subtitle.length() > 34) subtitle = subtitle.substring(0, 34).trim() + "...";
        copy.addView(ui.text(subtitle, 11, AppColors.MUTED, Typeface.BOLD));
        item.addView(copy, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));

        item.addView(ui.iconBadge(active ? "check" : "process", active ? AppColors.GREEN : AppColors.MUTED, 30), new LinearLayout.LayoutParams(ui.dp(30), ui.dp(30)));

        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        params.setMargins(0, 0, 0, ui.dp(8));
        item.setLayoutParams(params);
        return item;
    }
}
