#include "engine.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <thread>

#include "../util/db.hpp"
#include "../util/paths.hpp"
#include "../util/re.hpp"
#include "../util/strutil.hpp"
#include "core.hpp"
#include "multisrc.hpp"

using json = nlohmann::json;

static double mono() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
static double wall() { return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count(); }

Engine::Engine() : sock_(paths::ipc_name()) {}
Engine::~Engine() {
    auto p = proc_;
    if (p) p->terminate();
}

double Engine::num(const char* key, double dflt) {
    if (state_.contains(key) && state_[key].is_number()) return state_[key].get<double>();
    return dflt;
}

int Engine::ep_index(const std::string& ep) {
    auto it = std::find(episodes_.begin(), episodes_.end(), ep);
    return it == episodes_.end() ? -1 : (int)(it - episodes_.begin());
}

json Engine::snapshot() {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    json anime = anime_.first.empty() ? json(nullptr) : json{{"id", anime_.first}, {"title", anime_.second}};
    return {{"playing", proc_ != nullptr},
            {"anime", anime},
            {"episode", cur_ep_.empty() ? json(nullptr) : json(cur_ep_)},
            {"mode", mode_},
            {"autonext", autonext_},
            {"pos", num("time-pos")},
            {"duration", num("duration")},
            {"paused", state_.value("pause", json(false)).is_boolean() ? state_.value("pause", false) : false},
            {"volume", num("volume", 100)},
            {"muted", state_.value("mute", json(false)).is_boolean() ? state_.value("mute", false) : false},
            {"speed", num("speed", 1.0)},
            {"skip", skip_ranges()},
            {"source", source_},
            {"autofs", autofs_},
            {"autoskip", {{"intro", autoskip_["intro"]}, {"outro", autoskip_["outro"]}}}};
}

static const char* const SKIP_LUA = R"LUA(-- ani-c3d: an on-screen SKIP INTRO / SKIP CREDITS button inside the mpv window (written by the app at each playback)
local ia, ib, oa, ob = -1, -1, -1, -1
local pos, dur = 0, 0
local overlay = mp.create_osd_overlay("ass-events")
overlay.res_x, overlay.res_y = 1280, 720
local X0, Y0, X1, Y1 = 1000, 590, 1250, 650   -- the button, in 1280x720 overlay coordinates
local shown, which, bound = nil, nil, false

local function mouse_in()
    local mx, my = mp.get_mouse_pos()
    local w, h = mp.get_osd_size()
    if not w or w <= 0 then return false end
    mx, my = mx * 1280 / w, my * 720 / h
    return mx >= X0 and mx <= X1 and my >= Y0 and my <= Y1
end

local function click(t)
    if t.event ~= "up" then return end
    if shown and mouse_in() then
        mp.commandv("script-message", "anic3d-skip", which)
    else
        mp.commandv("cycle", "pause")   -- not on the button: the default click-to-pause
    end
end

local function draw()
    local want, label = nil, nil
    -- only where the intro / credits really are (stream timestamps, chapters or the AniSkip database)
    if ia >= 0 and ib > ia and pos >= ia - 1 and pos < ib - 1 then want, label = "intro", "SKIP INTRO" end
    if not want and oa >= 0 and ob > oa and pos >= oa - 1 and pos < ob - 1 then want, label = "outro", "SKIP CREDITS" end
    if not want then
        if shown then
            shown = nil
            overlay:remove()
            if bound then mp.remove_key_binding("anic3d-click") bound = false end
        end
        return
    end
    which = want
    shown = want
    if not bound then mp.add_forced_key_binding("MBTN_LEFT", "anic3d-click", click, {complex = true}) bound = true end
    local hot = mouse_in()
    local fill = hot and "FFFFFF" or "202020"
    local text = hot and "000000" or "FFFFFF"
    overlay.data = string.format(
        "{\\an7\\pos(0,0)\\1c&H%s&\\1a&H20&\\3c&HFFFFFF&\\bord2\\p1}m %d %d l %d %d %d %d %d %d{\\p0}\n" ..
        "{\\an5\\pos(%d,%d)\\1c&H%s&\\bord0\\fs30\\b1}%s  >>",
        fill, X0, Y0, X1, Y0, X1, Y1, X0, Y1, (X0 + X1) / 2, (Y0 + Y1) / 2, text, label)
    overlay:update()
end

mp.observe_property("time-pos", "number", function(_, v) pos = v or 0 draw() end)
mp.observe_property("duration", "number", function(_, v) dur = v or 0 end)
mp.observe_property("mouse-pos", "native", function() if shown then draw() end end)
mp.register_script_message("anic3d-range", function(a, b, c, d)
    ia, ib, oa, ob = tonumber(a) or -1, tonumber(b) or -1, tonumber(c) or -1, tonumber(d) or -1
    draw()
end)
)LUA";

// Intro / outro ranges: the stream's own timestamps, else mpv chapters called OP/ED, else none (the buttons then use fixed jumps).
json Engine::skip_ranges() {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    std::map<std::string, std::pair<double, double>> found = skip_;
    static const re::Regex OP(R"(\b(op|opening|intro|prologue)\b)");
    static const re::Regex ED(R"(\b(ed|ending|outro|credits?|epilogue)\b)");
    if (state_.contains("chapter-list") && state_["chapter-list"].is_array()) {
        const json& chs = state_["chapter-list"];
        for (size_t i = 0; i < chs.size(); i++) {
            const json& ch = chs[i];
            std::string title = su::lower(ch.contains("title") && ch["title"].is_string() ? ch["title"].get<std::string>() : "");
            std::string kind = OP.search(title) ? "intro" : ED.search(title) ? "outro" : "";
            if (!kind.empty() && !found.count(kind)) {
                double t0 = ch.value("time", 0.0);
                double end = i + 1 < chs.size() ? chs[i + 1].value("time", 0.0) : (num("duration") ? num("duration") : t0);
                found[kind] = {t0, end};
            }
        }
    }
    double dur = num("duration");
    json out = json::object();
    for (auto& [k, v] : found) {
        if (k == "intro" && dur > 300 && v.first > dur * 0.5) continue;  // an "opening" in the second half is a mislabelled ending
        out[k] = {v.first, v.second};
    }
    return out;
}

// Send the player state. Rapid updates are throttled, but the last one is always flushed.
void Engine::emit(bool force) {
    double now = mono();
    double wait;
    {
        std::lock_guard<std::recursive_mutex> lk(mu_);
        wait = 0.4 - (now - last_emit_);
        if (!(force || wait <= 0)) {
            if (flush_pending_) return;
            flush_pending_ = true;
            std::thread([this, wait] {
                std::this_thread::sleep_for(std::chrono::duration<double>(wait));
                {
                    std::lock_guard<std::recursive_mutex> lk2(mu_);
                    flush_pending_ = false;
                }
                emit(true);
            }).detach();
            return;
        }
        last_emit_ = now;
    }
    if (send) send({{"event", "player"}, {"state", snapshot()}});
}

json Engine::handle(const std::string& cmd, const json& a) {
    auto S = [&](const char* k) { return a.contains(k) && a[k].is_string() ? a[k].get<std::string>() : std::string(); };
    if (cmd == "search") {
        std::string out = core::backend({"search", S("q")});
        json rows = json::array();
        int k = 0;
        for (auto& line : su::splitlines(out)) {
            if (line.find('\t') == std::string::npos) continue;
            auto p = su::split(line, "\t");
            while (p.size() < 3) p.push_back("");
            rows.push_back({{"id", p[0]}, {"title", p[1]}, {"poster", p[2]}});
            if (k++ < 4) {  // warm the cache so clicking one of the top results is instant
                std::string id = p[0], title = p[1];
                std::thread([this, id, title] { prefetch(id, title); }).detach();
            }
        }
        return rows;
    }
    if (cmd == "episodes") {
        std::string aid = S("id");
        if (su::starts_with(aid, "q:")) aid = multisrc::resolve(S("title"));  // a recommendation: only a title so far
        std::string out = core::backend({"episodes", aid, S("title")});
        auto eps = su::split_ws(out);
        core::total_set(aid, (int)eps.size());
        {
            std::lock_guard<std::recursive_mutex> lk(mu_);
            anime_ = {aid, S("title")};
            episodes_ = eps;
        }
        auto watched = core::db_watched(aid, S("title"));
        return {{"id", aid}, {"episodes", eps}, {"watched", std::vector<std::string>(watched.begin(), watched.end())}};
    }
    if (cmd == "home") return core::home_data();
    if (cmd == "info") {
        json found = core::find_manga(S("title"));
        json manga = json::array();
        for (auto& f : found) {
            std::string id = f[0].get<std::string>();
            manga.push_back({{"id", id}, {"title", f[1]}, {"chapters", f[2]}, {"url", core::MANGA_SITE + "/manga/" + id}});
        }
        return {{"manga", manga}};
    }
    if (cmd == "blurb") return core::media_blurb(S("title"), a.contains("kind") ? S("kind") : "anime");
    if (cmd == "related") return core::related_content(S("title"));
    if (cmd == "edits") return core::fan_edits(S("title"));
    if (cmd == "history") {
        json out = json::array();
        db::Conn con;
        for (auto& r : con.query("SELECT COALESCE(anime_id,''), anime_name, group_concat(DISTINCT episode), max(timestamp) "
                                 "FROM history GROUP BY COALESCE(anime_id, anime_name) ORDER BY max(timestamp) DESC LIMIT 100")) {
            out.push_back({{"id", r.str(0)}, {"title", r.str(1)}, {"episodes", su::split(r.str(2), ",")}, {"last", r.str(3)}});
        }
        return out;
    }
    if (cmd == "state") return snapshot();
    if (cmd == "play") {
        double start = a.contains("start") && a["start"].is_number() ? a["start"].get<double>() : 0;
        std::string ep = a.contains("ep") ? (a["ep"].is_string() ? a["ep"].get<std::string>() : a["ep"].dump()) : "";
        {
            std::lock_guard<std::recursive_mutex> lk(mu_);
            if (!S("mode").empty()) mode_ = S("mode");
            if (!S("poster").empty()) poster_ = S("poster");
            if (!S("id").empty() && (anime_.first.empty() || anime_.first != S("id"))) {
                anime_ = {S("id"), S("title")};
                if (a.contains("episodes") && a["episodes"].is_array() && !a["episodes"].empty()) episodes_ = a["episodes"].get<std::vector<std::string>>();
            }
        }
        play(ep, start);
        return snapshot();
    }
    if (cmd == "control") return control(S("action"), a.contains("value") ? a["value"] : json(nullptr));
    throw std::invalid_argument("unknown command: " + cmd);
}

void Engine::prefetch(const std::string& aid, const std::string& title) {
    try {
        core::backend({"episodes", aid});
        core::find_manga(title);
    } catch (...) {
    }
}

json Engine::control(const std::string& action, const json& v) {
    auto vnum = [&](double d = 0) { return v.is_number() ? v.get<double>() : d; };
    auto truthy = [&]() { return v.is_boolean() ? v.get<bool>() : v.is_number() ? v.get<double>() != 0 : !v.is_null(); };
    if (action == "pause") mpv({"cycle", "pause"});
    else if (action == "seek") mpv({"seek", vnum(), "relative"});
    else if (action == "seekpct") mpv({"seek", vnum(), "absolute-percent"});
    else if (action == "volume") mpv({"set_property", "volume", vnum(100)});
    else if (action == "mute") mpv({"cycle", "mute"});
    else if (action == "autoskip") {
        std::lock_guard<std::recursive_mutex> lk(mu_);
        autoskip_["intro"] = v.is_object() && v.value("intro", false);
        autoskip_["outro"] = v.is_object() && v.value("outro", false);
        skipped_.clear();
    } else if (action == "autofs") {
        bool on = truthy();
        bool playing;
        {
            std::lock_guard<std::recursive_mutex> lk(mu_);
            autofs_ = on;
            playing = proc_ != nullptr;
        }
        if (on && playing) mpv({"set_property", "fullscreen", true});
    } else if (action == "fullscreen") mpv({"cycle", "fullscreen"});
    else if (action == "subs") mpv({"cycle", "sub-visibility"});
    else if (action == "speed") mpv({"set_property", "speed", vnum(1.0)});
    else if (action == "autonext") {
        std::lock_guard<std::recursive_mutex> lk(mu_);
        autonext_ = truthy();
    } else if (action == "stop") {
        std::lock_guard<std::recursive_mutex> lk(mu_);
        token_++;
        kill();
        state_ = json::object();
        cur_ep_.clear();
    } else if (action == "next" || action == "prev") {
        std::string target;
        {
            std::lock_guard<std::recursive_mutex> lk(mu_);
            int i = ep_index(cur_ep_);
            if (i >= 0) {
                i += action == "next" ? 1 : -1;
                if (i >= 0 && i < (int)episodes_.size()) target = episodes_[i];
            }
        }
        if (!target.empty()) play(target);
    } else if (action == "skip_intro") {
        json rng = skip_ranges();
        double pos = num("time-pos");
        if (rng.contains("intro") && pos < rng["intro"][1].get<double>() - 1) mpv({"seek", std::max(rng["intro"][1].get<double>(), pos), "absolute"});
        else mpv({"seek", 85, "relative"});  // a typical opening is ~90 s long
    } else if (action == "skip_outro") {
        json rng = skip_ranges();
        double pos = num("time-pos"), dur = num("duration");
        std::string next;
        {
            std::lock_guard<std::recursive_mutex> lk(mu_);
            int i = ep_index(cur_ep_);
            if (i >= 0 && i + 1 < (int)episodes_.size()) next = episodes_[i + 1];
        }
        bool has = rng.contains("outro");
        double o0 = has ? rng["outro"][0].get<double>() : 0, o1 = has ? rng["outro"][1].get<double>() : 0;
        if (has && pos < o0 - 1) mpv({"seek", o0, "absolute"});  // jump to where the credits start
        else if (has && pos < o1 - 1 && o1 < dur - 3) mpv({"seek", o1, "absolute"});  // past the credits (a preview comes after)
        else if (!next.empty()) play(next);  // no timestamps: the next episode
        else if (dur) mpv({"seek", std::max(dur - 2, 0.0), "absolute"});
    } else if (action == "mode") {
        std::string ep;
        double pos;
        bool playing;
        {
            std::lock_guard<std::recursive_mutex> lk(mu_);
            mode_ = v.is_string() && !v.get<std::string>().empty() ? v.get<std::string>() : (mode_ == "sub" ? "dub" : "sub");
            ep = cur_ep_;
            pos = num("time-pos");
            playing = proc_ != nullptr;
        }
        if (playing && !ep.empty()) play(ep, pos);
    } else {
        throw std::invalid_argument("unknown control: " + action);
    }
    emit(true);
    return snapshot();
}

void Engine::play(const std::string& ep, double start) {
    std::string aid, title, mode;
    long long token;
    {
        std::lock_guard<std::recursive_mutex> lk(mu_);
        aid = anime_.first;
        title = anime_.second;
        mode = mode_;
        token = ++token_;
        kill();
    }
    std::string out = core::backend({"link", aid, ep, mode, title});
    if (token != token_) return;
    auto parts = su::split(out, "\n");
    while (parts.size() < 5) parts.push_back("");
    std::string video = parts[0], refr = parts[1], sub = parts[2], skip = parts[3];
    std::string mpv_path = paths::ensure_mpv();  // on Windows the first run unpacks the bundled mpv
    std::lock_guard<std::recursive_mutex> lk(mu_);
    if (token != token_) return;
    source_ = parts[4];
    skip_.clear();
    try {
        json sk = json::parse(skip.empty() ? "{}" : skip);
        for (auto& [k, val] : sk.items()) skip_[k] = {val[0].get<double>(), val[1].get<double>()};
    } catch (...) {
        skip_.clear();
    }
    std::vector<std::string> args = {mpv_path, "--no-terminal", "--input-ipc-server=" + sock_, "--referrer=" + refr,
                                     "--force-media-title=" + title + " Episode " + ep, "--keep-open=no"};
    bool hls = su::contains(video, "m3u8");
    if (hls) {  // some hosts serve playlists under odd names ("...?t.m3u8"), which ffmpeg won't sniff as HLS
        args.push_back("--demuxer-lavf-format=hls");
        args.push_back("--demuxer-lavf-o=allowed_extensions=ALL,allowed_segment_extensions=ALL,extension_picky=0");
    }
    {  // the on-screen SKIP INTRO button is a small Lua script written next to the cache
        std::string lua = paths::join(paths::cache_dir(), "anic3d-skip.lua");
        paths::makedirs(paths::cache_dir());
        paths::write_file(lua, SKIP_LUA);
        args.push_back("--script=" + lua);
    }
    if (autofs_) args.push_back("--fullscreen");
    for (const char* env : {"ANIC3D_MPV_ARGS", "ANI3D_MPV_ARGS"})  // extra mpv options (used by the headless self-tests)
        for (auto& x : su::split_ws(paths::getenv_or(env, ""))) args.push_back(x);
    late_sub_.clear();
    last_ranges_.clear();
    if (!sub.empty() && hls) late_sub_ = sub;  // the forced hls format would also apply to --sub-file, so mpv could not read the .vtt
    else if (!sub.empty()) args.push_back("--sub-file=" + sub);
    if (start > 0) args.push_back("--start=" + std::to_string((long long)start));
    args.push_back(video);
    if (!paths::WINDOWS) std::remove(sock_.c_str());
    eof_ = false;
    state_ = json::object();
    cur_ep_ = ep;
    skipped_.clear();
    auto proc = std::shared_ptr<mpvlink::Proc>(mpvlink::Proc::spawn(args).release());
    if (!proc) throw std::runtime_error("could not start mpv (" + mpv_path + ")");
    proc_ = proc;
    core::db_log(aid, title, ep);
    auto watched = core::db_watched(aid, title);
    if (send) send({{"event", "watched"}, {"anime_id", aid}, {"episodes", std::vector<std::string>(watched.begin(), watched.end())}});
    emit(true);
    std::thread([this, proc, token] { watch(proc, token); }).detach();
}

void Engine::watch(std::shared_ptr<mpvlink::Proc> proc, long long token) {
    std::shared_ptr<mpvlink::Link> link(mpvlink::Link::connect(sock_, proc.get()).release());
    {
        std::lock_guard<std::recursive_mutex> lk(mu_);
        if (token == token_) link_ = link;
    }
    if (link) {
        const char* props[] = {"time-pos", "duration", "pause", "volume", "mute", "speed", "chapter-list"};
        for (int i = 0; i < 7; i++) link->send(json({{"command", {"observe_property", i + 1, props[i]}}}).dump() + "\n");
        // keys inside the mpv window: K skips the opening, Shift+K the ending (same logic as the SKIP buttons)
        link->send(json({{"command", {"keybind", "k", "script-message anic3d-skip intro"}}}).dump() + "\n");
        link->send(json({{"command", {"keybind", "K", "script-message anic3d-skip outro"}}}).dump() + "\n");
        std::string line;
        while (link->readline(line)) {
            json m;
            try {
                m = json::parse(line);
            } catch (...) {
                continue;
            }
            std::string ev = m.value("event", "");
            if (ev == "property-change") {
                std::string name = m.value("name", "");
                bool current;
                {
                    std::lock_guard<std::recursive_mutex> lk(mu_);
                    current = token == token_;
                    if (current) state_[name] = m.contains("data") ? m["data"] : json(nullptr);
                }
                if (!current) continue;
                emit();
                if (name == "time-pos" && wall() - resume_at_ > 5) {
                    resume_at_ = wall();
                    save_resume();
                }
                if (name == "time-pos" || name == "chapter-list") auto_skip();
                if (name == "time-pos" || name == "chapter-list") send_ranges(link);
                if (name == "duration" && m.contains("data") && !m["data"].is_null()) {
                    bool lookup = false;
                    std::string a_title, a_ep;
                    {
                        std::lock_guard<std::recursive_mutex> lk(mu_);
                        if (aniskip_tok_ != token) {  // once per playback: fill in the timestamps the stream didn't carry
                            aniskip_tok_ = token;
                            lookup = !skip_.count("intro") || !skip_.count("outro");
                            a_title = anime_.second;
                            a_ep = cur_ep_;
                        }
                    }
                    if (lookup)
                        std::thread([this, link, token, a_title, a_ep] {
                            json found;
                            try {
                                found = core::aniskip(a_title, a_ep);
                            } catch (...) {
                                return;
                            }
                            {
                                std::lock_guard<std::recursive_mutex> lk(mu_);
                                if (token != token_) return;
                                for (auto& [k, v] : found.items())
                                    if (!skip_.count(k) && v.is_array() && v.size() == 2) skip_[k] = {v[0].get<double>(), v[1].get<double>()};
                                last_ranges_.clear();
                            }
                            send_ranges(link);
                            emit(true);
                        }).detach();
                    std::string sub;
                    {
                        std::lock_guard<std::recursive_mutex> lk(mu_);
                        sub.swap(late_sub_);
                    }
                    if (!sub.empty()) {
                        link->send(json({{"command", {"set_property", "demuxer-lavf-format", ""}}}).dump() + "\n");
                        link->send(json({{"command", {"sub-add", sub, "select"}}}).dump() + "\n");
                    }
                }
            } else if (ev == "client-message" && m.contains("args") && m["args"].is_array() && m["args"].size() == 2 &&
                       m["args"][0] == "anic3d-skip") {
                std::string which = m["args"][1] == "outro" ? "skip_outro" : "skip_intro";
                bool current;
                {
                    std::lock_guard<std::recursive_mutex> lk(mu_);
                    current = token == token_;
                }
                if (current) std::thread([this, which] { try { control(which, json(nullptr)); } catch (...) {} }).detach();
            } else if (ev == "end-file" && m.value("reason", "") == "eof") {
                std::lock_guard<std::recursive_mutex> lk(mu_);
                eof_ = true;
                if (token == token_) save_resume(true);
            }
        }
    }
    proc->wait();
    std::string next;
    {
        std::lock_guard<std::recursive_mutex> lk(mu_);
        if (token != token_) return;
        proc_.reset();
        link_.reset();
        state_ = json::object();
    }
    emit(true);
    {
        std::lock_guard<std::recursive_mutex> lk(mu_);
        int i = ep_index(cur_ep_);
        if (eof_ && autonext_ && i >= 0 && i + 1 < (int)episodes_.size()) next = episodes_[i + 1];
    }
    if (!next.empty()) {
        try {
            play(next);
        } catch (const std::exception& e) {
            if (send) send({{"event", "error"}, {"message", e.what()}});
        }
    }
}

// Tell the on-screen button script where the intro / credits are (about once a second, and whenever they change)
void Engine::send_ranges(const std::shared_ptr<mpvlink::Link>& link) {
    json rng = skip_ranges();
    auto get = [&](const char* k, int i) { return rng.contains(k) ? rng[k][i].get<double>() : -1.0; };
    std::string key = std::to_string(get("intro", 0)) + "," + std::to_string(get("intro", 1)) + "," + std::to_string(get("outro", 0)) + "," +
                      std::to_string(get("outro", 1));
    if (key == last_ranges_ && wall() - ranges_at_ < 5) return;
    last_ranges_ = key;
    ranges_at_ = wall();
    link->send(json({{"command", {"script-message", "anic3d-range", std::to_string(get("intro", 0)), std::to_string(get("intro", 1)),
                                  std::to_string(get("outro", 0)), std::to_string(get("outro", 1))}}}).dump() + "\n");
}

// Honour the intro / credits auto-skip switches for the episode that is playing (once each).
void Engine::auto_skip() {
    std::string next_play;
    {
        std::lock_guard<std::recursive_mutex> lk(mu_);
        if (!state_.contains("time-pos") || !state_["time-pos"].is_number() || !(autoskip_["intro"] || autoskip_["outro"])) return;
        double pos = state_["time-pos"].get<double>();
        json rng = skip_ranges();
        double dur = num("duration");
        std::string ep = cur_ep_;
        if (autoskip_["intro"] && !skipped_.count({ep, "intro"})) {
            if (rng.contains("intro")) {
                double a = rng["intro"][0].get<double>(), b = rng["intro"][1].get<double>();
                if (a - 1 <= pos && pos < b - 1) {
                    skipped_.insert({ep, "intro"});
                    mpv({"seek", b, "absolute"});
                    return;
                }
            } else if (2 <= pos && pos < 30) {  // no timestamps: jump a typical opening length once
                skipped_.insert({ep, "intro"});
                mpv({"seek", 85, "relative"});
                return;
            }
        }
        if (autoskip_["outro"] && rng.contains("outro") && !skipped_.count({ep, "outro"})) {
            double a = rng["outro"][0].get<double>(), b = rng["outro"][1].get<double>();
            if (a - 0.5 <= pos && pos < b) {
                skipped_.insert({ep, "outro"});
                int i = ep_index(ep);
                if (b >= dur - 5 && i >= 0 && i + 1 < (int)episodes_.size() && autonext_) next_play = episodes_[i + 1];  // credits run to the end
                else if (b < dur - 5) mpv({"seek", b, "absolute"});
                else mpv({"seek", std::max(dur - 1, 0.0), "absolute"});
            }
        }
    }
    if (!next_play.empty())
        std::thread([this, next_play] {
            try {
                play(next_play);
            } catch (const std::exception& e) {
                if (send) send({{"event", "error"}, {"message", e.what()}});
            }
        }).detach();
}

// Write the playing episode's position to the resume table; a (nearly) finished episode moves on to the next one.
void Engine::save_resume(bool finished) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    if (anime_.first.empty() || cur_ep_.empty()) return;
    double pos = num("time-pos"), dur = num("duration");
    if (finished || (dur && pos / dur > 0.92)) {
        std::string nxt;
        int i = ep_index(cur_ep_);
        if (i >= 0 && i + 1 < (int)episodes_.size()) nxt = episodes_[i + 1];
        core::resume_advance(anime_.first, anime_.second, nxt);
    } else if (pos > 5) {
        core::resume_save(anime_.first, anime_.second, cur_ep_, pos, dur, poster_);
    }
}

void Engine::kill() {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    if (proc_) save_resume();
    auto proc = proc_;
    proc_.reset();
    link_.reset();
    if (proc && proc->running()) proc->terminate();
}

void Engine::mpv(const json& cmd) {
    std::shared_ptr<mpvlink::Link> l;
    {
        std::lock_guard<std::recursive_mutex> lk(mu_);
        l = link_;
    }
    if (l) l->send(json({{"command", cmd}}).dump() + "\n");
}
