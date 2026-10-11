package com.ani.c3d;

import android.content.Context;
import android.graphics.SurfaceTexture;
import android.media.MediaPlayer;
import android.media.PlaybackParams;
import android.net.Uri;
import android.view.Surface;

import org.libsdl.app.SDLActivity;

import java.util.HashMap;
import java.util.Map;

/**
 * The video side of ani-c3d on Android: a MediaPlayer that renders into a SurfaceTexture. The C++ renderer draws that
 * texture as the full-screen video quad (so the HUD, skip button and subtitles are drawn by the app, over the picture).
 * Everything is static and polled from C++ over JNI (see src/core/androidplayer.cpp).
 */
public final class VideoPlayer {
    public static final int IDLE = 0, PREPARING = 1, PLAYING = 2, ENDED = 3, ERROR = 4;

    private static SurfaceTexture texture;
    private static Surface surface;
    private static volatile boolean frameReady;
    private static MediaPlayer player;
    private static volatile int state = IDLE;
    private static volatile String error = "";
    private static volatile int videoW, videoH;
    private static volatile boolean wantPaused;
    private static float volume = 1f, speed = 1f;
    private static final Object lock = new Object();
    private static final float[] matrix = new float[16];

    private VideoPlayer() {}

    /** Called on the GL thread with the external-OES texture the renderer made. */
    public static void initSurface(int texId) {
        texture = new SurfaceTexture(texId);
        texture.setOnFrameAvailableListener(t -> frameReady = true);
        surface = new Surface(texture);
    }

    public static void open(String url, String referer, String userAgent, int startMs) {
        synchronized (lock) {
            release();
            state = PREPARING;
            error = "";
            videoW = videoH = 0;
            wantPaused = false;
            try {
                MediaPlayer mp = new MediaPlayer();
                player = mp;
                Map<String, String> headers = new HashMap<>();
                if (referer != null && !referer.isEmpty()) headers.put("Referer", referer);
                if (userAgent != null && !userAgent.isEmpty()) headers.put("User-Agent", userAgent);
                Context ctx = SDLActivity.getContext();
                mp.setDataSource(ctx, Uri.parse(url), headers);
                mp.setSurface(surface);
                mp.setOnVideoSizeChangedListener((m, w, h) -> { videoW = w; videoH = h; });
                mp.setOnPreparedListener(m -> {
                    synchronized (lock) {
                        if (m != player) return;
                        videoW = m.getVideoWidth();
                        videoH = m.getVideoHeight();
                        m.setVolume(volume, volume);
                        try {
                            PlaybackParams pp = new PlaybackParams();
                            pp.setSpeed(speed);
                            m.setPlaybackParams(pp);
                            if (wantPaused) m.pause();
                        } catch (Exception ignored) {}
                        if (startMs > 0) m.seekTo(startMs);
                        if (!wantPaused) m.start();
                        state = PLAYING;
                    }
                });
                mp.setOnCompletionListener(m -> { if (m == player) state = ENDED; });
                mp.setOnErrorListener((m, what, extra) -> {
                    if (m == player) {
                        error = "player error " + what + "/" + extra;
                        state = ERROR;
                    }
                    return true;
                });
                mp.prepareAsync();
            } catch (Exception e) {
                error = String.valueOf(e);
                state = ERROR;
            }
        }
    }

    private static void release() {
        MediaPlayer mp = player;
        player = null;
        if (mp != null) {
            try { mp.stop(); } catch (Exception ignored) {}
            try { mp.release(); } catch (Exception ignored) {}
        }
        state = IDLE;
    }

    public static void stop() {
        synchronized (lock) { release(); }
    }

    public static void setPaused(boolean p) {
        synchronized (lock) {
            wantPaused = p;
            MediaPlayer mp = player;
            if (mp == null || state != PLAYING) return;
            try { if (p) mp.pause(); else mp.start(); } catch (Exception ignored) {}
        }
    }

    public static void seekTo(int ms) {
        synchronized (lock) {
            MediaPlayer mp = player;
            if (mp == null || state != PLAYING) return;
            try { mp.seekTo(Math.max(0, ms)); } catch (Exception ignored) {}
        }
    }

    public static void setVolume(float v) {
        synchronized (lock) {
            volume = Math.max(0f, Math.min(1.3f, v));
            MediaPlayer mp = player;
            if (mp != null) try { mp.setVolume(Math.min(1f, volume), Math.min(1f, volume)); } catch (Exception ignored) {}
        }
    }

    public static void setSpeed(float s) {
        synchronized (lock) {
            speed = s;
            MediaPlayer mp = player;
            if (mp == null || state != PLAYING) return;
            try {
                PlaybackParams pp = mp.getPlaybackParams();
                pp.setSpeed(s);
                mp.setPlaybackParams(pp);
                if (wantPaused) mp.pause();
            } catch (Exception ignored) {}
        }
    }

    /** {state, position seconds, duration seconds, paused, videoW, videoH}; position/duration are 0 until known. */
    public static double[] poll() {
        double pos = 0, dur = 0;
        synchronized (lock) {
            MediaPlayer mp = player;
            if (mp != null && state == PLAYING) {
                try {
                    pos = mp.getCurrentPosition() / 1000.0;
                    dur = mp.getDuration() / 1000.0;
                } catch (Exception ignored) {}
            }
        }
        return new double[] { state, pos, dur, wantPaused ? 1 : 0, videoW, videoH };
    }

    public static String lastError() { return error; }

    /** GL thread: latch the newest decoded frame into the texture. Returns the 4x4 texture transform, or null if nothing new. */
    public static float[] frame() {
        SurfaceTexture t = texture;
        if (t == null || !frameReady) return null;
        frameReady = false;
        try {
            t.updateTexImage();
            t.getTransformMatrix(matrix);
        } catch (Exception e) {
            return null;
        }
        return matrix;
    }
}
