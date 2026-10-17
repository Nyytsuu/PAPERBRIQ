package com.paperbriq.app.ui;

import android.graphics.Color;
import android.graphics.Typeface;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import com.paperbriq.app.AppColors;
import com.paperbriq.app.AppController;
import com.paperbriq.app.R;

public class LoginScreen {
    private boolean passwordVisible = false;

    public View create(AppController controller) {
        Ui ui = controller.ui();
        ScrollView scroll = new ScrollView(controller.activity());
        scroll.setFillViewport(true);
        scroll.setBackgroundColor(AppColors.PAPER);

        LinearLayout root = ui.column();
        root.setBackgroundColor(AppColors.PAPER);
        scroll.addView(root, ui.matchWrap());

        root.addView(hero(controller));
        root.addView(loginCard(controller));
        return scroll;
    }

    private View hero(AppController controller) {
        Ui ui = controller.ui();
        FrameLayout hero = new FrameLayout(controller.activity());
        hero.setLayoutParams(new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, ui.dp(284)));

        ImageView image = new ImageView(controller.activity());
        image.setImageResource(R.drawable.paperbriq_illustration);
        image.setScaleType(ImageView.ScaleType.CENTER_CROP);
        hero.addView(image, new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));

        View wash = new View(controller.activity());
        wash.setBackgroundColor(Color.argb(200, 255, 255, 255));
        hero.addView(wash, new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));

        LinearLayout lockup = ui.row();
        lockup.setGravity(Gravity.CENTER_VERTICAL);
        lockup.setPadding(ui.dp(22), ui.dp(28), ui.dp(22), 0);
        lockup.addView(ui.brandMark(58, 19), new LinearLayout.LayoutParams(ui.dp(58), ui.dp(58)));

        LinearLayout copy = ui.column();
        copy.setPadding(ui.dp(14), 0, 0, 0);
        copy.addView(ui.text("PAPERBRIQ", 38, AppColors.GREEN_DARK, Typeface.BOLD));
        copy.addView(ui.text("LIVE MONITORING SYSTEM", 13, AppColors.GREEN, Typeface.BOLD));
        lockup.addView(copy, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        hero.addView(lockup, new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));
        return hero;
    }

    private View loginCard(AppController controller) {
        Ui ui = controller.ui();
        LinearLayout card = ui.card(22);
        card.setPadding(ui.dp(20), ui.dp(20), ui.dp(20), ui.dp(18));
        card.setLayoutParams(ui.fullWidth(LinearLayout.LayoutParams.WRAP_CONTENT, ui.dp(14), ui.dp(-58), ui.dp(14), ui.dp(24)));

        LinearLayout heading = ui.row();
        heading.setGravity(Gravity.CENTER_VERTICAL);
        heading.addView(ui.brandMark(42, 13), new LinearLayout.LayoutParams(ui.dp(42), ui.dp(42)));
        LinearLayout headingText = ui.column();
        headingText.setPadding(ui.dp(12), 0, 0, 0);
        headingText.addView(ui.text("Monitor PaperBriq", 23, AppColors.INK, Typeface.BOLD));
        headingText.addView(ui.text("Sign in for live machine and production data", 14, AppColors.MUTED, Typeface.NORMAL));
        heading.addView(headingText, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        card.addView(heading);

        EditText emailInput = ui.editText("Enter your email address", false);
        EditText passwordInput = ui.editText("Enter your password", true);
        CheckBox keepLoggedIn = new CheckBox(controller.activity());
        keepLoggedIn.setText("Keep me logged in");
        keepLoggedIn.setTextColor(AppColors.MUTED);
        keepLoggedIn.setTextSize(14);
        keepLoggedIn.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        keepLoggedIn.setButtonTintList(android.content.res.ColorStateList.valueOf(AppColors.GREEN));

        card.addView(field(ui, "Email Address", emailInput));
        card.addView(passwordField(ui, passwordInput));

        LinearLayout options = ui.row();
        options.setGravity(Gravity.CENTER_VERTICAL);
        options.setPadding(0, ui.dp(18), 0, ui.dp(18));
        options.addView(keepLoggedIn, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        TextView forgotPassword = ui.text("Forgot Password?", 14, AppColors.GREEN, Typeface.BOLD);
        forgotPassword.setGravity(Gravity.RIGHT);
        forgotPassword.setClickable(true);
        forgotPassword.setOnClickListener(v -> controller.showForgotPasswordDialog(emailInput.getText().toString(), "contributor"));
        options.addView(forgotPassword, new LinearLayout.LayoutParams(ui.dp(132), LinearLayout.LayoutParams.WRAP_CONTENT));
        card.addView(options);

        TextView messageView = ui.text("", 13, AppColors.RED, Typeface.BOLD);
        messageView.setGravity(Gravity.CENTER);

        Button loginButton = ui.greenButton("Log In");
        loginButton.setOnClickListener(v -> controller.login(
            emailInput.getText().toString(),
            passwordInput.getText().toString(),
            "",
            keepLoggedIn.isChecked(),
            loginButton,
            messageView
        ));
        card.addView(loginButton, ui.fullWidth(ui.dp(58), 0, 0, 0, 0));

        messageView.setPadding(0, ui.dp(12), 0, 0);
        card.addView(messageView);
        return card;
    }

    private View field(Ui ui, String label, EditText input) {
        LinearLayout group = ui.column();
        group.setPadding(0, ui.dp(16), 0, 0);
        group.addView(ui.text(label, 14, AppColors.INK, Typeface.BOLD));
        group.addView(input, ui.fullWidth(ui.dp(58), 0, ui.dp(8), 0, 0));
        return group;
    }

    private View passwordField(Ui ui, EditText passwordInput) {
        LinearLayout group = ui.column();
        group.setPadding(0, ui.dp(16), 0, 0);
        group.addView(ui.text("Password", 14, AppColors.INK, Typeface.BOLD));

        LinearLayout shell = ui.row();
        shell.setGravity(Gravity.CENTER_VERTICAL);
        shell.setPadding(ui.dp(12), 0, ui.dp(8), 0);
        shell.setBackground(ui.round(Color.WHITE, ui.dp(14), AppColors.LINE, 1));

        passwordInput.setBackgroundColor(Color.TRANSPARENT);
        shell.addView(passwordInput, new LinearLayout.LayoutParams(0, ui.dp(58), 1));

        Button toggle = new Button(shell.getContext());
        toggle.setText("Show");
        toggle.setAllCaps(false);
        toggle.setTextColor(AppColors.MUTED);
        toggle.setBackgroundColor(Color.TRANSPARENT);
        toggle.setOnClickListener(v -> {
            passwordVisible = !passwordVisible;
            passwordInput.setInputType(InputType.TYPE_CLASS_TEXT | (passwordVisible ? InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD : InputType.TYPE_TEXT_VARIATION_PASSWORD));
            toggle.setText(passwordVisible ? "Hide" : "Show");
            passwordInput.setSelection(passwordInput.getText().length());
        });
        shell.addView(toggle, new LinearLayout.LayoutParams(ui.dp(78), ui.dp(50)));
        group.addView(shell, ui.fullWidth(ui.dp(58), 0, ui.dp(8), 0, 0));
        return group;
    }

}
