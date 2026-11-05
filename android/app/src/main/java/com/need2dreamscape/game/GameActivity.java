package com.need2dreamscape.game;

import java.io.File;
import java.util.ArrayList;
import java.util.Collections;
import org.libsdl.app.SDLActivity;

public final class GameActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    @Override
    protected String[] getArguments() {
        ArrayList<String> arguments = new ArrayList<>();
        Collections.addAll(arguments, "--assets",
            new File(getFilesDir(), LauncherActivity.DATA_DIRECTORY).getAbsolutePath());
        String[] extra = getIntent().getStringArrayExtra("arguments");
        if (extra != null) Collections.addAll(arguments, extra);
        return arguments.toArray(new String[0]);
    }
}
