package com.paperbriq.app;

import android.app.Activity;
import android.os.Bundle;
import android.view.Window;

public class MainActivity extends Activity {
    private AppController controller;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Window window = getWindow();
        window.setStatusBarColor(AppColors.PAPER);
        window.setNavigationBarColor(AppColors.PAPER);
        controller = new AppController(this);
        controller.start();
    }

    @Override
    protected void onDestroy() {
        controller.destroy();
        super.onDestroy();
    }
}
