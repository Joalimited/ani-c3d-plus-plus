#ifdef __ANDROID__
#include "androidplayer.hpp"

#include <SDL.h>
#include <jni.h>

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

#include "../util/http.hpp"
#include "../util/strutil.hpp"

namespace aplayer {
static JavaVM* g_vm = nullptr;
static jclass g_cls = nullptr;
static jmethodID m_init_surface, m_open, m_stop, m_paused, m_seek, m_volume, m_speed, m_poll, m_frame, m_error;
static bool g_ok = false;

// a JNIEnv for the calling thread (attached on first use, detached when the thread ends)
static JNIEnv* env() {
    struct Att {
        JNIEnv* e = nullptr;
        bool attached = false;
        ~Att() {
            if (attached && g_vm) g_vm->DetachCurrentThread();
        }
    };
    thread_local Att a;
    if (!a.e && g_vm) {
        if (g_vm->GetEnv((void**)&a.e, JNI_VERSION_1_6) != JNI_OK) {
            if (g_vm->AttachCurrentThread(&a.e, nullptr) == JNI_OK) a.attached = true;
            else a.e = nullptr;
        }
    }
    return a.e;
}

bool init() {
    JNIEnv* e = (JNIEnv*)SDL_AndroidGetJNIEnv();
    if (!e) return false;
    e->GetJavaVM(&g_vm);
    jclass c = e->FindClass("com/ani/c3d/VideoPlayer");
    if (!c) {
        e->ExceptionClear();
        return false;
    }
    g_cls = (jclass)e->NewGlobalRef(c);
    m_init_surface = e->GetStaticMethodID(g_cls, "initSurface", "(I)V");
    m_open = e->GetStaticMethodID(g_cls, "open", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;I)V");
    m_stop = e->GetStaticMethodID(g_cls, "stop", "()V");
    m_paused = e->GetStaticMethodID(g_cls, "setPaused", "(Z)V");
    m_seek = e->GetStaticMethodID(g_cls, "seekTo", "(I)V");
    m_volume = e->GetStaticMethodID(g_cls, "setVolume", "(F)V");
    m_speed = e->GetStaticMethodID(g_cls, "setSpeed", "(F)V");
    m_poll = e->GetStaticMethodID(g_cls, "poll", "()[D");
    m_frame = e->GetStaticMethodID(g_cls, "frame", "()[F");
    m_error = e->GetStaticMethodID(g_cls, "lastError", "()Ljava/lang/String;");
    g_ok = !e->ExceptionCheck();
    if (!g_ok) e->ExceptionClear();
    return g_ok;
}

void init_surface(unsigned tex) {
    JNIEnv* e = env();
    if (!g_ok || !e) return;
    e->CallStaticVoidMethod(g_cls, m_init_surface, (jint)tex);
    if (e->ExceptionCheck()) e->ExceptionClear();
}

static std::string jstr(JNIEnv* e, jstring s) {
    if (!s) return "";
    const char* c = e->GetStringUTFChars(s, nullptr);
    std::string out = c ? c : "";
    if (c) e->ReleaseStringUTFChars(s, c);
    return out;
}

void java_open(const std::string& url, const std::string& referer, const std::string& ua, int start_ms) {
    JNIEnv* e = env();
    if (!g_ok || !e) return;
    jstring a = e->NewStringUTF(url.c_str()), b = e->NewStringUTF(referer.c_str()), c = e->NewStringUTF(ua.c_str());
    e->CallStaticVoidMethod(g_cls, m_open, a, b, c, (jint)start_ms);
    if (e->ExceptionCheck()) e->ExceptionClear();
    e->DeleteLocalRef(a);
    e->DeleteLocalRef(b);
    e->DeleteLocalRef(c);
}
#define CALL_VOID(m, ...)                                  \
    do {                                                   \
        JNIEnv* e = env();                                 \
        if (!g_ok || !e) return;                           \
        e->CallStaticVoidMethod(g_cls, m, ##__VA_ARGS__);  \
        if (e->ExceptionCheck()) e->ExceptionClear();      \
    } while (0)
void java_stop() { CALL_VOID(m_stop); }
void java_set_paused(bool p) { CALL_VOID(m_paused, (jboolean)p); }
void java_seek(double s) { CALL_VOID(m_seek, (jint)(std::max(0.0, s) * 1000)); }
void java_volume(double v) { CALL_VOID(m_volume, (jfloat)v); }
void java_speed(double s) { CALL_VOID(m_speed, (jfloat)s); }

Poll java_poll() {
    Poll p;
    JNIEnv* e = env();
    if (!g_ok || !e) return p;
    jdoubleArray arr = (jdoubleArray)e->CallStaticObjectMethod(g_cls, m_poll);
    if (e->ExceptionCheck()) {
        e->ExceptionClear();
        return p;
    }
    if (!arr) return p;
    double v[6] = {0, 0, 0, 0, 0, 0};
    e->GetDoubleArrayRegion(arr, 0, 6, v);
    e->DeleteLocalRef(arr);
    p.state = (int)v[0];
    p.pos = v[1];
    p.dur = v[2];
    p.paused = v[3] != 0;
    p.w = (int)v[4];
    p.h = (int)v[5];
    return p;
}

std::string java_error() {
    JNIEnv* e = env();
    if (!g_ok || !e) return "";
    jstring s = (jstring)e->CallStaticObjectMethod(g_cls, m_error);
    if (e->ExceptionCheck()) {
        e->ExceptionClear();
        return "";
    }
    std::string out = jstr(e, s);
    if (s) e->DeleteLocalRef(s);
    return out;
}

Frame frame() {
    Frame f;
    JNIEnv* e = env();
    if (!g_ok || !e) return f;
    static float last[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    jfloatArray arr = (jfloatArray)e->CallStaticObjectMethod(g_cls, m_frame);
    if (e->ExceptionCheck()) e->ExceptionClear();
    if (arr) {
        e->GetFloatArrayRegion(arr, 0, 16, last);
        e->DeleteLocalRef(arr);
    }
    Poll p = java_poll();
    f.showing = p.state == PREPARING || p.state == PLAYING;
    std::copy(last, last + 16, f.matrix);
    f.w = p.w;
    f.h = p.h;
    return f;
}

// ---------------------------------------------------------------- subtitles
struct Cue {
    double a, b;
    std::string text;
};
static std::mutex g_sub_mu;
static std::vector<Cue> g_cues;
static bool g_sub_on = true;
static std::atomic<long long> g_sub_gen{0};

static bool parse_time(const std::string& s, double& out) {
    unsigned h = 0, m = 0, sec = 0, ms = 0;
    char buf[64];
    std::snprintf(buf, sizeof buf, "%s", s.c_str());
    for (char& c : buf) if (c == ',') c = '.';
    if (std::sscanf(buf, "%u:%u:%u.%u", &h, &m, &sec, &ms) == 4) {
        out = h * 3600.0 + m * 60 + sec + ms / 1000.0;
        return true;
    }
    if (std::sscanf(buf, "%u:%u.%u", &m, &sec, &ms) == 3) {
        out = m * 60.0 + sec + ms / 1000.0;
        return true;
    }
    return false;
}

static std::string strip_tags(const std::string& s) {
    std::string out;
    bool tag = false;
    for (char c : s) {
        if (c == '<') tag = true;
        else if (c == '>') tag = false;
        else if (!tag) out += c;
    }
    return out;
}

static std::vector<Cue> parse_cues(const std::string& body) {
    std::vector<Cue> cues;
    std::string txt = body;
    txt.erase(std::remove(txt.begin(), txt.end(), '\r'), txt.end());
    size_t pos = 0;
    Cue cur{0, 0, ""};
    bool in_cue = false;
    auto flush = [&] {
        if (in_cue && !cur.text.empty()) cues.push_back(cur);
        in_cue = false;
        cur.text.clear();
    };
    while (pos <= txt.size()) {
        size_t nl = txt.find('\n', pos);
        std::string line = txt.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? txt.size() + 1 : nl + 1;
        size_t arrow = line.find("-->");
        if (arrow != std::string::npos) {
            flush();
            std::string a = su::strip(line.substr(0, arrow)), rest = su::strip(line.substr(arrow + 3));
            size_t sp = rest.find(' ');
            std::string b = sp == std::string::npos ? rest : rest.substr(0, sp);
            if (parse_time(a, cur.a) && parse_time(b, cur.b)) in_cue = true;
        } else if (line.empty()) {
            flush();
        } else if (in_cue) {
            if (!cur.text.empty()) cur.text += "\n";
            cur.text += strip_tags(line);
        }
    }
    flush();
    std::sort(cues.begin(), cues.end(), [](const Cue& x, const Cue& y) { return x.a < y.a; });
    return cues;
}

void clear_subtitles() {
    g_sub_gen++;
    std::lock_guard<std::mutex> lk(g_sub_mu);
    g_cues.clear();
}

void load_subtitles(const std::string& url, const std::string& referer) {
    long long gen = ++g_sub_gen;
    std::cout << "ani-c3d: subtitles: loading " << url << std::endl;
    {
        std::lock_guard<std::mutex> lk(g_sub_mu);
        g_cues.clear();
    }
    if (url.empty()) return;
    std::thread([url, referer, gen] {
        try {
            http::Req r;
            r.url = url;
            r.timeout = 20;
            r.headers = {{"User-Agent", "Mozilla/5.0 (Linux; Android 11; AFTKRT) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"}};
            if (!referer.empty()) {
                r.headers.push_back({"Referer", referer});
                size_t p = referer.find("://");
                size_t e = p == std::string::npos ? std::string::npos : referer.find('/', p + 3);
                r.headers.push_back({"Origin", e == std::string::npos ? referer : referer.substr(0, e)});
            }
            auto resp = http::request(r);
            std::cout << "ani-c3d: subtitles: HTTP " << resp.status << ", " << resp.body.size() << " bytes" << std::endl;
            if (resp.status >= 400) return;
            auto cues = parse_cues(resp.body);
            std::cout << "ani-c3d: subtitles: " << cues.size() << " cues" << std::endl;
            if (gen != g_sub_gen.load()) return;
            std::lock_guard<std::mutex> lk(g_sub_mu);
            g_cues = std::move(cues);
        } catch (const std::exception& e) {
            std::cout << "ani-c3d: subtitles: failed: " << e.what() << std::endl;
        }
    }).detach();
}

std::string subtitle(double t) {
    std::lock_guard<std::mutex> lk(g_sub_mu);
    if (!g_sub_on || g_cues.empty()) return "";
    std::string out;
    for (auto& c : g_cues) {
        if (c.a > t) break;
        if (t < c.b) out += (out.empty() ? "" : "\n") + c.text;
    }
    return out;
}

static std::atomic<double> g_vol{100.0};
double volume_pct() { return g_vol.load(); }
void set_volume_pct(double v) {
    v = std::min(100.0, std::max(0.0, v));
    g_vol = v;
    java_volume(v / 100.0);
}

bool subtitles_on() { return g_sub_on; }
void set_subtitles_on(bool on) { g_sub_on = on; }
}  // namespace aplayer
#endif
