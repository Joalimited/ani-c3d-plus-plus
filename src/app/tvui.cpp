// TV input + overlay for the Fire TV dashboard (the 3D scene itself is built in dash.cpp). It is another presentation of the
// data the desktop 3D world shows (scene.peds / sections / field), so searching, history, posters and playback are shared.
#include <algorithm>
#include <iostream>
#include <cmath>

#include "../core/androidplayer.hpp"
#include "../core/core.hpp"
#include "../util/strutil.hpp"
#include "app.hpp"

namespace {
std::string S(const nlohmann::json& j, const char* k) {
    return j.is_object() && j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : "";
}
}  // namespace

std::vector<std::string> App::tv_actions() const {
    if (tv_manga) return {"ANIME", "SEARCH", "THEME"};
    return {"WATCHING", "DISCOVER", "MANGA", "SEARCH", "HISTORY", "WATCHLIST", "THEME"};
}

std::vector<App::TvCat> App::tv_cats() const {
    std::vector<TvCat> out;
    if (tv_manga) {
        for (auto& [label, idxs] : mg_groups) {
            TvCat c;
            c.label = label;
            for (int i : idxs) {
                TvItem it;
                it.title = S(mg_items[(size_t)i].book, "title");
                it.id = S(mg_items[(size_t)i].book, "id");
                it.poster = mg_items[(size_t)i].poster;
                it.ref = i;
                it.starred = mg_listed.count(it.id) > 0;
                c.items.push_back(it);
            }
            out.push_back(c);
        }
        return out;
    }
    for (auto& idxs : tv_rows()) {
        TvCat c;
        const Ped& first = scene.peds[(size_t)idxs[0]];
        c.label = first.sec >= 0 && first.sec < (int)scene.sections.size() ? scene.sections[(size_t)first.sec].label : "";
        for (int i : idxs) {
            const Ped& p = scene.peds[(size_t)i];
            TvItem it;
            it.title = p.title;
            it.sub = p.sub;
            it.id = p.id;
            it.poster = p.poster;
            it.ref = i;
            it.starred = starred.count(p.id) > 0;
            c.items.push_back(it);
        }
        out.push_back(c);
    }
    return out;
}

App::TvDetail App::tv_detail() const {
    TvDetail d;
    if (tv_manga) {
        if (!mg_detail || !mg_book || mg_open_item < 0 || mg_open_item >= (int)mg_items.size()) return d;
        d.open = true;
        d.title = mg_book->title;
        d.unit = "CH";
        d.poster = mg_items[(size_t)mg_open_item].poster;
        d.eps = mg_chapters;
        d.watched = mg_read;
        d.starred = mg_listed.count(mg_book->id) > 0;
        return d;
    }
    if (scene.field && scene.selected >= 0 && scene.selected < (int)scene.peds.size()) {
        const Ped& p = scene.peds[(size_t)scene.selected];
        d.open = true;
        d.title = p.title;
        d.unit = "EP";
        d.poster = p.poster;
        d.eps = scene.field->eps;
        d.watched = scene.field->watched;
        d.starred = starred.count(p.id) > 0;
    }
    return d;
}

void App::tv_detail_play(const std::string& ep) {
    if (tv_manga) mg_read_chapter(ep);
    else play(ep);
}

void App::tv_detail_star() {
    if (!tv_manga) {
        star_selected();
        return;
    }
    if (!mg_book) return;
    bool on = !mg_listed.count(mg_book->id);
    core::mp_set(mg_book->id, on, mg_book->title, mg_book->poster_url, mg_book->title_jp);
    if (on) mg_listed.insert(mg_book->id);
    else mg_listed.erase(mg_book->id);
    status = on ? "Added to your manga list" : "Removed from your manga list";
}

void App::tv_open(int ref) {
    tv_ep = -2;
    if (tv_manga) mg_open(ref);
    else open_ped(ref);
}

void App::tv_action(const std::string& name) {
    if (name == "WATCHING" || name == "HOME") {
        tv_manga = false;
        show_watching();
    } else if (name == "DISCOVER") {
        tv_manga = false;
        show_home();
    } else if (name == "ANIME") {
        tv_manga = false;
        mg_detail = mg_reading = false;
        show_watching();
    } else if (name == "MANGA") {
        mg_enter();
        return;
    } else if (name == "SEARCH") {
        typing = true;
        swallow.reset();
        return;
    } else if (name == "HISTORY") {
        tv_manga = false;
        show_history();
    } else if (name == "WATCHLIST") {
        tv_manga = false;
        show_watchlist();
    } else if (name == "THEME") {
        cycle_theme();
        return;
    }
    scene.close_field();
    scene.selected = -1;
}

// the pedestals of each non-empty section, in reading order
std::vector<std::vector<int>> App::tv_rows() const {
    std::vector<std::vector<int>> rows(scene.sections.size());
    for (int i = 0; i < (int)scene.peds.size(); i++) {
        int s = scene.peds[i].sec;
        if (s >= 0 && s < (int)rows.size()) rows[s].push_back(i);
    }
    for (auto& r_ : rows)
        std::stable_sort(r_.begin(), r_.end(), [&](int a, int b) {
            const Ped &pa = scene.peds[a], &pb = scene.peds[b];
            return pa.row != pb.row ? pa.row < pb.row : pa.col < pb.col;
        });
    rows.erase(std::remove_if(rows.begin(), rows.end(), [](const std::vector<int>& v) { return v.empty(); }), rows.end());
    return rows;
}

void App::show_watchlist() {
    arcade_menu = adv_open = lib_typing = typing = false;
    follow.reset();
    history_mode = false;
    view = "watchlist";
    home_started = true;
    std::vector<json> rows;
    for (auto& w : core::wl_list()) rows.push_back({{"id", w["id"]}, {"title", w["title"]}, {"poster", w.value("poster", "")}});
    status = rows.empty() ? "Your watchlist is empty - open a show and choose ADD TO WATCHLIST" : std::to_string(rows.size()) + " in your watchlist";
    std::vector<std::pair<std::string, std::vector<json>>> groups;
    if (!rows.empty()) groups.emplace_back("MY WATCHLIST", rows);
    show_rows(rows, "WATCHLIST", &groups);
}

// the TV's home: the shows you are in the middle of (with the episode and the time you stopped at), then what you watched lately
void App::show_watching() {
    arcade_menu = adv_open = lib_typing = typing = false;
    history_mode = false;
    view = "watching";
    home_started = true;
    follow.reset();
    status = "Loading what you are watching...";
    int gen = ++home_gen;
    call("home", json::object(), tracked("Loading", [this, gen](const json& r2, const std::string& err) {
             if (gen != home_gen || view != "watching") return;
             std::vector<json> now;
             std::set<std::string> seen;
             if (err.empty() && r2.is_object() && r2.contains("continue"))
                 for (auto& c : r2["continue"]) {
                     double pos = c.value("pos", 0.0);
                     std::string where = pos > 10 ? " - " + su::fmt_time(pos) : "";
                     if (!seen.insert(S(c, "title")).second) continue;
                     now.push_back({{"id", c["id"]}, {"title", c["title"]}, {"poster", c["poster"]}, {"sub", "EP " + S(c, "ep") + where}, {"resume", {{"ep", c["ep"]}, {"pos", pos}}}});
                 }
             call("history", json::object(), [this, gen, now, seen](const json& rows_in, const std::string& e2) mutable {
                 if (gen != home_gen || view != "watching") return;
                 std::vector<json> recent;
                 if (e2.empty())
                     for (auto& h : rows_in)
                         if (!S(h, "id").empty() && recent.size() < 20 && seen.insert(S(h, "title")).second)
                             recent.push_back({{"id", h["id"]}, {"title", h["title"]}, {"poster", ""}, {"last", h["last"]}});
                 if (now.empty() && recent.empty()) {  // a fresh install: show what is popular instead of an empty screen
                     status = "Nothing in progress yet - here is what is popular";
                     show_home();
                     return;
                 }
                 std::vector<json> rows;
                 std::vector<std::pair<std::string, std::vector<json>>> groups;
                 if (!now.empty()) {
                     groups.emplace_back("CURRENTLY WATCHING", now);
                     rows.insert(rows.end(), now.begin(), now.end());
                 }
                 if (!recent.empty()) {
                     groups.emplace_back("RECENTLY WATCHED", recent);
                     rows.insert(rows.end(), recent.begin(), recent.end());
                 }
                 status = std::to_string(now.size()) + " in progress";
                 show_rows(rows, "WATCHING", &groups);
             });
         }));
}

bool App::tv_back() {
    if (mg_reading) {
        mg_close_reader();
        return true;
    }
    if (tv_manga && mg_detail) {
        mg_detail = false;
        mg_book.reset();
        tv_ep = -2;
        return true;
    }
    if (typing) {
        typing = false;
        suggest.clear();
        return true;
    }
    if (scene.field) {  // close the title, back to the list
        scene.close_field();
        scene.selected = -1;
        tv_ep = -2;
        return true;
    }
    if (tv_manga) {  // leave the manga dashboard for what you are watching
        tv_manga = false;
        show_watching();
        tv_sec = tv_idx = 0;
        return true;
    }
    if (view != "watching") {
        show_watching();
        tv_sec = tv_idx = 0;
        return true;
    }
    if (tv_sec != 0 || tv_idx != 0) {
        tv_sec = tv_idx = 0;
        return true;
    }
    return false;  // already at the top: the caller leaves the app
}

void App::tv_menu() {  // the Menu button: flip the reading direction in the reader, else jump to the action tabs and back
    if (mg_reading) {
        manga_rtl = !manga_rtl;
        themes::save_settings({{"manga_rtl_v2", manga_rtl}});
        mg_hud = t + 4;
        return;
    }
    auto cats = tv_cats();
    tv_sec = tv_sec < (int)cats.size() ? (int)cats.size() : 0;
    tv_idx = 0;
}

// D-pad / Select / Back as key names; true when consumed
bool App::tv_key(const std::string& key) {
    bool up = key == "ArrowUp", down = key == "ArrowDown", left = key == "ArrowLeft", right = key == "ArrowRight";
    bool sel = key == "Enter";
    bool back = key == "Escape" || key == "Backspace";
    if (mg_reading) return back ? tv_back() : mg_key(key) || key == "n" || key == "p";
    if (!(up || down || left || right || sel || back)) return false;
    if (back) return tv_back();
    TvDetail dt = tv_detail();
    if (dt.open) {  // ---- a title is open: PLAY / LIST buttons, then the episode / chapter tiles
        int n = (int)dt.eps.size();
        int cols = tv_cols;
        int& f = tv_ep;  // -2 PLAY, -1 LIST, 0..n-1 tiles
        if (f < -2 || f >= n) f = -2;
        if (sel) {
            if (f == -2) {
                std::string go = dt.eps.empty() ? "" : dt.eps[0];
                for (auto& e : dt.eps)
                    if (!dt.watched.count(e)) {
                        go = e;
                        break;
                    }
                if (!go.empty()) tv_detail_play(go);
            } else if (f == -1) {
                tv_detail_star();
            } else if (f >= 0) {
                tv_detail_play(dt.eps[(size_t)f]);
            }
        } else if (up) {
            if (f >= 0) f = f - cols >= 0 ? f - cols : (f % cols < 2 ? -2 : -1);
        } else if (down) {
            if (f < 0) f = n ? std::min(n - 1, f == -2 ? 0 : 3) : -1;
            else f = std::min(n - 1, f + cols);
        } else if (left) {
            if (f >= 0 && f % cols != 0) f--;
            else if (f == -1) f = -2;
        } else if (right) {
            if (f == -2) f = -1;
            else if (f >= 0 && f + 1 < n) f++;
        }
        return true;
    }
    // ---- the dashboard: categories left / right, titles up / down
    auto cats = tv_cats();
    auto acts = tv_actions();
    int tabs = (int)cats.size() + (int)acts.size();
    tv_sec = std::clamp(tv_sec, 0, tabs - 1);
    bool on_items = tv_sec < (int)cats.size();
    if (left) {
        if (tv_sec > 0) tv_sec--;
        tv_idx = 0;
    } else if (right) {
        if (tv_sec + 1 < tabs) tv_sec++;
        tv_idx = 0;
    } else if (on_items && up) {
        tv_idx = std::max(0, tv_idx - 1);
    } else if (on_items && down) {
        tv_idx = std::min((int)cats[(size_t)tv_sec].items.size() - 1, tv_idx + 1);
    } else if (sel) {
        if (on_items) {
            auto& items = cats[(size_t)tv_sec].items;
            tv_idx = std::clamp(tv_idx, 0, (int)items.size() - 1);
            tv_open(items[(size_t)tv_idx].ref);
        } else {
            tv_action(acts[(size_t)(tv_sec - (int)cats.size())]);
            tv_sec = tv_idx = 0;
        }
    }
    return true;
}

void App::tv_volume(double delta) {
    last_input = t;
    std::cout << "ani-c3d: volume key " << (delta > 0 ? "+" : "-") << " -> " << std::min(100.0, std::max(0.0, aplayer::volume_pct() + delta)) << "%"
              << (video_on ? " (while playing)" : "") << std::endl;
    aplayer::set_volume_pct(aplayer::volume_pct() + delta);
    vol_until = t + 2.2;
}

void App::draw_volume(double W, double H) {
    if (t >= vol_until) return;
    const Theme& th = *theme_;
    double v = aplayer::volume_pct();
    double w = 300, x = W - w - 30, y = 26;
    r.rect(x - 14, y - 10, w + 28, 52, C4(th.panel.r, th.panel.g, th.panel.b, 0.92));
    txt("VOL", x, y + 4, 15, th.text);
    r.rect(x + 56, y + 8, w - 130, 12, C4(0, 0, 0, 0.5));
    r.rect(x + 56, y + 8, (w - 130) * v / 100.0, 12, C4(th.accent, 1.0));
    txt(su::fmt("%d%%", (int)std::round(v)), x + w - 62, y + 4, 15, th.text);
}

// the 2D layer over the 3D dashboard: synopsis, search box, hints, volume, errors
void App::tv_hud(double W, double H) {
    const Theme& th = *theme_;
    if (mg_reading) {
        mg_draw(W, H);
        draw_volume(W, H);
        return;
    }
    auto cats = tv_cats();
    TvDetail dt = tv_detail();
    bool detail = dt.open;
    struct Shown {
        std::string title, sub;
    } shown;
    bool have = false;
    if (detail) {
        shown.title = dt.title;
        have = true;
    } else if (tv_sec < (int)cats.size() && !cats[(size_t)tv_sec].items.empty()) {
        auto& items = cats[(size_t)tv_sec].items;
        const TvItem& it = items[(size_t)std::clamp(tv_idx, 0, (int)items.size() - 1)];
        shown.title = it.title;
        shown.sub = it.sub;
        have = true;
    }
    const char* kind = tv_manga ? "manga" : "anime";
    if (typing) {
        double bw = std::min(W * 0.5, 640.0), bx = (W - bw) / 2;
        panel(bx, 96, bw, 56);
        std::string caret = ((long long)(t * 2)) % 2 == 0 ? "|" : " ";
        txt("SEARCH  " + q_in.text() + caret, bx + 18, 112, 22, th.text);
        txt("type with the on-screen keyboard, then NEXT", W / 2, 164, 13, th.dim, true);
    } else if (have) {
        auto bl = blurb(kind, shown.title);
        json d = bl->data.is_object() ? bl->data : json::object();
        std::vector<std::string> meta;
        if (!shown.sub.empty()) meta.push_back(shown.sub);
        if (d.contains("score") && d["score"].is_number() && d["score"].get<double>()) meta.push_back(su::fmt("%.0f%%", d["score"].get<double>()));
        for (const char* k : {"format", "year", "status"}) {
            if (!d.contains(k) || d[k].is_null()) continue;
            meta.push_back(d[k].is_number() ? std::to_string(d[k].get<long long>()) : S(d, k));
        }
        if (detail) meta.push_back(std::to_string(dt.eps.size()) + (tv_manga ? " chapters" : " eps"));
        std::string ml;
        for (auto& m : meta) ml += (ml.empty() ? "" : "   -   ") + m;
        double x = W * 0.04, y = H * 0.80, w = detail ? W * 0.30 : W * 0.52;
        if (detail) {  // the poster card sits on the left: the title and synopsis go under it
            y = H * 0.82;
            txt(su::utf8_substr(shown.title, 0, 40), x, y - 30, 20, th.text);
        }
        txt(ml, x, y, 14, th.accent);
        if (bl->state == "ok") {
            double yy = y + 26;
            for (auto& l : wrap(S(d, "desc"), 14, w, detail ? 3 : 3)) {
                txt(l, x, yy, 14, th.text);
                yy += 19;
            }
        }
    }
    // a one-line hint, only while it helps
    if (t < 25 || t - last_input < 6) {
        std::string hint = detail ? "SELECT  play / choose      BACK  close title"
                                  : "LEFT / RIGHT  categories      UP / DOWN  titles      SELECT  open      +/-  volume";
        if (tv_manga && !detail) hint = "LEFT / RIGHT  categories      UP / DOWN  titles      SELECT  open      BACK  anime";
        txt(hint, W / 2, H - 26, 12, th.dim, true);
    }
    std::string low = su::lower(status);
    if (su::contains(low, "failed") || su::contains(low, "error") || su::contains(low, "could not") || su::contains(low, "empty")) {
        double bw = std::min(W - 80, 760.0);
        r.rect((W - bw) / 2, H - 70, bw, 34, C4(0.35, 0.05, 0.08, 0.92));
        txt(status, W / 2, H - 62, 14, th.text, true);
    }
    draw_volume(W, H);
    toast_hud(W, H);
}
