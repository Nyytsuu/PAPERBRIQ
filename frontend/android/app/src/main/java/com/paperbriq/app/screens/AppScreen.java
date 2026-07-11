package com.paperbriq.app.screens;

import android.widget.LinearLayout;

import com.paperbriq.app.AppController;

public interface AppScreen {
    String key();
    String navLabel();
    String title();
    String subtitle();
    void render(LinearLayout root, AppController controller);
}
