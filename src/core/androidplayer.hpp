// Video playback on Android (Fire TV): a JNI bridge to com.ani.c3d.VideoPlayer (a MediaPlayer drawing into a
// SurfaceTexture, which the renderer shows as the full-screen video). Subtitles (WebVTT) are parsed here and drawn by the HUD.
#pragma once
#include <string>

namespace aplayer {
#ifdef __ANDROID__
bool init();                      // SDL main thread: caches the Java class
void init_surface(unsigned tex);  // GL thread: hands the external-OES texture to the Java side
struct Frame {
    bool showing = false;  // a video is on (or loading) - the renderer shows it instead of the 3D world
    float matrix[16];      // SurfaceTexture transform
    int w = 0, h = 0;
};
Frame frame();  // GL thread, once per rendered frame: latches the newest picture
std::string subtitle(double t);  // the cue (lines joined with \n) visible at time t, "" if none
bool subtitles_on();
void set_subtitles_on(bool on);
double volume_pct();            // 0..100, kept across episodes
void set_volume_pct(double v);  // also applies it to the player

// ---- used by core/mpvlink_android.cpp (the fake mpv the engine talks to)
enum State { IDLE = 0, PREPARING = 1, PLAYING = 2, ENDED = 3, ERROR_ = 4 };
void java_open(const std::string& url, const std::string& referer, const std::string& user_agent, int start_ms);
void java_stop();
void java_set_paused(bool p);
void java_seek(double seconds);
void java_volume(double v);  // 0..1.3
void java_speed(double s);
struct Poll {
    int state = IDLE;
    double pos = 0, dur = 0;
    bool paused = false;
    int w = 0, h = 0;
};
Poll java_poll();
std::string java_error();
void load_subtitles(const std::string& url, const std::string& referer);  // async fetch + parse (WebVTT / SRT)
void clear_subtitles();
#else
inline bool init() { return false; }
inline void init_surface(unsigned) {}
struct Frame {
    bool showing = false;
    float matrix[16] = {};
    int w = 0, h = 0;
};
inline Frame frame() { return {}; }
inline std::string subtitle(double) { return ""; }
inline bool subtitles_on() { return false; }
inline double volume_pct() { return 100; }
inline void set_volume_pct(double) {}
#endif
}  // namespace aplayer
