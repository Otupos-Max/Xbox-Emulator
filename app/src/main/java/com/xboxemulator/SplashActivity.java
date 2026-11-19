package com.xboxemulator;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.VideoView;

/**
 * Tela de abertura: toca o vídeo de splash ("video splash.mp4", empacotado
 * como asset) e, quando ele termina (ou falha), segue para a Activity do
 * emulador.
 */
public class SplashActivity extends Activity {

    private boolean navigated = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        Window window = getWindow();
        window.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN
                | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        hideSystemBars();

        FrameLayout root = new FrameLayout(this);
        VideoView videoView = new VideoView(this);
        FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT);
        root.addView(videoView, params);
        setContentView(root);

        // O asset é servido pelo próprio APK ("video splash.mp4" fica em
        // Interface/Splash, incluído como diretório de assets no build.gradle).
        Uri videoUri = Uri.parse("file:///android_asset/video splash.mp4");
        videoView.setVideoURI(videoUri);

        videoView.setOnCompletionListener(mp -> goToEmulator());
        videoView.setOnErrorListener((mp, what, extra) -> {
            goToEmulator();
            return true;
        });

        videoView.start();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            hideSystemBars();
        }
    }

    private void goToEmulator() {
        if (navigated) {
            return;
        }
        navigated = true;
        startActivity(new Intent(this, XenonActivity.class));
        finish();
    }

    @SuppressWarnings("deprecation")
    private void hideSystemBars() {
        Window window = getWindow();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            window.setDecorFitsSystemWindows(false);
            WindowInsetsController controller = window.getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                controller.setSystemBarsBehavior(
                        WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            View decor = window.getDecorView();
            decor.setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_FULLSCREEN);
        }
    }
}
