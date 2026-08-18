package com.perfectdark.port;

import androidx.appcompat.app.AppCompatActivity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.TextView;
import android.widget.Toast;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AlertDialog;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStream;
import java.security.DigestInputStream;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.LinkedHashMap;
import java.util.Map;

/**
 * Launcher that ensures the Perfect Dark ROM exists in
 * getExternalFilesDir(null)/data as pd.ntsc-final.z64 before starting SDL.
 */
public class LauncherActivity extends AppCompatActivity {
    private static final String ROM_FILE_NAME = "pd.ntsc-final.z64";
    private static final String INI_FILE_NAME = "pd.ini";

    private static final String MD5_NTSC_V11 = "e03b088b6ac9e0080440efed07c1e40f";
    private static final String MD5_NTSC_V10 = "7f4171b0c8d17815be37913f535e4e93";

    // Requested reset values.
    private static final String RESET_WIDTH = "0";
    private static final String RESET_HEIGHT = "0";
    private static final String RESET_MSAA = "1";
    private static final String RESET_VR_RENDER_SCALE = "1";
    private static final String RESET_EXTERNAL_TEXTURES = "0";


    private int currentRomStatus = -1;

    private View missingRomView;
    private TextView infoText;
    private Button pickRomButton;
    private Button startButton;
    private Button resetGraphicsButton;

    private final ActivityResultLauncher romPicker =
            registerForActivityResult(new ActivityResultContracts.OpenDocument(), this::onRomPicked);

    @Override
    protected void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        ensureDataDir();

        setContentView(R.layout.activity_launcher);
        missingRomView = findViewById(R.id.missingRomContainer);
        infoText = findViewById(R.id.infoText);
        pickRomButton = findViewById(R.id.pickRomButton);
        startButton = findViewById(R.id.startButton);
        resetGraphicsButton = findViewById(R.id.resetGraphicsButton);

        pickRomButton.setOnClickListener(v -> openRomPicker());
        startButton.setOnClickListener(v -> onStartClicked());
        resetGraphicsButton.setOnClickListener(v -> onResetGraphicsClicked());

        refreshRomStatusUi();
    }

    @Override
    protected void onResume() {
        super.onResume();
        refreshRomStatusUi();
    }

// ---------------------------------------------------------------------
// ROM detection
// ---------------------------------------------------------------------

    private void ensureDataDir() {
        File dataDir = new File(getExternalFilesDir(null), "data");
        if (!dataDir.exists()) {
//noinspection ResultOfMethodCallIgnored
            dataDir.mkdirs();
        }
    }

    private File getRomDataDir() {
        return new File(getExternalFilesDir(null), "data");
    }

    private File getRomFile() {
        return new File(getRomDataDir(), ROM_FILE_NAME);
    }

    private File getIniFile() {
        return new File(getExternalFilesDir(null), INI_FILE_NAME);
    }

    private boolean romExists() {
        File target = getRomFile();
        return target.exists() && target.length() > 0;
    }

    private void refreshRomStatusUi() {
        missingRomView.setVisibility(View.VISIBLE);

        if (!romExists()) {
            currentRomStatus = -1;
            infoText.setText("ROM not found. Select your Perfect Dark NTSC (z64) ROM to proceed.\n"
                    + "It will be copied to Android/data/com.perfectdark.port/files/data as " + ROM_FILE_NAME + ".");
            setStartEnabled(false);
            return;
        }

        int hashStatus = checkRomHash(getRomFile());
        currentRomStatus = hashStatus;

        switch (hashStatus) {
            case 0:
                infoText.setText("ROM detected: NTSC-U v1.1 (recommended).\nReady to start.");
                setStartEnabled(true);
                break;
            case 1:
                infoText.setText("ROM detected: NTSC-U v1.0 (not recommended).\nYou can start, but some content may not work correctly.");
                setStartEnabled(true);
                break;
            default:
                infoText.setText("A ROM file was found but it does not match the expected version.\nPlease pick a valid Perfect Dark NTSC (z64) ROM.");
                setStartEnabled(false);
                break;
        }
    }

    private void setStartEnabled(boolean enabled) {
        startButton.setEnabled(enabled);
        startButton.setAlpha(enabled ? 1.0f : 0.5f);
    }

    private void openRomPicker() {
        romPicker.launch(new String[]{"application/octet-stream", "*/*"});
    }

    private void onRomPicked(@Nullable Uri uri) {
        if (uri == null) {
            Toast.makeText(this, "No file selected", Toast.LENGTH_SHORT).show();
            return;
        }

        final int flags = Intent.FLAG_GRANT_READ_URI_PERMISSION;
        try {
            getContentResolver().takePersistableUriPermission(uri, flags);
        } catch (Exception ignored) {
        }

        try {
            copyRomToAppData(uri);
        } catch (IOException e) {
            Toast.makeText(this, "Failed to copy ROM: " + e.getMessage(), Toast.LENGTH_LONG).show();
            return;
        }

        if (!romExists()) {
            Toast.makeText(this, "ROM copy failed", Toast.LENGTH_LONG).show();
            refreshRomStatusUi();
            return;
        }

        File target = getRomFile();
        int hashStatus = checkRomHash(target);
        if (hashStatus == 0) {
            Toast.makeText(this, "ROM verified (v1.1)", Toast.LENGTH_SHORT).show();
            refreshRomStatusUi();
        } else if (hashStatus == 1) {
            showV10WarningDialog(target);
        } else {
            showHashMismatchDialog(target);
        }
    }

    private void onStartClicked() {
        if (currentRomStatus != 0 && currentRomStatus != 1) {
            Toast.makeText(this, "No valid ROM selected yet", Toast.LENGTH_SHORT).show();
            return;
        }
        startGame();
    }

    private void copyRomToAppData(Uri sourceUri) throws IOException {
        File dataDir = getRomDataDir();
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
        android.util.Log.i("PerfectDark", "Start clicked, launching MainActivity in VR mode");
        Intent intent = new Intent(this, MainActivity.class);
        intent.putExtra(MainActivity.EXTRA_FROM_LAUNCHER, true);
        intent.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TASK | Intent.FLAG_ACTIVITY_NEW_TASK);
        startActivity(intent);
        finish();
    }

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
            computed = "";
        }

        new AlertDialog.Builder(this)
                .setTitle("Wrong ROM version")
                .setMessage("Expected NTSC-U v1.1 ROM (md5: " + MD5_NTSC_V11 + ")\nAlso allowed (not recommended): v1.0 (md5: " + MD5_NTSC_V10 + ")\n\nGot: " + computed + "\n\nPick a different .z64 ROM?")
                .setPositiveButton("Pick another", (d, w) -> {
                    try {
//noinspection ResultOfMethodCallIgnored
                        target.delete();
                    } catch (Exception ignored) {}
                    refreshRomStatusUi();
                })
                .setNegativeButton("Keep anyway", (d, w) -> refreshRomStatusUi())
                .setCancelable(false)
                .show();
    }

    private void showV10WarningDialog(File target) {
        new AlertDialog.Builder(this)
                .setTitle("NTSC v1.0 detected")
                .setMessage("You selected NTSC-U v1.0 (not recommended).\nThe port targets v1.1; some content may not work.\n\nYou can keep it and press Start later, or pick a different ROM.")
                .setPositiveButton("Keep this ROM", (d, w) -> refreshRomStatusUi())
                .setNegativeButton("Pick another", (d, w) -> {
                    try {
//noinspection ResultOfMethodCallIgnored
                        target.delete();
                    } catch (Exception ignored) {}
                    refreshRomStatusUi();
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

// ---------------------------------------------------------------------
// Reset graphics settings
// ---------------------------------------------------------------------

    /**
     * Resets DefaultWidth=0, DefaultHeight=0, MSAA=1, VRRenderScale=1 and
     * ExternalTextures=0 in pd.ini (file located at the root of
     * getExternalFilesDir(null)). Other existing keys in the file are preserved.
     */
    private void onResetGraphicsClicked() {
        File iniFile = getIniFile();
        Map ini = readIni(iniFile);

        ini.put("DefaultWidth", RESET_WIDTH);
        ini.put("DefaultHeight", RESET_HEIGHT);
        ini.put("MSAA", RESET_MSAA);
        ini.put("VRRenderScale", RESET_VR_RENDER_SCALE);
        ini.put("ExternalTextures", RESET_EXTERNAL_TEXTURES);

        writeIni(iniFile, ini);

        android.util.Log.i("PerfectDark", "Graphics settings reset: DefaultWidth=" + RESET_WIDTH
                + " DefaultHeight=" + RESET_HEIGHT + " MSAA=" + RESET_MSAA
                + " VRRenderScale=" + RESET_VR_RENDER_SCALE
                + " ExternalTextures=" + RESET_EXTERNAL_TEXTURES);
        Toast.makeText(this, "Graphics settings reset", Toast.LENGTH_SHORT).show();
    }

    /**
     * Parses a simple "key=value" .ini file (one entry per line; empty lines or
     * lines starting with ';' or '#' are ignored). Preserves the original order
     * using LinkedHashMap.
     */
    private Map readIni(File file) {
        Map map = new LinkedHashMap<>();
        if (!file.exists()) return map;

        try (BufferedReader reader = new BufferedReader(new FileReader(file))) {
            String line;
            while ((line = reader.readLine()) != null) {
                String trimmed = line.trim();
                if (trimmed.isEmpty() || trimmed.startsWith(";") || trimmed.startsWith("#")) {
                    continue;
                }
                int eq = trimmed.indexOf('=');
                if (eq <= 0) continue;
                String key = trimmed.substring(0, eq).trim();
                String value = trimmed.substring(eq + 1).trim();
                map.put(key, value);
            }
        } catch (IOException e) {
            android.util.Log.e("PerfectDark", "Failed to read pd.ini", e);
        }
        return map;
    }

    private void writeIni(File file, Map<String, String> data) {
        try (FileWriter writer = new FileWriter(file, false)) {
            for (Map.Entry<String, String> entry : data.entrySet()) {
                writer.write(entry.getKey() + "=" + entry.getValue() + "\n");
            }
        } catch (IOException e) {
            android.util.Log.e("PerfectDark", "Failed to write pd.ini", e);
            Toast.makeText(this, "Failed to reset graphics settings: " + e.getMessage(), Toast.LENGTH_LONG).show();
        }
    }
}
