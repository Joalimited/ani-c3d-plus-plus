#include "app.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <future>

#include "../core/core.hpp"
#include "../core/multisrc.hpp"
#include "../core/sections.hpp"
#include "../util/paths.hpp"
#include "../util/pdf.hpp"
#include "../util/strutil.hpp"
#include "../world/effects.hpp"

using json = nlohmann::json;

namespace {
std::string S(const json& j, const char* k) { return j.is_object() && j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : ""; }
std::string cut(const std::string& s, size_t n) { return su::utf8_substr(s, 0, n); }
std::shared_ptr<const Image> decode_thumb(const std::string& path, int w, int h) { return std::make_shared<const Image>(img::open_thumb(path, w, h)); }
std::string today() {
    std::time_t now = std::time(nullptr);
    char buf[16];
    std::strftime(buf, sizeof buf, "%Y-%m-%d", std::localtime(&now));
    return buf;
}

// HISTORY / library rows have no poster: find it through the (cached) search; ids differ between sites, so fall back
// to the same show by title
std::string poster_for_sync(const std::string& title, const std::string& id) {
    auto lines = su::splitlines(core::backend({"search", su::replace(title, " ", "+")}));
    for (auto& ln : lines) {
        auto p = su::split(ln, "\t");
        if (p.size() >= 3 && p[0] == id) return p[2];
    }
    for (auto& ln : lines) {
        auto p = su::split(ln, "\t");
        if (p.size() >= 3 && multisrc::same_show(p[1], title)) return p[2];
    }
    return "";
}
}  // namespace

App::App(Renderer& r_, double ratio_) : r(r_), ratio(ratio_), text(r_, ratio_), tasks_(24), engine(std::make_shared<Engine>()) {
    gpu_label = "OPENGL";
    tasks_.on_error = [this](const std::string& e) { status = "UI error: " + e; };
    engine->send = [this](const json& ev) { tasks_.post([this, ev] { on_engine_event(ev); }); };
    json cfg = themes::load_settings();
    call("control", {{"action", "autofs"}, {"value", cfg.value("auto_fullscreen", false)}});
    auto_intro = cfg.value("auto_skip_intro", false);
    auto_outro = cfg.value("auto_skip_outro", false);
    call("control", {{"action", "autoskip"}, {"value", {{"intro", auto_intro}, {"outro", auto_outro}}}});
    lib_in = TextInput("", 60);
    // manga read right to left by default (the older "manga_rtl" key was written when left-to-right was the default)
    manga_rtl = cfg.value("manga_rtl_v2", true);
    manga_jp = cfg.value("manga_jp", false);  // shelf titles: English (default) or Japanese (romaji)
    auto_fs = cfg.value("auto_fullscreen", false);
    if (cfg.contains("librarian_recent") && cfg["librarian_recent"].is_array())
        for (auto& r2 : cfg["librarian_recent"])
            if (r2.is_string() && recent.size() < 8) recent.push_back(r2.get<std::string>());
    adv.refresh();  // the first pass records what you had already earned, silently
    for (auto& w : core::wl_list()) starred.insert(S(w, "id"));
    scene.starred = &starred;
    std::string th = paths::getenv_or("ANIC3D_THEME", paths::getenv_or("ANI3D_THEME", themes::load_theme()));
    theme_ = &themes::get(th);
    scene.dist_scale = (theme_->fx == "jurassic" || theme_->fx == "halloween" || theme_->fx == "forest") ? 1.35 : 1.0;
}

App::~App() {
    engine->send = nullptr;
    try {
        engine->handle("control", {{"action", "stop"}});
    } catch (...) {
    }
}

// ---------------------------------------------------------------- engine glue
void App::call(const std::string& cmd, const json& args, Tasks::Cb cb) {
    auto eng = engine;
    tasks_.run([eng, cmd, args] { return eng->handle(cmd, args); }, std::move(cb));
}

// wrap a result callback so a spinner with `msg` shows (after a short delay) until the work finishes
Tasks::Cb App::tracked(const std::string& msg, Tasks::Cb cb) {
    int tok = ++busy_seq;
    busy[tok] = {msg, t};
    return [this, tok, cb](const json& r2, const std::string& e) {
        busy.erase(tok);
        if (cb) cb(r2, e);
    };
}

void App::pump() { tasks_.drain(); }

void App::on_engine_event(const json& ev) {
    std::string kind = S(ev, "event");
    if (kind == "player") {
        player = ev["state"];
        audio = player.value("mode", audio);
    } else if (kind == "watched") {
        std::string aid = S(ev, "anime_id");
        std::vector<std::string> eps;
        for (auto& e : ev["episodes"]) eps.push_back(e.get<std::string>());
        bool in_lib = false;
        if (lib)
            for (auto& b : lib->books)
                if (b->id == aid) {
                    in_lib = true;
                    b->episodes = eps;
                    b->watched = (int)std::set<std::string>(eps.begin(), eps.end()).size();
                }
        if (!in_lib && scene.field && scene.peds[scene.field->idx].id == aid) scene.field->watched = std::set<std::string>(eps.begin(), eps.end());
    } else if (kind == "error") {
        status = S(ev, "message");
    }
    if (kind == "watched") check_adv();
}

// ---------------------------------------------------------------- small queries
std::vector<std::string> App::library_titles() const {
    std::vector<std::string> out;
    if (lib)
        for (auto& b : lib->books) out.push_back(b->title);
    return out;
}

// true when the theme has interactive animals and the world view is showing
bool App::jurassic() const { return (theme_->fx == "jurassic" || theme_->fx == "halloween" || theme_->fx == "forest") && mode == "world"; }

// library: drop the theme's moving things (creatures, petals, leaves, rain, shadows) between the camera and the bookcase
void App::keep_clear(std::vector<Box>& fx, size_t n_q3d, const V3& eye) {
    double x0 = -libc_::SHELF_W / 2 - 4.0, x1 = libc_::SHELF_W / 2 + 4.0;
    double z0 = -3.0, z1 = eye.z + 6.0;
    double top = lib->row_y.back() + libc_::BOOK_H + 6.0;
    auto inside = [&](double x, double y, double z) { return x > x0 && x < x1 && z > z0 && z < z1 && y < top; };
    fx.erase(std::remove_if(fx.begin(), fx.end(), [&](const Box& b) { return inside(b.v[0], b.v[1], b.v[2]); }), fx.end());
    std::vector<Quad> kept(r.q3d.begin(), r.q3d.begin() + (long)std::min(n_q3d, r.q3d.size()));
    for (size_t i = n_q3d; i < r.q3d.size(); i++) {  // billboards and contact shadows: find each quad's world position from its matrix
        const float* m = r.q3d[i].mvp;
        auto p = r.inv_viewproj.mul(m[12], m[13], m[14], m[15]);
        if (std::fabs(p[3]) > 1e-9 && inside(p[0] / p[3], p[1] / p[3], p[2] / p[3])) continue;
        kept.push_back(r.q3d[i]);
    }
    r.q3d = std::move(kept);
}

// true while a book is open in the library: the world, its creatures and effects step aside so you can read
bool App::reading_focus() const { return mode == "library" && lib && lib->pick && lib->pick->u >= 0.9 && !lib->pick->closing; }

Creatures& App::herd() {
    Creatures* c = effects::creatures(*theme_);
    return c ? *c : effects::herd();
}

// where the mouse ray meets the ground (y = 0)
std::optional<std::pair<double, double>> App::floor_point() {
    auto [o, d] = mouse_ray();
    if (d.y > -1e-4) return std::nullopt;
    double tt = -o.y / d.y;
    return std::make_pair(o.x + d.x * tt, o.z + d.z * tt);
}

// click a creature to poke it, or click the ground to drop food; true if the click was used
bool App::click_dino() {
    Creatures& h = herd();
    auto [o, d] = mouse_ray();
    auto i = h.pick(o, d);
    if (i) {
        Beast& b = h.poke(*i, t);
        dino_card = std::make_pair(*i, t);
        std::string verb = !b.verb.empty() ? b.verb : (b.flyer ? "screeches" : "roars");
        status = "The " + b.name + " " + verb + "!  (V to follow it)";
        return true;
    }
    auto pt = floor_point();
    if (!pt || std::hypot(pt->first, pt->second) > 260) return false;
    Beast* b = h.feed(pt->first, pt->second, t);
    std::string what = theme_->fx == "halloween" ? "candy" : theme_->fx == "forest" ? "berries" : "food";
    status = b ? "You dropped some " + what + " - a " + b->name + " is coming for it" : "Nothing wants that " + what + " right now";
    return true;
}

void App::toggle_follow() {
    Creatures& h = herd();
    std::optional<int> i = dino_hover ? dino_hover : (dino_card ? std::optional<int>(dino_card->first) : std::nullopt);
    if (follow) {
        follow.reset();
        scene.goto_home();
        status = "Stopped following";
    } else if (i) {
        follow = i;
        status = "Following the " + h.dinos[*i].name + " (V or Esc to stop)";
    } else {
        status = "Hover or click a creature first";
    }
}

// ---------------------------------------------------------------- search / home / history
void App::search(const std::string& q_in_) {
    std::string q = su::strip(q_in_.empty() ? q_in.text() : q_in_);
    if (q.empty()) return;
    suggest.clear();
    sugg_sel = -1;
    sugg_for = q;
    history_mode = false;
    view = "search";
    home_started = true;
    status = "Searching '" + q + "'...";
    call("search", {{"q", q}}, tracked("Searching '" + q + "'", [this](const json& rows, const std::string& err) {
             if (!err.empty()) {
                 status = "Search failed: " + err;
                 return;
             }
             std::vector<json> top;
             for (size_t i = 0; i < rows.size() && i < 30; i++) top.push_back(rows[i]);
             status = !rows.empty() ? std::to_string(rows.size()) + " results - click a pedestal" : "Nothing found on hianime or miruro - try another spelling";
             auto groups = sections::group_search(top);
             show_rows(top, "SEARCH", &groups);
         }));
}

// HOME: the shows you are partway through, what to watch next, and what is popular
void App::show_home() {
    history_mode = false;
    view = "home";
    home_started = true;
    follow.reset();
    status = "Loading your home...";
    int gen = ++home_gen;
    call("home", json::object(), tracked("Loading home", [this, gen](const json& r2, const std::string& err) {
             if (gen != home_gen || view != "home") return;  // a search / history view replaced it while this was loading
             if (!err.empty() || r2.is_null()) {
                 status = "Home failed: " + err;
                 return;
             }
             std::vector<json> rows;
             std::vector<std::pair<std::string, std::vector<json>>> groups;
             std::set<std::string> seen;
             auto add = [&](const std::string& label, const json& items, std::function<json(const json&)> make) {
                 std::vector<json> sect;
                 for (auto& it : items) {
                     json row = make(it);
                     std::string key = multisrc::akey(S(row, "title"));
                     if (seen.count(key)) continue;
                     seen.insert(key);
                     sect.push_back(row);
                 }
                 if (!sect.empty()) {
                     groups.emplace_back(label, sect);
                     rows.insert(rows.end(), sect.begin(), sect.end());
                 }
             };
             add("CONTINUE WATCHING", r2["continue"], [](const json& c) {
                 double pos = c.value("pos", 0.0);
                 std::string where = pos > 10 ? " - " + su::fmt_time(pos) : "";
                 return json{{"id", c["id"]}, {"title", c["title"]}, {"poster", c["poster"]}, {"sub", "EP " + S(c, "ep") + where}, {"resume", {{"ep", c["ep"]}, {"pos", pos}}}};
             });
             add(r2.value("personal", false) ? "RECOMMENDED FOR YOU" : "TOP RATED", r2["recommended"],
                 [](const json& x) { return json{{"id", "q:" + S(x, "title")}, {"title", x["title"]}, {"poster", x["poster"]}}; });
             add("POPULAR", r2["popular"], [](const json& x) { return json{{"id", x["id"]}, {"title", x["title"]}, {"poster", x["poster"]}}; });
             add("TOP AIRING", r2["airing"], [](const json& x) { return json{{"id", x["id"]}, {"title", x["title"]}, {"poster", x["poster"]}}; });
             size_t n = r2["continue"].size();
             status = n ? std::to_string(n) + " show" + (n != 1 ? "s" : "") + " to continue - click one to pick up where you left off" : "Pick something popular, or search with /";
             show_rows(rows, "HOME", &groups);
         }));
}

void App::show_rows(const std::vector<json>& rows, const std::string& heading, const std::vector<std::pair<std::string, std::vector<json>>>* groups) {
    seq++;
    int gen = ++rows_gen;
    scene.set_results(rows, t, heading, groups);
    info_manga = json::array();
    info_related = nullptr;
    for (int i = 0; i < (int)scene.peds.size(); i++) {
        const Ped& p = scene.peds[i];
        load_poster(gen, i);
        if (!su::starts_with(p.id, "q:")) {  // recommendations are only titles until opened
            std::string id = p.id;
            tasks_.run([id] { return json((int)su::split_ws(core::backend({"episodes", id})).size()); },
                       [this, gen, i](const json& n, const std::string& e) {
                           if (e.empty() && gen == rows_gen && i < (int)scene.peds.size()) scene.peds[i].eps = n.get<int>();
                       });
        }
        if (p.poster_url.empty() && !p.id.empty())
            poster_for(p.title, p.id, [this, gen, i](const std::string& url) {
                if (gen == rows_gen && i < (int)scene.peds.size() && !url.empty()) {
                    scene.peds[i].poster_url = url;
                    load_poster(gen, i);
                }
            });
    }
}

void App::poster_for(const std::string& title, const std::string& id, std::function<void(const std::string&)> cb) {
    tasks_.run([title, id] { return json(poster_for_sync(title, id)); },
               [cb](const json& r2, const std::string& e) {
                   if (e.empty() && r2.is_string()) cb(r2.get<std::string>());
               });
}

void App::load_poster(int gen, int idx) {
    if (gen != rows_gen || idx >= (int)scene.peds.size()) return;
    std::string url = scene.peds[idx].poster_url;
    if (url.empty()) return;
    auto box_ = std::make_shared<std::shared_ptr<const Image>>();
    tasks_.run([url, box_] {
                   *box_ = decode_thumb(core::fetch_poster(url), 400, 600);
                   return json(true);
               },
               [this, gen, idx, box_](const json&, const std::string& e) {
                   if (e.empty() && *box_) upload_q.push_back({gen, idx, *box_});  // uploaded two per frame by drain_uploads
               });
}

void App::drain_uploads(int budget) {
    while (!upload_q.empty() && budget > 0) {
        Upload u = upload_q.front();
        upload_q.pop_front();
        if (u.gen != rows_gen || u.idx >= (int)scene.peds.size()) continue;
        scene.peds[u.idx].poster = r.make_texture(u.img->rgba.data(), u.img->w, u.img->h);
        budget--;
    }
}

// HISTORY works from anywhere: it closes the library / game / overlays and shows your watch history in the world
void App::show_history() {
    arcade_menu = adv_open = lib_typing = typing = false;
    if (mode != "world") {
        mode = "world";
        game.reset();
        lib.reset();
        game_back.clear();
    }
    follow.reset();
    history_mode = true;
    view = "history";
    home_started = true;
    status = "Loading watch history...";
    call("history", json::object(), tracked("Loading history", [this](const json& rows_in, const std::string& err) {
             if (!err.empty()) {
                 status = "History failed: " + err;
                 return;
             }
             std::vector<json> rows;
             for (auto& h : rows_in)
                 if (!S(h, "id").empty() && rows.size() < 36) rows.push_back({{"id", h["id"]}, {"title", h["title"]}, {"poster", ""}, {"last", h["last"]}});
             status = !rows.empty() ? std::to_string(rows.size()) + " anime in your watch history" : "No watch history yet - play an episode first";
             auto groups = sections::group_history(rows);
             show_rows(rows, "HISTORY", &groups);  // history has no posters: show_rows finds them through the cached search
         }));
}

// search-bar suggestions: your own titles right away, then what the sources return (cached, so Enter is instant)
void App::update_suggestions() {
    std::string q = su::strip(q_in.text());
    if (!(typing && mode == "world") || q.empty()) {
        if (q.empty()) {
            suggest.clear();
            sugg_for.clear();
        }
        return;
    }
    if (q == sugg_for || t < sugg_due) return;
    sugg_for = q;
    std::string low = su::lower(q);
    std::vector<std::string> pool = core::known_titles();
    for (auto& p : scene.peds) pool.push_back(p.title);
    std::vector<std::string> mine;
    std::set<std::string> seen;
    for (auto& tt : pool)
        if (seen.insert(tt).second && su::contains(su::lower(tt), low)) mine.push_back(tt);
    if (mine.size() > 7) mine.resize(7);
    suggest = mine;
    sugg_sel = -1;
    if (su::utf8_len(q) >= 2) {
        tasks_.run(
            [q] {
                json out = json::array();
                for (auto& ln : su::splitlines(core::backend({"search", q})))
                    if (ln.find('\t') != std::string::npos) out.push_back(su::split(ln, "\t")[1]);
                return out;
            },
            [this, q](const json& rows, const std::string& err) {
                if (!err.empty() || rows.empty() || q != sugg_for || !typing) return;
                std::vector<std::string> merged = suggest;
                for (auto& r2 : rows) merged.push_back(r2.get<std::string>());
                std::vector<std::string> uniq;
                std::set<std::string> seen2;
                for (auto& m : merged)
                    if (seen2.insert(m).second) uniq.push_back(m);
                if (uniq.size() > 8) uniq.resize(8);
                suggest = uniq;
            });
    }
}

void App::pick_suggestion(int i) {
    if (0 <= i && i < (int)suggest.size()) {
        q_in.set(suggest[i]);
        typing = false;
        suggest.clear();
        search();
    }
}

void App::load_edits() {
    if (!scene.field) {
        status = "Open an anime first";
        return;
    }
    std::string title = scene.peds[scene.field->idx].title;
    edits = "loading";
    call("edits", {{"title", title}}, [this](const json& r2, const std::string&) { edits = (r2.is_array() && !r2.empty()) ? r2 : json::array(); });
}

// evaluate advancements (after an episode / chapter / game event) and queue toasts for new ones
void App::check_adv(std::optional<advance::Toast> only) {
    std::vector<advance::Toast> fresh = only ? std::vector<advance::Toast>{*only} : adv.refresh();
    for (auto& [title, desc, group] : fresh) toasts.push_back({title, desc, group, t});
    if (!fresh.empty()) status = "Advancement made: " + std::get<0>(fresh.back());
}

void App::open_ped(int idx) {
    const Ped& p = scene.peds[idx];
    int s = ++seq;
    scene.selected = idx;
    info_manga = json::array();
    info_related = nullptr;
    edits = nullptr;
    status = "Opening " + p.title + "...";
    call("episodes", {{"id", p.id}, {"title", p.title}}, tracked("Opening " + cut(p.title, 28), [this, s, idx](const json& res, const std::string& err) {
             if (s != seq) return;
             if (!err.empty()) {
                 status = "Failed: " + err;
                 return;
             }
             Ped& ped = scene.peds[idx];
             std::vector<std::string> eps;
             for (auto& e : res["episodes"]) eps.push_back(e.get<std::string>());
             std::set<std::string> watched;
             for (auto& e : res["watched"]) watched.insert(e.get<std::string>());
             ped.eps = (int)eps.size();
             if (!S(res, "id").empty()) ped.id = S(res, "id");  // a recommendation resolved to a real site id
             scene.open_field(idx, eps, watched, t);
             status = std::to_string(eps.size()) + " episodes - click a block to play";
             if (ped.resume.is_object()) {  // CONTINUE WATCHING: start playing right where it was left
                 std::string want = S(ped.resume, "ep");
                 double pos = ped.resume.value("pos", 0.0);
                 for (auto& e : eps)
                     if (e == want || multisrc::canon(e) == multisrc::canon(want)) {
                         play(e, pos > 10 ? pos : 0);
                         break;
                     }
             }
         }));
    call("info", {{"title", p.title}}, [this, s](const json& r2, const std::string& e) {
        if (s == seq && e.empty() && r2.is_object()) info_manga = r2["manga"];
    });
}

void App::play(const std::string& ep, double start) {
    if (!scene.field) return;
    const Ped& p = scene.peds[scene.field->idx];
    status = start ? "Resuming episode " + ep + " at " + su::fmt_time(start) + "..." : "Starting episode " + ep + "...";
    call("play", {{"id", p.id}, {"title", p.title}, {"ep", ep}, {"mode", audio}, {"episodes", scene.field->eps}, {"start", start}, {"poster", p.poster_url}},
         [this, ep](const json& r2, const std::string& e) {
             if (!e.empty()) status = "Play failed: " + e;
             else status = "Playing episode " + ep + (!S(r2, "source").empty() ? " via " + S(r2, "source") : "");
         });
}

// SKIP INTRO / SKIP CREDITS: switches automatic skipping for every episode, and skips right now when turning on
void App::toggle_skip(const std::string& kind) {
    bool on;
    if (kind == "intro") on = auto_intro = !auto_intro;
    else on = auto_outro = !auto_outro;
    themes::save_settings({{"auto_skip_intro", auto_intro}, {"auto_skip_outro", auto_outro}});
    control("autoskip", {{"intro", auto_intro}, {"outro", auto_outro}});
    if (on && player.value("playing", false)) control(kind == "intro" ? "skip_intro" : "skip_outro");
    std::string what = kind == "intro" ? "intro" : "credits";
    status = on ? "Auto-skip " + what + ": ON for every episode (click again to turn off)" : "Auto-skip " + what + ": off";
}

void App::toggle_autofs() {
    auto_fs = !auto_fs;
    themes::save_settings({{"auto_fullscreen", auto_fs}});
    control("autofs", auto_fs);
    status = auto_fs ? "Auto fullscreen ON: episodes open fullscreen" : "Auto fullscreen off";
}

void App::control(const std::string& action, const json& value) {
    call("control", {{"action", action}, {"value", value}}, [this](const json&, const std::string& e) {
        if (!e.empty()) status = e;
    });
}

void App::toggle_mode() {
    audio = audio == "sub" ? "dub" : "sub";
    if (player.value("playing", false)) control("mode", audio);
    status = "Audio: " + su::upper(audio);
}

void App::load_related() {
    if (!scene.field) {
        status = "Open an anime first";
        return;
    }
    std::string title = scene.peds[scene.field->idx].title;
    info_related = "loading";
    call("related", {{"title", title}}, [this](const json& r2, const std::string&) { info_related = r2.is_object() ? r2 : json(nullptr); });
}

// ---------------------------------------------------------------- modes: arcade + library
void App::exit_mode() {
    if (mode == "game" && game_back == "library" && lib) {  // back to the shelves
        mode = "library";
        game.reset();
        game_back.clear();
        return;
    }
    mode = "world";
    game.reset();
    lib.reset();
    arcade_menu = false;
    game_back.clear();
    scene.goto_home();
}

void App::start_game(const std::string& key, const std::string& back) {
    arcade_menu = typing = false;
    game_back = (back == "library" && lib) ? back : "";
    game = make_game(key, *this);
    mode = "game";
    CamGoal c = game->cam();
    scene.cam.goto_(c.target, c.yaw, c.pitch, c.dist);
    status = game->name + ": " + game->help;
}

void App::open_library(const std::string& kind) {
    arcade_menu = typing = false;
    auto eng = engine;
    if (kind == "manga") {
        status = "Building your manga library (MangaDex)... this can take a few seconds the first time";
        tasks_.run(
            [eng] {
                // sections for the manga shelf: what you're reading, manga of your anime, and what's popular - hosted chapters only
                json reading = core::mr_list();
                if (reading.size() > 14) reading.erase(reading.begin() + 14, reading.end());
                json hist = eng->handle("history", json::object());
                std::set<std::string> seen;
                std::vector<std::string> titles;
                std::vector<std::string> cand;
                for (size_t i = 0; i < hist.size() && i < 10; i++) cand.push_back(S(hist[i], "title"));
                json wl = core::wl_list();
                for (size_t i = 0; i < wl.size() && i < 6; i++) cand.push_back(S(wl[i], "title"));
                for (auto& tt : cand) {
                    std::string key = core::norm(core::base_title(tt));
                    if (seen.insert(key).second) titles.push_back(tt);  // seasons / movies of one series map to the same manga
                }
                std::vector<std::future<json>> matches;
                for (auto& tt : titles) matches.push_back(std::async(std::launch::async, [tt] { return core::md_for_title(tt); }));
                json popular = json::array();
                try {
                    popular = core::md_popular(18);
                } catch (...) {
                }
                std::map<std::string, json> items;
                std::vector<std::string> order;
                auto add = [&](const json& it, const std::string& group) {
                    if (it.is_object() && !S(it, "id").empty() && !items.count(S(it, "id"))) {
                        std::string id = S(it, "id");
                        items[id] = {{"id", id}, {"title", it["title"]}, {"cover", S(it, "cover")}, {"group", group},
                                     {"title_en", !S(it, "title_en").empty() ? S(it, "title_en") : S(it, "title")}, {"title_jp", S(it, "title_jp")}};
                        order.push_back(id);
                    }
                };
                for (auto& rd : reading) add({{"id", rd["id"]}, {"title", rd["title"]}, {"cover", rd["cover"]}, {"title_jp", S(rd, "title_jp")}}, "reading");
                for (auto& pm : core::mp_list()) add(pm, "plan");
                for (auto& f : matches) {
                    try {
                        add(f.get(), "anime");
                    } catch (...) {
                    }
                }
                std::vector<std::future<int>> probes;  // one cheap request each
                for (auto& id : order) probes.push_back(std::async(std::launch::async, [id] { return core::probe_any(id); }));
                std::map<std::string, json> read;
                for (auto& rd : reading) read[S(rd, "id")] = rd;
                json groups = {{"reading", json::array()}, {"plan", json::array()}, {"anime", json::array()}, {"popular", json::array()}, {"done", json::array()}};
                for (size_t k = 0; k < order.size(); k++) {
                    int total = 0;
                    try {
                        total = probes[k].get();
                    } catch (...) {
                        continue;
                    }
                    if (!total) continue;
                    const std::string& mid = order[k];
                    const json& it = items[mid];
                    json done_chs = json::array();
                    if (read.count(mid))
                        for (auto& c : read[mid]["chapters"])
                            if (c.is_string() && !c.get<std::string>().empty()) done_chs.push_back(c);
                    std::set<std::string> uniq;
                    for (auto& c : done_chs) uniq.insert(c.get<std::string>());
                    json book = {{"id", mid}, {"title", it["title"]}, {"title_en", it["title_en"]}, {"title_jp", it["title_jp"]}, {"episodes", done_chs},
                                 {"total", total}, {"last", read.count(mid) ? read[mid]["last"] : json(nullptr)}, {"poster_url", it["cover"]},
                                 {"src", mid.find(':') != std::string::npos ? mid.substr(0, mid.find(':')) : "md"}};
                    if (!done_chs.empty() && (int)uniq.size() >= total) groups["done"].push_back(book);
                    else if (!done_chs.empty()) groups["reading"].push_back(book);
                    else groups[S(it, "group")].push_back(book);
                }
                json todo = json::array();  // probed in the background after the shelf shows
                for (auto& pm : popular)
                    if (!items.count(S(pm, "id")) && todo.size() < 16) todo.push_back(pm);
                return json{{"groups", groups}, {"todo", todo}};
            },
            tracked("Building the manga library", [this](const json& res, const std::string& err) {
                if (!err.empty()) {
                    status = "Library failed: " + err;
                    return;
                }
                library_ready_manga(res);
            }));
        return;
    }
    status = "Building your library from the watch history...";
    tasks_.run(
        [eng] {
            json hist_all = eng->handle("history", json::object());
            std::vector<json> hist;
            for (auto& h : hist_all)
                if (!S(h, "id").empty() && hist.size() < 48) hist.push_back(h);
            json wl_all = core::wl_list();
            std::vector<json> wl(wl_all.begin(), wl_all.begin() + (long)std::min<size_t>(30, wl_all.size()));
            auto fill = [](json& h) {
                try {
                    h["all_eps"] = su::split_ws(core::backend({"episodes", S(h, "id")}));
                } catch (...) {
                    h["all_eps"] = json::array();
                }
                h["total"] = h["all_eps"].size();
            };
            std::vector<std::future<void>> fs;
            for (auto& h : hist) fs.push_back(std::async(std::launch::async, fill, std::ref(h)));
            for (auto& w : wl) fs.push_back(std::async(std::launch::async, fill, std::ref(w)));
            for (auto& f : fs) f.get();
            return json{{"rows", hist}, {"wl", wl}};
        },
        tracked("Building the library", [this](const json& res, const std::string& err) {
            if (!err.empty()) {
                status = "Library failed: " + err;
                return;
            }
            library_ready_anime(res);
        }));
}

BookP App::book_from(const json& j) const {
    auto b = std::make_shared<Book>();
    b->id = S(j, "id");
    b->title = S(j, "title");
    b->title_en = !S(j, "title_en").empty() ? S(j, "title_en") : b->title;
    b->title_jp = S(j, "title_jp");
    if (j.contains("episodes"))
        for (auto& e : j["episodes"])
            if (e.is_string()) b->episodes.push_back(e.get<std::string>());
    b->total = j.value("total", 0);
    b->last = S(j, "last");
    b->poster_url = S(j, "poster_url");
    b->src = !S(j, "src").empty() ? S(j, "src") : "md";
    return b;
}

void App::library_ready_anime(const json& res) {
    std::vector<json> rows(res["rows"].begin(), res["rows"].end()), wl(res["wl"].begin(), res["wl"].end());
    starred.clear();
    for (auto& w : core::wl_list()) starred.insert(S(w, "id"));
    lib = std::make_unique<Library>(*this, rows, wl);
    lib_t0 = t;
    mode = "library";
    auto n = [&](const char* k) { return lib->section_info.count(k) ? lib->section_info[k].second : 0; };
    status = "Watching " + std::to_string(n("watching")) + "  -  Planning " + std::to_string(n("planning")) + "  -  Watched " + std::to_string(n("watched"));
}

void App::library_ready_manga(const json& res) {
    Library::Groups groups;
    for (auto& [k, v] : res["groups"].items())
        for (auto& bj : v) groups[k].push_back(book_from(bj));
    lib = std::make_unique<Library>(*this, std::vector<json>{}, std::vector<json>{}, "manga", groups);
    lib_t0 = t;
    mode = "library";
    auto n = [&](const char* k) { return lib->section_info.count(k) ? lib->section_info[k].second : 0; };
    status = "Reading " + std::to_string(n("reading")) + "  -  Of your anime " + std::to_string(n("anime")) + "  -  Finished " + std::to_string(n("done")) +
             "  -  loading popular...";
    fill_jp_titles();
    json todo = res["todo"];
    if (!todo.empty()) {
        tasks_.run(
            [todo] {
                std::vector<std::future<int>> probes;
                for (auto& it : todo) {
                    std::string id = S(it, "id");
                    probes.push_back(std::async(std::launch::async, [id] { return core::md_probe(id); }));
                }
                json out = json::array();
                for (size_t i = 0; i < todo.size(); i++) {
                    int n2 = 0;
                    try {
                        n2 = probes[i].get();
                    } catch (...) {
                        continue;
                    }
                    if (!n2) continue;
                    const json& it = todo[i];
                    out.push_back({{"id", it["id"]}, {"title", it["title"]}, {"title_en", !S(it, "title_en").empty() ? S(it, "title_en") : S(it, "title")},
                                   {"title_jp", S(it, "title_jp")}, {"episodes", json::array()}, {"total", n2}, {"poster_url", S(it, "cover")}});
                }
                return out;
            },
            [this](const json& books, const std::string& e) {
                if (!e.empty() || !lib || lib->kind != "manga" || books.empty()) return;
                std::vector<BookP> more;
                for (auto& bj : books) more.push_back(book_from(bj));
                lib->extend("popular", more);
                status = "Popular manga added (" + std::to_string(more.size()) + ")";
            });
    }
}

// keep the episode data we already fetched when the star list changes
std::vector<json> App::wl_with_data(std::vector<json> new_wl, const std::vector<json>& old_wl) {
    std::map<std::string, json> have;
    for (auto& w : old_wl) have[S(w, "id")] = w;
    for (auto& w : new_wl) {
        auto it = have.find(S(w, "id"));
        if (it != have.end()) {
            w["all_eps"] = it->second.value("all_eps", json::array());
            w["total"] = it->second.value("total", 0);
        }
    }
    return new_wl;
}

void App::toggle_star(const std::string& id, const std::string& title, const std::string& poster) {
    if (starred.count(id)) {
        core::wl_remove(id);
        starred.erase(id);
        status = "Removed '" + title + "' from your watch list";
    } else {
        core::wl_add(id, title, poster);
        starred.insert(id);
        status = "* Added '" + title + "' to your watch list (see Planning on the bookshelf)";
    }
}

// world view: star/unstar the hovered pedestal, else the open anime
void App::star_selected() {
    int idx = hover.kind == "ped" ? hover.i : scene.selected;
    if (idx < 0 || idx >= (int)scene.peds.size()) {
        status = "Hover or open an anime first";
        return;
    }
    const Ped& p = scene.peds[idx];
    toggle_star(p.id, p.title, p.poster_url);
}

static json book_json(const Book& b) { return {{"id", b.id}, {"title", b.title}, {"title_en", b.title_en}, {"src", b.src}}; }

// manga shelves only probe chapter counts; the full list is fetched the first time a book is opened
void App::ensure_chapters(const BookP& book, std::function<void()> then, bool quiet) {
    if (book->chapters) {
        if (then) then();
        return;
    }
    if (then) book->after.push_back(then);
    if (book->loading) return;
    book->loading = true;
    json bj = book_json(*book);
    auto done = [this, book, quiet](const json& res, const std::string& err) {
        book->loading = false;
        if (!err.empty() || res.is_null()) {
            if (!quiet) status = "Couldn't load the chapter list: " + err;
            book->after.clear();
            return;
        }
        book->chapters = res["chapters"];
        book->all_eps.clear();
        for (auto& c : res["chapters"]) book->all_eps.push_back(S(c, "chapter"));
        book->total = (int)book->all_eps.size();
        book->source = S(res, "label");
        if (!S(res["book"], "src").empty()) book->src = S(res["book"], "src");
        book->alt_id = S(res["book"], "alt_id");
        if (book->source != "MangaDex" && !quiet)
            status = "MangaDex only hosts part of '" + cut(book->title, 30) + "' - reading all " + std::to_string(book->total) + " chapters from " + book->source;
        if (lib && lib->bv && lib->bv->b == book) lib->bv->art.clear();
        auto after = std::move(book->after);
        book->after.clear();
        for (auto& cb : after) cb();
    };
    tasks_.run(
        [bj]() mutable {
            json res = core::full_chapters(bj);
            res["book"] = bj;
            return res;
        },
        quiet ? Tasks::Cb(done) : tracked("Loading chapter list", done));
}

// open a manga chapter inside the open book: fetch its page list, then the images stream in as you turn pages
void App::read_chapter(const BookP& book, const std::string& chapter) {
    if (!book->chapters) return ensure_chapters(book, [this, book, chapter] { read_chapter(book, chapter); });
    json ch;
    for (auto& c : *book->chapters)
        if (S(c, "chapter") == chapter) {
            ch = c;
            break;
        }
    std::weak_ptr<BookView> wbv = lib ? lib->bv : nullptr;
    if (ch.is_null() || wbv.expired()) {
        status = "That chapter isn't available";
        return;
    }
    status = "Loading chapter " + chapter + " of " + book->title + "...";
    tasks_.run([ch] { return core::chapter_pages(ch); },
               tracked("Loading chapter " + chapter, [this, book, chapter, wbv](const json& urls_j, const std::string& err) {
                   if (!err.empty() || urls_j.empty()) {
                       status = "Couldn't load chapter " + chapter + ": " + (!err.empty() ? err : "no pages");
                       return;
                   }
                   std::vector<std::string> urls;
                   for (auto& u : urls_j) urls.push_back(u.get<std::string>());
                   auto bv = wbv.lock();
                   if (bv) bv->set_chapter(chapter, urls);
                   core::mr_add(book->id, !book->title_en.empty() ? book->title_en : book->title, book->poster_url, chapter, book->title_jp);
                   check_adv();
                   if (std::find(book->episodes.begin(), book->episodes.end(), chapter) == book->episodes.end()) {
                       book->episodes.push_back(chapter);
                       book->watched = (int)std::set<std::string>(book->episodes.begin(), book->episodes.end()).size();
                   }
                   book->last = today();
                   if (bv && bv->scrolling()) status = "Chapter " + chapter + ": " + std::to_string(urls.size()) + " pages - scroll down to read (V for the book)";
                   else
                       status = "Chapter " + chapter + ": " + std::to_string(urls.size()) + " pages - " +
                                ((bv && bv->rtl) ? "right to left (left arrow = next page)" : "left to right (right arrow = next page)") + ", wheel to zoom, Z fits a page";
               }));
}

void App::fetch_page_image(const std::string& url, BookView* bvp) {
    if (!lib || lib->bv.get() != bvp) return;
    std::weak_ptr<BookView> wbv = lib->bv;
    auto box_ = std::make_shared<std::shared_ptr<const Image>>();
    tasks_.run([url, box_] {
                   *box_ = decode_thumb(core::fetch_manga_image(url), 1100, 1600);
                   return json(true);
               },
               [this, url, wbv, box_](const json&, const std::string& err) {
                   if (!err.empty()) {
                       status = "Page failed to load: " + err;
                       return;
                   }
                   if (auto bv = wbv.lock()) bv->imgs[url] = *box_;
               });
}

void App::play_from_book(const BookP& book, const std::string& ep) {
    status = "Starting episode " + ep + " of " + book->title + "...";
    json eps = !book->all_eps.empty() ? json(book->all_eps) : json::array({ep});
    call("play", {{"id", book->id}, {"title", book->title}, {"ep", ep}, {"mode", audio}, {"episodes", eps}}, [this](const json&, const std::string& e) {
        if (!e.empty()) status = "Play failed: " + e;
    });
}

// fetch the poster shown on the left page when a book is opened
void App::fetch_book_poster(const BookP& book) {
    if (book->poster_img || book->poster_loading) return;
    book->poster_loading = true;
    std::string url = book->poster_url, title = book->title, id = book->id;
    auto box_ = std::make_shared<std::shared_ptr<const Image>>();
    tasks_.run(
        [url, title, id, box_] {
            std::string u = !url.empty() ? url : poster_for_sync(title, id);
            if (!u.empty()) *box_ = decode_thumb(core::fetch_poster(u), 400, 600);
            return json(true);
        },
        [book, box_](const json&, const std::string& err) {
            if (err.empty() && *box_) book->poster_img = *box_;
            book->poster_loading = false;
        });
}

// the person pulled a book out: show that anime's episodes in the main world
void App::open_history_book(const BookP& book) {
    exit_mode();
    show_rows({json{{"id", book->id}, {"title", book->title}, {"poster", ""}}}, "HISTORY");
    history_mode = true;
    int gen = rows_gen;
    poster_for(book->title, book->id, [this, gen](const std::string& url) {
        if (gen == rows_gen && !scene.peds.empty() && !url.empty()) {
            scene.peds[0].poster_url = url;
            load_poster(gen, 0);
        }
    });
    open_ped(0);
}

// books that came from your reading list only have one title: fetch the other language in one request
void App::fill_jp_titles() {
    std::vector<std::string> need;
    if (lib)
        for (auto& b : lib->books)
            if (b->title_jp.empty() && b->id.find(':') == std::string::npos && need.size() < 100) need.push_back(b->id);
    if (need.empty()) return;
    tasks_.run([need] { return core::md_alt_titles(need); },
               [this](const json& res, const std::string& err) {
                   if (!err.empty() || !res.is_object() || !lib) return;
                   for (auto& b : lib->books)
                       if (res.contains(b->id)) {
                           b->title_en = res[b->id][0].get<std::string>();
                           b->title_jp = res[b->id][1].get<std::string>();
                       }
                   lib->apply_titles();
               });
}

void App::toggle_titles() {
    manga_jp = !manga_jp;
    themes::save_settings({{"manga_jp", manga_jp}});
    if (lib && lib->kind == "manga") lib->apply_titles();
    status = manga_jp ? "Manga titles: Japanese (romaji)" : "Manga titles: English";
}

void App::open_link(const std::string& url) {
    if (open_url_hook) open_url_hook(url);
    else paths::open_url(url);
}

// MangaFire as the last resort: its search page opens in the browser
void App::open_mangafire(const std::string& title) {
    open_link(core::mangafire_url(title));
    status = "Opened MangaFire for '" + title + "'";
}

void App::librarian_search(const std::string& q_raw) {
    std::string q = su::strip(q_raw);
    if (q.empty() || !(mode == "library" && lib && lib->kind == "manga")) return;
    status = "The librarian is searching for '" + q + "'...";
    std::vector<std::string> rec = {q};
    for (auto& r2 : recent)
        if (su::lower(r2) != su::lower(q)) rec.push_back(r2);
    if (rec.size() > 8) rec.resize(8);
    recent = rec;
    themes::save_settings({{"librarian_recent", recent}});
    lib_busy = true;
    tasks_.run(
        [q] {
            json out = json::array();
            for (auto& it : core::search_all(q))
                out.push_back({{"id", it["id"]}, {"title", it["title"]}, {"title_en", !S(it, "title_en").empty() ? S(it, "title_en") : S(it, "title")},
                               {"title_jp", S(it, "title_jp")}, {"episodes", json::array()}, {"total", it.value("total", 0)}, {"poster_url", S(it, "cover")},
                               {"src", !S(it, "src").empty() ? S(it, "src") : "md"}});
            return out;
        },
        tracked("Librarian: " + q, [this, q](const json& books, const std::string& err) {
            lib_busy = false;
            if (!err.empty()) {
                status = "The librarian couldn't search: " + err;
            } else if (!lib || lib->kind != "manga") {
                return;
            } else if (books.empty()) {
                lib->mangafire = q;
                status = "Nothing hosted for '" + q + "' - the MANGAFIRE button opens it in your browser";
            } else {
                std::vector<BookP> found;
                for (auto& bj : books) found.push_back(book_from(bj));
                lib->set_search(q, found);
                status = "The librarian found " + std::to_string(found.size()) + " for '" + q + "' - she walked you to the shelf";
            }
        }));
}

// entries under the librarian's box: recent searches, or matches among your searches and the books on the shelves
std::vector<std::string> App::lib_choices() const {
    static const std::vector<std::string> STARTERS = {"One Piece", "Berserk", "Vagabond", "Chainsaw Man", "Frieren", "Dandadan", "Blue Lock", "Vinland Saga"};
    std::string q = su::lower(su::strip(lib_in.text()));
    std::vector<std::string> out;
    if (q.empty()) {
        out = recent;
        for (auto& s : STARTERS)
            if (std::find(recent.begin(), recent.end(), s) == recent.end()) out.push_back(s);
        if (out.size() > 7) out.resize(7);
        return out;
    }
    std::vector<std::string> pool = recent;
    if (lib)
        for (auto& b : lib->books) pool.push_back(b->title);
    std::set<std::string> seen;
    for (auto& tt : pool)
        if (seen.insert(tt).second && su::contains(su::lower(tt), q) && su::lower(tt) != q && out.size() < 7) out.push_back(tt);
    return out;
}

// write a chapter as a PDF and open it in the system's document reader (the next unread chapter when none is given)
void App::export_chapter(const BookP& book, std::optional<std::string> chapter) {
    if (!chapter && lib && lib->bv && lib->bv->b == book) chapter = lib->bv->mode == "toc" ? lib->bv->next_ep() : lib->bv->chapter;
    status = "Preparing " + (chapter ? "chapter " + *chapter : std::string("the next chapter")) + " of " + book->title + " as a PDF...";
    json bj = book_json(*book);
    std::optional<json> have = book->chapters;
    std::vector<std::string> done_list = book->episodes;
    std::string title_en = !book->title_en.empty() ? book->title_en : book->title;
    tasks_.run(
        [bj, have, done_list, chapter, title_en]() mutable {
            json chapters, fetched;
            std::string source;
            if (!have) {
                json res = core::full_chapters(bj);
                chapters = res["chapters"];
                source = S(res, "label");
                fetched = res;
            } else {
                chapters = *have;
            }
            if (chapters.empty()) throw std::runtime_error("no hosted chapters");
            std::set<std::string> done_set(done_list.begin(), done_list.end());
            std::string no;
            if (chapter) no = *chapter;
            else {
                no = S(chapters[0], "chapter");
                for (auto& c : chapters)
                    if (!done_set.count(S(c, "chapter"))) {
                        no = S(c, "chapter");
                        break;
                    }
            }
            json ch;
            for (auto& c : chapters)
                if (S(c, "chapter") == no) {
                    ch = c;
                    break;
                }
            if (ch.is_null()) throw std::runtime_error("chapter " + no + " isn't available");
            json urls = core::chapter_pages(ch);
            std::vector<std::future<std::string>> files;  // the API gate keeps a few in flight
            for (auto& u : urls) {
                std::string url = u.get<std::string>();
                files.push_back(std::async(std::launch::async, [url] { return core::fetch_manga_image(url); }));
            }
            std::vector<Image> pages;
            for (auto& f : files) {
                Image im = img::decode_file(f.get());
                if (im.w > 1500) im = img::resize(im, 1500, (int)std::lround(im.h * 1500.0 / im.w));
                pages.push_back(std::move(im));
            }
            std::string safe;
            for (uint32_t c : su::utf8_decode(title_en))
                if ((c < 128 && std::isalnum((int)c)) || c == ' ' || c == '-' || c == '_') safe += (char)c;
            safe = su::strip(safe.substr(0, 60));
            if (safe.empty()) safe = "manga";
            std::string out = paths::join(paths::join(paths::documents_dir(), safe), safe + " - chapter " + no + ".pdf");
            if (!pdf::write_images(out, pages, 110.0)) throw std::runtime_error("could not write " + out);
            return json{{"out", out}, {"no", no}, {"chapters", have ? json(nullptr) : chapters}, {"source", source}};
        },
        tracked("Making the chapter PDF", [this, book](const json& res, const std::string& err) {
            if (!err.empty() || res.is_null()) {
                status = "Couldn't make the PDF: " + err;
                return;
            }
            std::string out = S(res, "out"), no = S(res, "no");
            if (!res["chapters"].is_null() && !book->chapters) {
                book->chapters = res["chapters"];
                book->source = S(res, "source");
                book->all_eps.clear();
                for (auto& c : res["chapters"]) book->all_eps.push_back(S(c, "chapter"));
            }
            core::mr_add(book->id, !book->title_en.empty() ? book->title_en : book->title, book->poster_url, no, book->title_jp);
            if (std::find(book->episodes.begin(), book->episodes.end(), no) == book->episodes.end()) {
                book->episodes.push_back(no);
                book->watched = (int)std::set<std::string>(book->episodes.begin(), book->episodes.end()).size();
            }
            paths::open_file(out);
            status = "Opened chapter " + no + " in your document reader  (" + out + ")";
            check_adv();
        }));
}

void App::toggle_manga_direction() {
    manga_rtl = !manga_rtl;
    themes::save_settings({{"manga_rtl_v2", manga_rtl}});
    if (lib && lib->bv) lib->bv->set_direction(manga_rtl);
    status = manga_rtl ? "Reading direction: right to left (classic manga, left arrow = next page)" : "Reading direction: left to right (right arrow = next page)";
}

void App::cycle_theme(int step) {
    const auto& order = themes::order();
    int i = (int)(std::find(order.begin(), order.end(), theme_->key) - order.begin());
    i = (int)pmod(i + step, (double)order.size());
    theme_ = &themes::get(order[i]);
    themes::save_theme(theme_->key);
    scene.dist_scale = (theme_->fx == "jurassic" || theme_->fx == "halloween" || theme_->fx == "forest") ? 1.35 : 1.0;
    if (mode == "world" && !scene.field && !scene.peds.empty()) scene.goto_home();
    status = "Theme: " + theme_->label;
}

void App::home() {
    follow.reset();
    if (mode != "world") return exit_mode();
    if (scene.field) scene.close_field();
    else if (view != "home") show_home();  // HOME from a search / history view: back to continue-watching + recommendations
    else scene.goto_home();
    info_manga = json::array();
    info_related = nullptr;
}

// ---------------------------------------------------------------- input
void App::on_pointer_move(double x, double y) {
    mouse = {x, y};
    if (down && !buttons_held.empty()) {
        double dx = x - down->first, dy = y - down->second;
        if (dragged || std::hypot(dx, dy) > 5) {
            dragged = true;
            auto [mx_, my_] = last_drag;
            if (mode == "library" && lib && lib->drag_candidate) {
                auto [o, d] = mouse_ray();
                lib->start_drag(o, d);
            }
            if (mode == "library" && lib && lib->drag) {
                auto [o, d] = mouse_ray();
                lib->drag_move(o, d);
            } else if (mode == "library" && lib && lib->pick) {
                lib->pan_by(x - mx_, y - my_);  // drag pans the zoomed book
            } else if (buttons_held.count(1)) {
                scene.cam.orbit(x - mx_, y - my_);
            } else {
                scene.cam.pan(x - mx_, y - my_);
            }
        }
        last_drag = {x, y};
    }
}

void App::on_pointer_down(double x, double y, int button) {
    buttons_held.insert(button);
    down = std::make_pair(x, y);
    last_drag = *down;
    dragged = false;
    mouse = *down;
    if (mode == "library" && lib && !lib->pick && !arcade_menu && !adv_open && !over_hits()) {
        auto [o, d] = mouse_ray();
        lib->drag_candidate = lib->pick_book(o, d);  // a press on a shelved book may become a drag
    }
}

void App::on_pointer_up(double x, double y, int button) {
    buttons_held.erase(button);
    if (mode == "library" && lib && lib->drag) lib->drop();
    else if (down && !dragged && button == 1) click(x, y);
    if (lib) lib->drag_candidate.reset();
    down.reset();
}

void App::on_wheel(double dy) {
    if (adv_open) adv_scroll = std::max(0.0, adv_scroll + (dy > 0 ? 60 : -60));
    else if (mode == "library" && lib && lib->drawer && !lib->pick && mouse.first < 340) lib->drawer_scroll += dy > 0 ? 1 : -1;
    else if (mode == "library" && lib && lib->pick) lib->zoom_by(dy);  // zoom the open book
    else scene.cam.zoom(dy);
}

void App::on_char(const std::string& c) {
    if (swallow && *swallow == c) {
        swallow.reset();
        return;
    }
    bool printable = !c.empty() && (unsigned char)c[0] >= 0x20 && c != "\x7f";
    if (lib_typing && printable) lib_in.insert(c);
    else if (typing && printable) {
        q_in.insert(c);
        sugg_due = t + 0.25;
    }
}

void App::on_key_down(const std::string& key, const std::set<std::string>& mods_) {
    mods = mods_;
    keys.insert(key);
    pressed.insert(key);
    on_key(key);
}

void App::on_key_up(const std::string& key) { keys.erase(key); }

void App::on_key(const std::string& key) {
    if (lib_typing) {  // the librarian's question
        if (lib_in.key(key, mods)) lib_sel = -1;
        else if (key == "Enter") {
            auto ch = lib_choices();
            if (lib_sel >= 0 && lib_sel < (int)ch.size()) lib_in.set(ch[lib_sel]);
            lib_typing = false;
            librarian_search(lib_in.text());
        } else if (key == "ArrowDown") lib_sel = std::min(lib_sel + 1, (int)lib_choices().size() - 1);
        else if (key == "ArrowUp") lib_sel = std::max(lib_sel - 1, -1);
        else if (key == "Escape") lib_typing = false;
        return;
    }
    if (adv_open) {
        if (key == "Escape" || key == "t" || key == "T") adv_open = false;
        else if (key == "ArrowDown" || key == "PageDown") adv_scroll += 120;
        else if (key == "ArrowUp" || key == "PageUp") adv_scroll = std::max(0.0, adv_scroll - 120);
        return;
    }
    if (key == "h" && !typing) {
        show_history();  // from anywhere: library, games, an open book...
        return;
    }
    if (key == "u" && !typing && !arcade_menu) {
        hide_ui = !hide_ui;
        status = hide_ui ? "UI hidden - press U or click the corner chip to bring it back" : "UI shown";
        return;
    }
    if (arcade_menu) {
        auto names = game_keys();
        if (key.size() == 1 && key[0] >= '1' && key[0] < '1' + (int)names.size()) start_game(names[key[0] - '1'], mode == "library" ? "library" : "");
        else if (key == "Escape" || key == "g") arcade_menu = false;
        return;
    }
    if (mode != "world") {
        if (key == "Escape" && !(lib && lib->pick)) exit_mode();
        else if (key == "t") cycle_theme();
        else if (game) game->key(key);
        else if (lib) lib->key(key);
        return;
    }
    if (typing) {
        if (q_in.key(key, mods)) {
            sugg_sel = -1;
            sugg_due = t + 0.2;
        } else if (key == "ArrowDown" && !suggest.empty()) sugg_sel = (sugg_sel + 1) % (int)suggest.size();
        else if (key == "ArrowUp" && !suggest.empty()) sugg_sel = (int)pmod(sugg_sel - 1, (double)suggest.size());
        else if (key == "Enter") {
            if (sugg_sel >= 0) pick_suggestion(sugg_sel);
            else {
                typing = false;
                suggest.clear();
                search();
            }
        } else if (key == "Tab" && !suggest.empty()) {  // complete with the highlighted (or first) suggestion without searching yet
            q_in.set(suggest[std::max(sugg_sel, 0)]);
            sugg_for = su::strip(q_in.text());
            sugg_sel = -1;
        } else if (key == "Escape" || key == "Tab") {
            typing = false;
            suggest.clear();
        }
        return;
    }
    double vol = player.contains("volume") && player["volume"].is_number() ? player["volume"].get<double>() : 100;
    if (key == "/" || key == "Tab") {
        typing = true;
        if (key == "/") swallow = "/";
        else swallow.reset();
    } else if (key == " ") control("pause");
    else if (key == "ArrowLeft") control("seek", -10);
    else if (key == "ArrowRight") control("seek", 10);
    else if (key == "ArrowUp") control("volume", std::min(130.0, vol + 5));
    else if (key == "ArrowDown") control("volume", std::max(0.0, vol - 5));
    else if (key == "n") control("next");
    else if (key == "p") control("prev");
    else if (key == "i") toggle_skip("intro");
    else if (key == "o") toggle_skip("outro");
    else if (key == "e") load_edits();
    else if (key == "s") control("stop");
    else if (key == "d") toggle_mode();
    else if (key == "t") cycle_theme();
    else if (key == "g") arcade_menu = true;
    else if (key == "l") open_library();
    else if (key == "m") open_library("manga");
    else if (key == "f") star_selected();
    else if (key == "v") toggle_follow();
    else if (key == "r") load_related();
    else if (key == "h") show_history();
    else if (key == "Escape" || key == "Backspace") home();
}

void App::click(double x, double y) {
    for (auto& h : hits) {
        if (h.x <= x && x <= h.x + h.w && h.y <= y && y <= h.y + h.h) {
            typing = false;
            auto cb = h.cb;  // a callback may rebuild the hit list
            cb(x, y);
            return;
        }
    }
    typing = false;
    if (mode == "library" && lib) {
        if (hide_ui && lib->pick && lib->bv && lib->pick->u >= 2.85 && (x < size.first * 0.14 || x > size.first * 0.86)) {
            lib->bv->go(x < size.first / 2 ? lib->bv->left_delta() : -lib->bv->left_delta());  // click the screen edges to turn pages
            return;
        }
        auto [o, d] = mouse_ray();
        lib->click(o, d);
        return;
    }
    if (mode != "world") return;
    auto [o, d] = mouse_ray();
    Hit ref = scene.pick(o, d);
    if (!ref) {
        if (jurassic()) click_dino();
        return;
    }
    if (ref.kind == "ped") open_ped(ref.i);
    else if (ref.kind == "ep") play(ref.s);
    else if (ref.kind == "root") home();
}

std::pair<V3, V3> App::mouse_ray() const {
    double W = size.first, H = size.second;
    double nx = 2 * mouse.first / W - 1, ny = 1 - 2 * mouse.second / H;
    V3 near_ = mx::unproject(r.inv_viewproj, nx, ny, -1.0);  // OpenGL clip depth runs -1..1
    V3 far_ = mx::unproject(r.inv_viewproj, nx, ny, 1.0);
    V3 d = far_ - near_;
    return {scene.cam.eye(), d / d.norm()};
}

bool App::hit_test(double x, double y, double w, double h) const { return x <= mouse.first && mouse.first <= x + w && y <= mouse.second && mouse.second <= y + h; }
bool App::over_hits() const {
    for (auto& h : hits)
        if (hit_test(h.x, h.y, h.w, h.h)) return true;
    return false;
}

// ---------------------------------------------------------------- frame
void App::frame(int target_fbo, int phys_w, int phys_h, double ratio_, double dt) {
    ratio = ratio_;
    text.ratio = ratio_;
    double W = phys_w / ratio_, H = phys_h / ratio_;
    size = {W, H};
    t += dt;
    dt_last = dt;
    pump();
    if (!home_started && mode == "world") show_home();  // first frame: the world opens on HOME unless a search ran already
    drain_uploads();
    r.resize(phys_w, phys_h);
    Camera& cam = scene.cam;
    if (typing && mode == "world" && q_in.update(t, keys, mods)) {
        sugg_sel = -1;
        sugg_due = t + 0.2;
    }
    if (lib_typing && lib_in.update(t, keys, mods)) lib_sel = -1;
    update_suggestions();
    if (mode == "game" && game) {
        game->update(dt, keys);
        for (auto& key : game->achievements()) {
            auto got = adv.award(key);
            if (got) check_adv(got);
        }
    } else if (mode == "library" && lib) {
        Library* l = lib.get();
        l->update(dt, keys);  // may leave library mode (a picked book opens in the world)
        if (lib.get() == l) {
            CamGoal c = l->camera();
            cam.goto_(c.target, c.yaw, c.pitch, c.dist);
        }
    }
    Creatures& hd = herd();
    if (follow && jurassic() && *follow < (int)hd.dinos.size()) {  // follow-cam keeps the chosen animal in frame
        const Beast& d = hd.dinos[*follow];
        cam.goto_({d.x, d.flyer ? d.y : 4.0, d.z}, cam.g_yaw, std::max(cam.g_pitch, 0.25), std::min(std::max(cam.g_dist, 18.0), 70.0));
    }
    cam.update(dt);
    V3 eye = cam.eye();
    cam_ground = {eye.x, eye.z};
    V3 view_eye = eye;
    if (jurassic()) {  // roar shake
        double k = hd.shake * 0.7;
        double sx = grand().uniform(-1, 1), sy = grand().uniform(-1, 1), sz = grand().uniform(-1, 1);
        view_eye = eye + V3(sx, sy, sz) * k;
    }
    M4 proj = mx::perspective(55 * PI / 180, W / H, 0.1, 1200);
    bool focus = reading_focus();
    r.set_camera(mx::look_at(view_eye, cam.target), proj, view_eye, t, W, H, *theme_, focus ? 0.0 : effects::flash(*theme_, t));
    if (mode == "library" && lib) lib->ray = mouse_ray();
    if (mode == "world" && !over_hits()) {
        auto [o, d] = mouse_ray();
        hover = scene.pick(o, d);
    } else {
        hover = Hit{};
    }
    dino_hover.reset();
    if (jurassic() && !hover && !over_hits()) {
        auto [o, d] = mouse_ray();
        dino_hover = hd.pick(o, d);
    }
    if (mode == "world") scene.build(r, text, t, hover, player, eye, *theme_);
    else if (game) r.add_boxes(game->build(r, text, *theme_, t));
    else if (lib) r.add_boxes(lib->build(r, text, *theme_, t));
    if (mode != "library") pressed.clear();
    size_t n_q3d = r.q3d.size();
    World world = roam_world();
    double fl;
    std::vector<Box> fx = effects::build(*theme_, r, text, t, eye, &world, fl);
    if (mode == "library" && lib) keep_clear(fx, n_q3d, eye);  // nothing that moves may come between you and the books
    r.add_boxes(fx);
    hits.clear();
    hud(W, H);
    r.render(target_fbo);
}

// obstacles and the roaming band for wandering theme creatures
World App::roam_world() const {
    World w;
    if (mode != "world") {
        w.circles = {{0, 0, 34}};
        w.bounds = {40, 95};
        return w;
    }
    w.circles.push_back({0, 0, 8});
    for (auto& p : scene.peds) w.circles.push_back({p.x, p.z, 5.6});
    if (theme_->fx == "jurassic") w.circles.push_back({cam_ground.first, cam_ground.second, 16});  // nobody stands right under the camera
    if (scene.field) w.rects.push_back({scene.field->cx, scene.field->cz, scene.field->w / 2 + 3, scene.field->d / 2 + 3});
    double far = 8;
    for (auto& p : scene.peds) far = std::max(far, std::hypot(p.x, p.z));
    w.bounds = {far + 8, far + 42};
    w.has_platforms = true;
    for (auto& s : scene.sections) w.platforms.push_back({s.cx, s.zf - s.d / 2, s.w / 2 + 1.0, s.d / 2 + 1.0});
    w.platforms.push_back({0.0, 0.0, 6.0, 6.0});  // the root pedestal and an open episode field
    for (auto& rr : w.rects) w.platforms.push_back(rr);
    return w;
}
