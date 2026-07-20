package com.perfectdark.port;

import androidx.appcompat.app.AppCompatActivity;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.TextView;
import android.widget.Toast;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AlertDialog;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.security.DigestInputStream;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

/**
 * Launcher that ensures the Perfect Dark ROM exists in
 * getExternalFilesDir(null)/data as pd.ntsc-final.z64 before starting SDL.
 */
public class LauncherActivity extends AppCompatActivity {
    private static final String ROM_FILE_NAME = "pd.ntsc-final.z64";
    // Primary: NTSC-U Rev 1 (v1.1) .z64 (recommended)
    private static final String MD5_NTSC_V11 = "e03b088b6ac9e0080440efed07c1e40f";
    // Secondary: NTSC-U v1.0 .z64 (not recommended, but optionally allowed)
    private static final String MD5_NTSC_V10 = "7f4171b0c8d17815be37913f535e4e93";

    private View missingRomView;
    private TextView infoText;
    private Button pickRomButton;

    private final ActivityResultLauncher<String[]> romPicker =
            registerForActivityResult(new ActivityResultContracts.OpenDocument(), this::onRomPicked);

    @Override
    protected void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        ensureDataDir();

        // If the ROM already exists, launch the game directly in VR (skip this activity)
        if (romExists()) {
            File target = new File(new File(getExternalFilesDir(null), "data"), ROM_FILE_NAME);
            int hashStatus = checkRomHash(target);
            if (hashStatus == 0 || hashStatus == 1) {
                // ROM valide → lancer directement MainActivity
                android.util.Log.i("PerfectDark", "ROM found, launching VR mode directly");
                Intent intent = new Intent(this, MainActivity.class);
                intent.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_NEW_TASK);
                startActivity(intent);
                finish();
                return;
            }
        }

        // No ROM found -> display the selection UI
        setContentView(R.layout.activity_launcher);
        missingRomView = findViewById(R.id.missingRomContainer);
        infoText = findViewById(R.id.infoText);
        pickRomButton = findViewById(R.id.pickRomButton);
        pickRomButton.setOnClickListener(v -> openRomPicker());
        showMissingRomUi();
    }


    private void ensureDataDir() {
        File dataDir = new File(getExternalFilesDir(null), "data");
        if (!dataDir.exists()) {
            //noinspection ResultOfMethodCallIgnored
            dataDir.mkdirs();
        }
    }

    private boolean romExists() {
        File target = new File(new File(getExternalFilesDir(null), "data"), ROM_FILE_NAME);
        return target.exists() && target.length() > 0;
    }

    private void showMissingRomUi() {
        missingRomView.setVisibility(View.VISIBLE);
        infoText.setText("ROM not found. Select your Perfect Dark NTSC (z64) ROM to proceed.\nIt will be copied to Android/data/com.perfectdark.port/files/data as " + ROM_FILE_NAME + ".");
    }

    private void openRomPicker() {
        // Use SAF OpenDocument so we can persist read permission if supported.
        romPicker.launch(new String[]{"application/octet-stream", "*/*"});
    }

    private void onRomPicked(@Nullable Uri uri) {
        if (uri == null) {
            Toast.makeText(this, "No file selected", Toast.LENGTH_SHORT).show();
            return;
        }

        // Take persistable permission so we can read during copy
        final int flags = Intent.FLAG_GRANT_READ_URI_PERMISSION;
        try {
            getContentResolver().takePersistableUriPermission(uri, flags);
        } catch (Exception ignored) {
            // Not critical; some providers don't support persistable perms
        }

        try {
            copyRomToAppData(uri);
        } catch (IOException e) {
            Toast.makeText(this, "Failed to copy ROM: " + e.getMessage(), Toast.LENGTH_LONG).show();
            return;
        }

        if (romExists()) {
            File target = new File(new File(getExternalFilesDir(null), "data"), ROM_FILE_NAME);
            int hashStatus = checkRomHash(target);
            if (hashStatus == 0) {
                Toast.makeText(this, "ROM verified — starting game", Toast.LENGTH_SHORT).show();
                startGame();
            } else if (hashStatus == 1) {
                showV10WarningDialog(target);
            } else {
                showHashMismatchDialog(target);
            }
        } else {
            Toast.makeText(this, "ROM copy failed", Toast.LENGTH_LONG).show();
        }
    }

    private void copyRomToAppData(Uri sourceUri) throws IOException {
        File dataDir = new File(getExternalFilesDir(null), "data");
        if (!dataDir.exists()) {
            //noinspection ResultOfMethodCallIgnored
            dataDir.mkdirs();
        }

        File target = new File(dataDir, ROM_FILE_NAME);

        try (InputStream in = getContentResolver().openInputStream(sourceUri);
             FileOutputStream out = new FileOutputStream(target)) {
            if (in == null) throw new IOException("Unable to open selected file");
            byte[] buf = new byte[8192];
            int read;
            while ((read = in.read(buf)) != -1) {
                out.write(buf, 0, read);
            }
            out.flush();
        }
    }

    private void startGame() {
        android.util.Log.i("PerfectDark", "ROM ready, restarting MainActivity in VR mode");
        // Relaunch MainActivity in VR mode
        Intent intent = new Intent(this, MainActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TASK | Intent.FLAG_ACTIVITY_NEW_TASK);
        startActivity(intent);
        finish();
    }

    // Returns: 0 = matches v1.1 (recommended), 1 = matches v1.0 (allowed), -1 = mismatch/error
    private int checkRomHash(File file) {
        try {
            String md5 = computeMd5(file);
            if (MD5_NTSC_V11.equalsIgnoreCase(md5)) return 0;
            if (MD5_NTSC_V10.equalsIgnoreCase(md5)) return 1;
            return -1;
        } catch (Exception e) {
            Toast.makeText(this, "Hash check failed: " + e.getMessage(), Toast.LENGTH_SHORT).show();
            return -1;
        }
    }

    private void showHashMismatchDialog(File target) {
        String computed;
        try {
            computed = computeMd5(target);
        } catch (Exception e) {
            computed = "<error>";
        }

        new AlertDialog.Builder(this)
                .setTitle("Wrong ROM version")
                .setMessage("Expected NTSC-U v1.1 ROM (md5: " + MD5_NTSC_V11 + ")\nAlso allowed (not recommended): v1.0 (md5: " + MD5_NTSC_V10 + ")\n\nGot: " + computed + "\n\nPick a different .z64 ROM?")
                .setPositiveButton("Pick another", (d, w) -> {
                    // Remove the copied file to avoid confusion
                    try { //noinspection ResultOfMethodCallIgnored
                        target.delete();
                    } catch (Exception ignored) {}
                    showMissingRomUi();
                })
                .setNegativeButton("Proceed anyway", (d, w) -> startGame())
                .setCancelable(false)
                .show();
    }

    private void showV10WarningDialog(File target) {
        new AlertDialog.Builder(this)
                .setTitle("NTSC v1.0 detected")
                .setMessage("You selected NTSC-U v1.0 (not recommended).\nThe port targets v1.1; some content may not work.\n\nProceed with v1.0 or pick a different ROM?")
                .setPositiveButton("Proceed", (d, w) -> startGame())
                .setNegativeButton("Pick another", (d, w) -> {
                    try { //noinspection ResultOfMethodCallIgnored
                        target.delete();
                    } catch (Exception ignored) {}
                    showMissingRomUi();
                })
                .setCancelable(false)
                .show();
    }

    private String computeMd5(File file) throws IOException, NoSuchAlgorithmException {
        MessageDigest md = MessageDigest.getInstance("MD5");
        byte[] buffer = new byte[8192];
        int read;
        try (InputStream in = new java.io.FileInputStream(file);
             DigestInputStream din = new DigestInputStream(in, md)) {
            while ((read = din.read(buffer)) != -1) {
                // digest updated via DigestInputStream
            }
        }
        byte[] digest = md.digest();
        StringBuilder sb = new StringBuilder(digest.length * 2);
        for (byte b : digest) {
            sb.append(String.format("%02x", b));
        }
        return sb.toString();
    }
}
