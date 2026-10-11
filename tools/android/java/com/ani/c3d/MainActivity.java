package com.ani.c3d;

import android.util.Log;
import android.view.KeyEvent;

import org.libsdl.app.SDLActivity;

/** SDL's activity, pointed at libmain.so. Also takes the remote's volume +/- keys itself (logged, then handed to the app). */
public class MainActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        int code = event.getKeyCode();
        if (code == KeyEvent.KEYCODE_VOLUME_UP || code == KeyEvent.KEYCODE_VOLUME_DOWN || code == KeyEvent.KEYCODE_VOLUME_MUTE) {
            // visible with:  adb logcat -s ani-c3d-key
            Log.i("ani-c3d-key", "volume key " + KeyEvent.keyCodeToString(code) + " action=" + (event.getAction() == KeyEvent.ACTION_DOWN ? "down" : "up")
                    + " repeat=" + event.getRepeatCount() + " source=0x" + Integer.toHexString(event.getSource()));
            if (event.getAction() == KeyEvent.ACTION_DOWN) onNativeKeyDown(code);
            else onNativeKeyUp(code);
            return true;  // the app owns the volume: the system's own bar stays away
        }
        return super.dispatchKeyEvent(event);
    }
}
