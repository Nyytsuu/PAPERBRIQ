package com.paperbriq.app.screens;

public final class ScreenRegistry {
    private static final AppScreen[] NAV_SCREENS = {
        new DashboardScreen(),
        new ProcessTrackingScreen(),
        new IotMonitoringScreen(),
        new ContributionsScreen(),
        new RewardsScreen(),
        new ReportsScreen(),
        new HistoryScreen(),
        new SettingsScreen(),
        new ScanQrScreen()
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
