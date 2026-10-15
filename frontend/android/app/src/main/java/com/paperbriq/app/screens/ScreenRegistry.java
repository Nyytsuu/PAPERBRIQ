package com.paperbriq.app.screens;

public final class ScreenRegistry {
    private static final AppScreen[] NAV_SCREENS = {
        new DashboardScreen(),
        new IotMonitoringScreen(),
        new ProcessTrackingScreen(),
        new ReportsScreen(),
        new HistoryScreen()
    };

    private ScreenRegistry() {
    }

    public static AppScreen[] navScreens() {
        return NAV_SCREENS;
    }

    public static AppScreen get(String key) {
        for (AppScreen screen : NAV_SCREENS) {
            if (screen.key().equals(key)) {
                return screen;
            }
        }
        return NAV_SCREENS[0];
    }
}
