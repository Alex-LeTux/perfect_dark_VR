package com.perfectdark.port;

import org.libsdl.app.SDLActivity;
import android.content.Intent;
import android.os.Bundle;
import android.view.View;
import android.util.Log;
import java.io.File;

public class MainActivity extends SDLActivity {
    private static final String TAG = "PerfectDark";

    // Extra used to indicate that MainActivity was explicitly launched
    // by LauncherActivity (via the "Start" button), rather than by the
    // Android launcher / Meta Store. Without this flag, we always redirect
    // to LauncherActivity first.
    public static final String EXTRA_FROM_LAUNCHER = "com.perfectdark.port.FROM_LAUNCHER";

    private static native void nativeSetVrJavaContext(android.app.Activity activity, android.view.Surface surface);

    static {
        System.loadLibrary("openxr_loader");
        System.loadLibrary("SDL2");
        System.loadLibrary("pd");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        Log.i(TAG, "MainActivity onCreate");

        boolean fromLauncher = getIntent() != null
                && getIntent().getBooleanExtra(EXTRA_FROM_LAUNCHER, false);

        if (!fromLauncher) {
        // MainActivity was started directly (Android launcher, Meta Store,
        // adb shell am start, etc.): always redirect to LauncherActivity,
        // which will display the ROM selection window and handle the Start button.
            Log.i(TAG, "Not launched from LauncherActivity, redirecting to LauncherActivity");
            Intent intent = new Intent(this, LauncherActivity.class);
            startActivity(intent);
            finish();
            return;
        }

    // Explicitly launched from LauncherActivity after ROM validation
    // (Start button) -> start the game directly in VR.
        Log.i(TAG, "Launched from LauncherActivity, starting VR mode");
        initializeGame();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        Log.i(TAG, "onWindowFocusChanged: " + hasFocus);
        if (hasFocus) {
            getWindow().getDecorView().post(this::hideSystemUI);
        }
    }

    private void hideSystemUI() {
        View decorView = getWindow().getDecorView();
        int uiOptions = View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_STABLE;
        decorView.setSystemUiVisibility(uiOptions);
    }

    private void initializeGame() {
        Log.i(TAG, "initializeGame start");

        File dataDir = new File(getExternalFilesDir(null), "data");
        Log.i(TAG, "Data dir: " + dataDir.getAbsolutePath());

        if (!dataDir.exists()) {
            dataDir.mkdirs();
        }

        Log.i(TAG, "Calling nativeInit");
        nativeInit(dataDir.getAbsolutePath());
        Log.i(TAG, "initializeGame complete");
    }

    @Override
    protected void onResume() {
        Log.i(TAG, "MainActivity onResume - VR mode");
        SDLActivity.mHasFocus = true;
        super.onResume();

        new Thread(() -> {
            final int MAX_ATTEMPTS = 50;
            final int DELAY_MS = 100;
            for (int attempt = 0; attempt < MAX_ATTEMPTS; attempt++) {
                try {
                    Thread.sleep(DELAY_MS);
                    android.view.Surface surface = org.libsdl.app.SDLActivity.getNativeSurface();
                    if (surface != null && surface.isValid()) {
                        Log.i(TAG, "SDL Surface found after " + (attempt * DELAY_MS) + "ms");
                        // Called directly from this background thread
                        // xrInitializeLoaderKHR is thread-safe and must NOT block the UI thread
                        nativeSetVrJavaContext(MainActivity.this, surface);
                        return;
                    }
                } catch (InterruptedException e) {
                    Log.e(TAG, "Surface polling interrupted", e);
                    break;
                }
            }
            Log.e(TAG, "SDL Surface timeout after " + (MAX_ATTEMPTS * DELAY_MS) + "ms");
        }, "SurfacePollingThread").start();
    }

    @Override
    protected void onPause() {
        Log.i(TAG, "MainActivity onPause - VR may be going to sleep");
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        Log.i(TAG, "MainActivity onDestroy");
        super.onDestroy();
        nativeDestroy();
    }

    // Native methods
    public native void nativeInit(String dataPath);
    private static native void nativeVrResume();
    public native void nativeDestroy();
}
