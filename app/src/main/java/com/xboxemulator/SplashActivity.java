package com.xboxemulator;

import android.app.Activity;
import android.content.Intent;
import android.content.res.AssetFileDescriptor;
import android.graphics.SurfaceTexture;
import android.media.MediaPlayer;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.view.Surface;
import android.view.TextureView;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.FrameLayout;

import java.io.IOException;

/**
 * Tela de abertura: toca o vídeo de splash ("video splash.mp4", empacotado
 * como asset) e, quando ele termina (ou falha), segue para a Activity do
 * emulador.
 *
 * Usa MediaPlayer + TextureView diretamente (em vez de VideoView/Uri) porque
 * o nome do asset tem espaço, e montar uma Uri "file:///android_asset/..."
 * com espaço é frágil; AssetFileDescriptor não tem esse problema.
 */
public class SplashActivity extends Activity implements TextureView.SurfaceTextureListener {

    private static final String TAG = "SplashActivity";
    private static final String ASSET_NAME = "video splash.mp4";

    private MediaPlayer mediaPlayer;
    private boolean navigated = false;
    private boolean surfaceReady = false;
    private boolean playerPrepared = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        Window window = getWindow();
        window.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN
                | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        hideSystemBars();

        FrameLayout root = new FrameLayout(this);
        TextureView textureView = new TextureView(this);
        textureView.setSurfaceTextureListener(this);
        FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT);
        root.addView(textureView, params);
        setContentView(root);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            hideSystemBars();
        }
    }

    @Override
    public void onSurfaceTextureAvailable(SurfaceTexture surfaceTexture, int width, int height) {
        surfaceReady = true;
        preparePlayer(new Surface(surfaceTexture));
    }

    private void preparePlayer(Surface surface) {
        try {
            mediaPlayer = new MediaPlayer();
            AssetFileDescriptor afd = getAssets().openFd(ASSET_NAME);
            mediaPlayer.setDataSource(afd.getFileDescriptor(), afd.getStartOffset(), afd.getLength());
            afd.close();
            mediaPlayer.setSurface(surface);
            mediaPlayer.setOnCompletionListener(mp -> goToEmulator());
            mediaPlayer.setOnErrorListener((mp, what, extra) -> {
                Log.w(TAG, "Falha ao tocar o vídeo de splash (what=" + what + ", extra=" + extra + ")");
                goToEmulator();
                return true;
            });
            mediaPlayer.setOnPreparedListener(mp -> {
                playerPrepared = true;
                mp.start();
            });
            mediaPlayer.prepareAsync();
        } catch (IOException e) {
            Log.w(TAG, "Não foi possível abrir o asset de splash, pulando para o emulador", e);
            goToEmulator();
        }
    }

    @Override
    public void onSurfaceTextureSizeChanged(SurfaceTexture surfaceTexture, int width, int height) {
        // Nada a fazer: o vídeo preenche a view inteira.
    }

    @Override
    public boolean onSurfaceTextureDestroyed(SurfaceTexture surfaceTexture) {
        surfaceReady = false;
        return true;
    }

    @Override
    public void onSurfaceTextureUpdated(SurfaceTexture surfaceTexture) {
        // Nada a fazer.
    }

    private void goToEmulator() {
        if (navigated) {
            return;
        }
        navigated = true;
        releasePlayer();
        startActivity(new Intent(this, XenonActivity.class));
        finish();
    }

    private void releasePlayer() {
        if (mediaPlayer != null) {
            try {
                if (playerPrepared) {
                    mediaPlayer.stop();
                }
            } catch (IllegalStateException ignored) {
                // Já parado/solto, sem problema.
            }
            mediaPlayer.release();
            mediaPlayer = null;
        }
    }

    @Override
    protected void onDestroy() {
        releasePlayer();
        super.onDestroy();
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
