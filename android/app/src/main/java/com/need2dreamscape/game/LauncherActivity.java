package com.need2dreamscape.game;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.os.Bundle;
import android.view.Gravity;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

public final class LauncherActivity extends Activity {
    static final String DATA_DIRECTORY = "game-v158-android1";

    @Override
    public void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout layout = new LinearLayout(this);
        layout.setGravity(Gravity.CENTER);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setBackgroundColor(Color.rgb(8, 12, 22));
        TextView title = new TextView(this);
        title.setText("NEED2DREAMSCAPE");
        title.setTextColor(Color.rgb(163, 187, 255));
        title.setTextSize(26);
        title.setPadding(16, 16, 16, 24);
        layout.addView(title);
        ProgressBar progress = new ProgressBar(this);
        layout.addView(progress);
        TextView status = new TextView(this);
        status.setText("Preparando el juego…");
        status.setTextColor(Color.LTGRAY);
        status.setPadding(16, 24, 16, 16);
        layout.addView(status);
        setContentView(layout);
        new Thread(() -> {
            try {
                prepareAssets();
                runOnUiThread(() -> {
                    if (!isFinishing()) {
                        Intent game = new Intent(this, GameActivity.class);
                        game.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP);
                        game.putExtras(getIntent());
                        startActivity(game);
                        finish();
                    }
                });
            } catch (IOException error) {
                runOnUiThread(() -> {
                    progress.setVisibility(ProgressBar.GONE);
                    status.setText("No se pudo preparar el juego. " + error.getMessage());
                });
            }
        }, "n2d-assets").start();
    }

    private void prepareAssets() throws IOException {
        File directory = new File(getFilesDir(), DATA_DIRECTORY);
        File ready = new File(directory, ".ready");
        if (ready.isFile()) return;
        if (!directory.isDirectory() && !directory.mkdirs()) {
            throw new IOException("No se pudo crear la carpeta del juego.");
        }
        String prefix = directory.getCanonicalPath() + File.separator;
        byte[] buffer = new byte[65536];
        try (ZipInputStream input = new ZipInputStream(getAssets().open("game-assets.zip"))) {
            ZipEntry entry;
            while ((entry = input.getNextEntry()) != null) {
                File destination = new File(directory, entry.getName());
                if (!destination.getCanonicalPath().startsWith(prefix)) {
                    throw new IOException("Ruta de recurso inválida.");
                }
                File parent = entry.isDirectory() ? destination : destination.getParentFile();
                if (!parent.isDirectory() && !parent.mkdirs()) {
                    throw new IOException("No se pudo guardar un recurso.");
                }
                if (!entry.isDirectory()) {
                    try (FileOutputStream output = new FileOutputStream(destination)) {
                        int count;
                        while ((count = input.read(buffer)) != -1) output.write(buffer, 0, count);
                    }
                }
                input.closeEntry();
            }
        }
        if (!ready.createNewFile()) throw new IOException("No se pudo completar la instalación.");
    }
}
