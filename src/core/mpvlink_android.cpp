// The "mpv" of the Android build: no process and no socket. Proc::spawn reads the same command line the engine builds for
// mpv and starts the Java MediaPlayer; the Link speaks the subset of mpv's JSON IPC the engine uses and answers with the
// same property-change / end-file events, so engine.cpp is the same code on every platform.
#ifdef __ANDROID__
#include <chrono>
#include <cmath>
#include <iostream>

#include "androidplayer.hpp"
#include "json.hpp"
#include "mpvlink.hpp"

namespace mpvlink {
using json = nlohmann::json;
static std::atomic<long long> g_gen{0};          // the newest playback
static std::atomic<bool> g_finished{false};
static std::mutex g_ref_mu;
static std::string g_referer;  // the current stream's Referer: late subtitle files (sub-add) need the same one or the host answers 403      // the newest playback has ended
static const char* const UA =
    "Mozilla/5.0 (Linux; Android 11; AFTKRT) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

std::unique_ptr<Proc> Proc::spawn(const std::vector<std::string>& args) {
    std::string url, referer, sub;
    int start_ms = 0;
    for (size_t i = 1; i < args.size(); i++) {
        const std::string& a = args[i];
        auto val = [&](const char* key) -> const char* {
            size_t n = std::char_traits<char>::length(key);
            return a.compare(0, n, key) == 0 ? a.c_str() + n : nullptr;
        };
        if (const char* v = val("--referrer=")) referer = v;
        else if (const char* v2 = val("--start=")) start_ms = (int)(std::atof(v2) * 1000);
        else if (const char* v3 = val("--sub-file=")) sub = v3;
        else if (a.rfind("--", 0) == 0) continue;
        else url = a;  // the last plain argument is the media URL
    }
    std::cout << "ani-c3d: play url=" << url.substr(0, 90) << " sub=" << (sub.empty() ? "(none)" : sub.substr(0, 90)) << std::endl;
    if (url.empty()) return nullptr;
    {
        std::lock_guard<std::mutex> lk(g_ref_mu);
        g_referer = referer;
    }
    aplayer::clear_subtitles();
    if (!sub.empty()) aplayer::load_subtitles(sub, referer);
    g_finished = false;
    std::unique_ptr<Proc> p(new Proc());
    p->gen_ = ++g_gen;
    aplayer::java_open(url, referer, UA, start_ms);
    aplayer::java_volume(aplayer::volume_pct() / 100.0);
    return p;
}

Proc::~Proc() = default;

bool Proc::running() {
    if (gen_ != g_gen.load() || g_finished.load()) return false;
    auto st = aplayer::java_poll().state;
    return st == aplayer::PREPARING || st == aplayer::PLAYING;
}

void Proc::wait() {
    while (running()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

void Proc::terminate() {
    if (gen_ == g_gen.load()) {
        aplayer::java_stop();
        g_finished = true;
    }
}

std::unique_ptr<Link> Link::connect(const std::string&, Proc* proc, int) {
    std::unique_ptr<Link> l(new Link());
    l->gen_ = proc ? proc->generation() : g_gen.load();
    Link* raw = l.get();
    l->poll_ = std::thread([raw] { raw->poll_loop(); });
    return l;
}

Link::~Link() { close(); }

void Link::close() {
    if (closed_) return;
    closed_ = true;
    stop_ = true;
    cv_.notify_all();
    if (poll_.joinable()) {
        if (poll_.get_id() == std::this_thread::get_id()) poll_.detach();
        else poll_.join();
    }
}

void Link::push(const std::string& line) {
    {
        std::lock_guard<std::mutex> lk(qmu_);
        q_.push_back(line);
    }
    cv_.notify_one();
}

bool Link::readline(std::string& line) {
    std::unique_lock<std::mutex> lk(qmu_);
    cv_.wait(lk, [this] { return !q_.empty() || stop_.load(); });
    if (q_.empty()) return false;
    line = std::move(q_.front());
    q_.pop_front();
    return true;
}

static std::string prop(const char* name, const json& v) { return json({{"event", "property-change"}, {"name", name}, {"data", v}}).dump(); }

void Link::poll_loop() {
    double sent_pos = -1, sent_dur = -1, sent_vol = -1;
    bool started = false;
    while (!stop_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        if (gen_ != g_gen.load()) break;  // replaced by a newer playback
        aplayer::Poll p = aplayer::java_poll();
        if (p.state == aplayer::PLAYING) {
            started = true;
            if (force_emit_) {
                force_emit_ = false;
                push(prop("pause", paused_));
                push(prop("volume", volume_));
                push(prop("mute", muted_));
                push(prop("speed", speed_));
                sent_dur = -1;
            }
            double vp = aplayer::volume_pct();
            if (std::fabs(vp - sent_vol) >= 0.5) {  // the remote's volume keys
                sent_vol = vp;
                volume_ = vp;
                push(prop("volume", vp));
            }
            pos_ = p.pos;
            dur_ = p.dur;
            if (p.dur > 0 && std::fabs(p.dur - sent_dur) > 0.5) {
                sent_dur = p.dur;
                push(prop("duration", p.dur));
            }
            if (std::fabs(p.pos - sent_pos) >= 0.2) {
                sent_pos = p.pos;
                push(prop("time-pos", p.pos));
            }
        } else if (p.state == aplayer::ENDED) {
            g_finished = true;
            push(json({{"event", "end-file"}, {"reason", "eof"}}).dump());
            break;
        } else if (p.state == aplayer::ERROR_) {
            g_finished = true;
            push(json({{"event", "end-file"}, {"reason", "error"}, {"file_error", aplayer::java_error()}}).dump());
            break;
        } else if (p.state == aplayer::IDLE && started) {
            break;  // stopped
        }
    }
    stop_ = true;
    cv_.notify_all();
}

void Link::send(const std::string& json_line) {
    json j = json::parse(json_line, nullptr, false);
    if (!j.is_object() || !j.contains("command") || !j["command"].is_array() || j["command"].empty()) return;
    const json& c = j["command"];
    std::string cmd = c[0].is_string() ? c[0].get<std::string>() : "";
    auto arg_s = [&](size_t i) { return c.size() > i && c[i].is_string() ? c[i].get<std::string>() : std::string(); };
    auto arg_n = [&](size_t i, double d = 0) { return c.size() > i && c[i].is_number() ? c[i].get<double>() : d; };
    std::lock_guard<std::mutex> lk(send_mu_);
    if (cmd == "cycle") {
        std::string what = arg_s(1);
        if (what == "pause") {
            paused_ = !paused_;
            aplayer::java_set_paused(paused_);
            push(prop("pause", paused_));
        } else if (what == "mute") {
            muted_ = !muted_;
            aplayer::java_volume(muted_ ? 0.0 : volume_ / 100.0);
            push(prop("mute", muted_));
        } else if (what == "sub-visibility") {
            aplayer::set_subtitles_on(!aplayer::subtitles_on());
        }
    } else if (cmd == "seek") {
        std::string mode = arg_s(2);
        double v = arg_n(1), target = pos_ + v;
        if (mode == "absolute") target = v;
        else if (mode == "absolute-percent") target = dur_ * v / 100.0;
        if (dur_ > 0) target = std::min(target, dur_);
        aplayer::java_seek(target);
        pos_ = std::max(0.0, target);
        push(prop("time-pos", pos_));
    } else if (cmd == "set_property") {
        std::string name = arg_s(1);
        if (name == "volume") {
            volume_ = std::min(100.0, arg_n(2, 100));
            aplayer::set_volume_pct(volume_);
            if (muted_) aplayer::java_volume(0.0);
            push(prop("volume", volume_));
        } else if (name == "speed") {
            speed_ = arg_n(2, 1.0);
            aplayer::java_speed(speed_);
            push(prop("speed", speed_));
        } else if (name == "pause" && c.size() > 2 && c[2].is_boolean()) {
            paused_ = c[2].get<bool>();
            aplayer::java_set_paused(paused_);
            push(prop("pause", paused_));
        }
    } else if (cmd == "sub-add") {
        std::cout << "ani-c3d: sub-add " << arg_s(1).substr(0, 90) << std::endl;
        std::string ref;
        {
            std::lock_guard<std::mutex> lk(g_ref_mu);
            ref = g_referer;
        }
        aplayer::load_subtitles(arg_s(1), ref);
    } else if (cmd == "observe_property") {
        force_emit_ = true;
    }
    // keybind / script-message / fullscreen: nothing to do here (the app draws its own skip button)
}
}  // namespace mpvlink
#endif
