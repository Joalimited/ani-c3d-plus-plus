// The HUD of the app: top bar, status and help lines, player bar, info panel, overlays, toasts and the hover hologram.
#include <algorithm>
#include <cmath>
#include <ctime>

#include "../core/core.hpp"
#include "../util/strutil.hpp"
#include "../world/effects.hpp"
#include "app.hpp"

using json = nlohmann::json;

namespace {
std::string S(const json& j, const char* k) { return j.is_object() && j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : ""; }
std::string cut(const std::string& s, size_t n) { return su::utf8_substr(s, 0, n); }
C4 a1(C3 c, double a = 1.0) { return C4(c, a); }
}  // namespace

void App::panel(double x, double y, double w, double h) {
    r.rect(x, y, w, h, theme_->panel);
    const double rr[4][4] = {{x, y, w, 1.5}, {x, y + h - 1.5, w, 1.5}, {x, y, 1.5, h}, {x + w - 1.5, y, 1.5, h}};
    for (auto& q : rr) r.rect(q[0], q[1], q[2], q[3], theme_->edge);
}

std::pair<double, double> App::txt_cb(const std::string& s, double x, double y, double px, C3 color, std::function<void(double, double)> cb, bool center) {
    TextTex tt = text.get(s, px, color);
    if (center) x -= tt.w / 2;
    r.quad2d(tt.tex, x, y, tt.w, tt.h);
    if (cb) hits.push_back({x, y, tt.w, tt.h, cb});
    return {tt.w, tt.h};
}

void App::txt(const std::string& s, double x, double y, double px, C3 color, bool center) { txt_cb(s, x, y, px, color, nullptr, center); }

void App::button(const std::string& label, double x, double y, double w, double h, std::function<void()> cb, bool active) {
    bool hot = hit_test(x, y, w, h);
    C4 e = theme_->edge;
    double k = active ? 0.6 : hot ? 0.35 : 0.12;
    r.rect(x, y, w, h, C4(e.r * k, e.g * k, e.b * k, active || hot ? 0.95 : 0.92));
    r.rect(x, y + h - 2, w, 2, e);
    txt(label, x + w / 2, y + h / 2 - 8, 13, (hot || active) ? C3(1, 1, 1) : theme_->text, true);
    hits.push_back({x, y, w, h, [cb](double, double) { cb(); }});
}

double App::text_w(const std::string& s, double px) const { return Font::get().text_length(s, std::max(6.0, std::round(px * text.ratio))) / text.ratio; }

void App::hud(double W, double H) {
    const Theme& th = *theme_;
    if (hide_ui) {  // reading mode: nothing but a faint chip to bring the controls back
        button("SHOW UI (U)", W - 118, H - 38, 108, 28, [this] { hide_ui = false; });
        return;
    }
    txt("ANI-C3D", 20, 10, 30, th.logo);
    std::vector<std::string> pending;  // spinner for work that takes noticeable time
    for (auto& [tok, mt] : busy)
        if (t - mt.second > 0.25) pending.push_back(mt.first);
    if (!pending.empty()) {
        double cx = 34, cy = 84;
        for (int i = 0; i < 8; i++) {
            double ang = i * PI / 4;
            double a = pmod(t * 9 - i, 8) / 8;
            r.rect(cx + std::cos(ang) * 9 - 2.5, cy + std::sin(ang) * 9 - 2.5, 5, 5, a1(th.accent, 0.15 + 0.85 * (1 - a)));
        }
        txt(pending.back() + "...", 56, 74, 13, th.accent);
    }
    bool wide = W >= 1500;
    txt(wide ? "FILE SYSTEM NAVIGATOR // ANIME  -  " + gpu_label + "  -  " + th.label : th.label, 22, 46, 11, th.dim);

    // top bar laid out from the right: buttons, then a search bar that fills what is left after the logo
    double bx = W - 7 * 85 - 8;
    double right_edge = bx - 8;
    double left_limit = wide ? 410 : 190;
    double avail = right_edge - left_limit;
    double sw = std::min(460.0, std::max(avail, 160.0));
    double sx = left_limit + std::max(avail - sw, 0.0) / 2;
    panel(sx, 14, sw, 38);
    if (mode == "world") {
        size_t room = (size_t)std::max((int)((sw - 70) / 9.4), 6);
        if (!q_in.text().empty() || typing) {
            auto [vis, cp] = q_in.window(room);
            std::string caret = typing && ((long long)(t * 2)) % 2 == 0 ? "|" : " ";
            txt("SEARCH> " + su::utf8_substr(vis, 0, cp) + caret + su::utf8_substr(vis, cp), sx + 14, 22, 16, th.text);
        } else {
            txt("SEARCH> press / to search", sx + 14, 22, 16, th.dim);
        }
        hits.push_back({sx, 14, sw, 38, [this](double, double) { typing = true; }});
    } else {
        txt(game ? game->name : lib ? lib->name : "", sx + sw / 2, 22, 16, th.accent, true);
    }
    bool in_manga = mode == "library" && lib && lib->kind == "manga";
    struct B {
        const char* label;
        std::function<void()> cb;
        bool act;
    };
    std::vector<B> bs = {{"ARCADE", [this] { arcade_menu = !arcade_menu; }, arcade_menu || mode == "game"},
                         {"LIBRARY", [this] { open_library(); }, mode == "library" && !in_manga},
                         {"MANGA", [this] { open_library("manga"); }, in_manga},
                         {"THEME", [this] { cycle_theme(); }, false},
                         {"HOME", [this] { home(); }, false},
                         {"HISTORY", [this] { show_history(); }, history_mode && mode == "world"},
                         {"TROPHIES", [this] { adv_open = !adv_open; }, adv_open}};
    for (size_t i = 0; i < bs.size(); i++) button(bs[i].label, bx + i * 85, 14, 80, 38, bs[i].cb, bs[i].act);

    if (mode == "world") info_panel(W, H);
    else if (game) {
        txt(game->help, W / 2, 66, 14, th.dim, true);
        game->hud(W, H);
    } else if (lib) {
        lib->hud(W, H);
    }
    if (arcade_menu) arcade_overlay(W, H);

    // status + hover hint
    std::string hint = status;
    if (hover.kind == "ep" && scene.field) hint = "EPISODE " + hover.s + "  " + (scene.field->watched.count(hover.s) ? "(watched)" : "") + "  - click to play";
    else if (hover.kind == "ped") hint = scene.peds[hover.i].title + "  - click to open";
    else if (hover.kind == "root") hint = "click to return to the overview";
    if (mode == "world") {
        txt(hint, 22, H - 142, 15, th.accent);
        std::string more;
        if (jurassic()) {
            if (th.fx == "jurassic") more = "   click a dinosaur: roar   click the ground: feed   v follow";
            else if (th.fx == "forest") more = "   click a frog / ladybug / beetle / butterfly: poke it   click the ground: drop berries   v follow";
            else more = "   click a ghost / skeleton / cat...: poke it   click the ground: drop candy   v follow";
        }
        std::string help_text = "drag: orbit   right-drag: pan   wheel: zoom   / search   h history   r related   esc back   space pause   arrows seek/volume   n p next/prev   d dub   "
                                "t theme   g arcade   l library   m manga   u hide UI" + more;
        size_t room = (size_t)std::max((int)((W - 44) / 6.4), 30);
        std::vector<std::string> lines;
        std::string cur;
        for (auto& chunk : su::split(help_text, "   ")) {  // wrap between hints so the line never runs off the window
            if (cur.size() + chunk.size() + 3 > room && !cur.empty()) {
                lines.push_back(cur);
                cur = chunk;
            } else {
                cur = cur.empty() ? chunk : cur + "   " + chunk;
            }
        }
        lines.push_back(cur);
        for (size_t i = 0; i < lines.size(); i++) txt(lines[lines.size() - 1 - i], 22, H - 162 - i * 16, 11, th.dim);
    } else {
        txt(status, 22, H - (mode == "library" ? 36 : 142), 13, th.dim);
    }
    if (jurassic()) dino_hud(W, H);
    hover_card(W, H);
    if (mode != "library") player_bar(W, H);  // the libraries are for reading and browsing: no media player there
    suggestions_dropdown(sx, sw);
    if (lib_typing) librarian_overlay(W, H);
    if (adv_open) advancement_panel(W, H);
    if (reading_focus()) {  // achievements wait until the book is closed
        for (auto& tt : toasts) tt.born += dt_last;
    } else {
        toast_hud(W, H);
        if (th.fx == "vcr") vcr_overlay(W, H);
    }
}

void App::suggestions_dropdown(double sx, double sw) {
    if (!(typing && mode == "world" && !suggest.empty())) return;
    size_t n = std::min<size_t>(8, suggest.size());
    double y0 = 54;
    panel(sx, y0, sw, 8 + 26.0 * n);
    size_t room = (size_t)std::max((int)((sw - 28) / 8.4), 10);
    std::vector<HitRect> hs;
    for (size_t i = 0; i < n; i++) {
        double y = y0 + 5 + i * 26;
        bool on = (int)i == sugg_sel || hit_test(sx, y, sw, 26);
        if (on) r.rect(sx + 3, y, sw - 6, 26, C4(theme_->edge.r, theme_->edge.g, theme_->edge.b, 0.35));
        const std::string& name = suggest[i];
        std::string label = su::utf8_len(name) <= room ? name : cut(name, room - 1) + "~";
        txt(label, sx + 14, y + 4, 14, on ? C3(1, 1, 1) : theme_->text);
        hs.push_back({sx, y, sw, 26, [this, i](double, double) { pick_suggestion((int)i); }});
    }
    hits.insert(hits.begin(), hs.begin(), hs.end());  // on top of everything beneath
}

// 'Advancement made' toasts slide down from the top centre one after another
void App::toast_hud(double W, double H) {
    toasts.erase(std::remove_if(toasts.begin(), toasts.end(), [&](const Toast& x) { return !(t - x.born < 6.5); }), toasts.end());
    if (toasts.size() > 4) toasts.erase(toasts.begin(), toasts.end() - 4);
    const Theme& th = *theme_;
    for (size_t i = 0; i < toasts.size(); i++) {
        const Toast& ts = toasts[i];
        double age = t - ts.born;
        double slide = std::min(1.0, age / 0.35) * std::min(1.0, (6.5 - age) / 0.35);
        double w = 420, h = 62;
        double x = W / 2 - w / 2, y = 8 - (1 - slide) * 90 + 110 + i * 70;
        r.rect(x, y, w, h, C4(0.02, 0.03, 0.05, 0.94));
        const double rr[4][4] = {{x, y, w, 2}, {x, y + h - 2, w, 2}, {x, y, 2, h}, {x + w - 2, y, 2, h}};
        for (auto& q : rr) r.rect(q[0], q[1], q[2], q[3], a1(th.accent));
        r.rect(x + 12, y + 14, 34, 34, a1(th.accent, 0.9));
        txt("*", x + 29, y + 17, 26, {0.05, 0.05, 0.05}, true);
        txt("ADVANCEMENT MADE!", x + 58, y + 6, 12, th.accent);
        txt(cut(ts.title, 36), x + 58, y + 22, 16, {1, 1, 1});
        txt(cut(ts.desc, 52), x + 58, y + 43, 11, th.dim);
    }
}

void App::advancement_panel(double W, double H) {
    const Theme& th = *theme_;
    double pw = std::min(W - 80, 980.0), ph = H - 230;
    double x = W / 2 - pw / 2, y = 70;
    r.rect(x, y, pw, ph, C4(0.01, 0.015, 0.03, 0.97));  // opaque backdrop: the list is long and busy
    panel(x, y, pw, ph);
    auto [done, total] = adv.counts();
    txt("ADVANCEMENTS   " + std::to_string(done) + " / " + std::to_string(total), x + 18, y + 12, 22, th.logo);
    button("CLOSE (T)", x + pw - 120, y + 10, 104, 30, [this] { adv_open = false; });
    txt("generated from what you watch and read - every anime you watch gets its own ladder", x + 18, y + 44, 12, th.dim);
    double top = y + 72, bottom = y + ph - 10;
    double cy = top - adv_scroll;
    double content = 0;
    for (auto& grp : adv.groups()) {
        double head_y = cy;
        if (top - 24 <= head_y && head_y <= bottom - 24) {
            int n = 0;
            for (auto& it : grp.items) n += it.done ? 1 : 0;
            txt(cut(grp.group, 60) + "   (" + std::to_string(n) + "/" + std::to_string(grp.items.size()) + ")", x + 18, head_y, 16, th.accent);
        }
        cy += 30;
        int cols = std::max(1, (int)((pw - 36) / 232));
        for (size_t k = 0; k < grp.items.size(); k++) {
            const auto& it = grp.items[k];
            double cx = x + 18 + (k % cols) * 232, ry = cy + (k / cols) * 58;
            if (top - 4 <= ry && ry <= bottom - 54) {
                C3 col = it.done ? th.good : th.dim;
                r.rect(cx, ry, 224, 52, it.done ? C4(th.edge.r, th.edge.g, th.edge.b, 0.22) : C4(0, 0, 0, 0.35));
                r.rect(cx, ry, 4, 52, a1(col));
                txt((it.done ? "* " : "") + cut(it.title, 26), cx + 12, ry + 4, 13, it.done ? C3(1, 1, 1) : th.dim);
                txt(cut(it.desc, 36), cx + 12, ry + 24, 10, th.dim);
                if (!it.done) {
                    r.rect(cx + 12, ry + 42, 200, 4, C4(0.2, 0.2, 0.25, 1));
                    r.rect(cx + 12, ry + 42, 200.0 * it.have / std::max<long long>(it.need, 1), 4, a1(th.accent));
                }
            }
        }
        cy += ((grp.items.size() + cols - 1) / cols) * 58 + 12;
        content = cy - top + adv_scroll;
    }
    adv_scroll = std::max(0.0, std::min(adv_scroll, std::max(0.0, std::floor(content - (bottom - top)))));
}

// old-VCR look: scanlines, a rolling tracking band, tape noise, a PLAY / timecode OSD and a REC lamp
void App::vcr_overlay(double W, double H) {
    for (int yy = 0; yy < (int)H; yy += 4) r.rect(0, yy, W, 1.5, C4(0, 0, 0, 0.16));
    double band = pmod(t * 90, H + 120) - 60;  // a slow tracking band rolling up the screen
    r.rect(0, band, W, 14, C4(1, 1, 1, 0.045));
    r.rect(0, band + 14, W, 3, C4(1, 1, 1, 0.09));
    bool glitch = pmod(t, 11.0) < 0.55;  // a periodic tracking error
    if (glitch) {
        double gy = pmod(t * 37, H - 80);
        r.rect(0, gy, W, 26, C4(0.9, 0.95, 1.0, 0.16));
        r.rect(0, gy + 26, W, 4, C4(1.0, 0.1, 0.1, 0.18));
        for (int i = 0; i < 60; i++) r.rect((double)((i * 947 + (long long)(t * 700)) % std::max(1, (int)W)), gy + (i * 13) % 30, 40 + (i * 31) % 160, 2, C4(1, 1, 1, 0.5));
    }
    for (int i = 0; i < 24; i++) {  // snow
        long long nx = ((long long)i * 7919 + (long long)(t * 60) * 131) % std::max(1, (int)W);
        long long ny = ((long long)i * 104729 + (long long)(t * 60) * 71) % std::max(1, (int)H);
        r.rect((double)nx, (double)ny, 3 + i % 4, 1.5, C4(1, 1, 1, 0.22));
    }
    r.rect(0, 0, W, 10, C4(0, 0, 0, 0.35));
    r.rect(0, H - 10, W, 10, C4(0, 0, 0, 0.35));
    bool playing = player.value("playing", false);
    bool paused = player.value("paused", false);
    txt(paused ? "PAUSE ||" : (playing ? "PLAY >" : "STOP []"), 40, 104, 24, {0.95, 0.95, 1.0});
    double pos = playing ? player.value("pos", 0.0) : t;
    long long p = (long long)pos;
    txt(su::fmt("SP  %lld:%02lld:%02lld", p / 3600, p / 60 % 60, p % 60), 40, 134, 15, {0.8, 0.9, 1.0});
    if (((long long)(t * 1.5)) % 2 == 0) r.rect(22, 110, 12, 12, C4(1.0, 0.1, 0.1, 1.0));
    std::time_t now = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof buf, "%b %d %Y", std::localtime(&now));
    txt(su::upper(buf), 40, 156, 12, {1.0, 0.85, 0.3});
}

// screen-space bounding box of a world-space box, or none when it is behind the camera
std::optional<std::array<double, 4>> App::screen_box(V3 lo, V3 hi) const {
    double W = size.first, H = size.second;
    double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
    for (double cx : {lo.x, hi.x})
        for (double cy : {lo.y, hi.y})
            for (double cz : {lo.z, hi.z}) {
                auto p = r.viewproj.mul(cx, cy, cz, 1.0);
                if (p[3] <= 0.05) return std::nullopt;
                double sx = (p[0] / p[3] + 1) / 2 * W, sy = (1 - p[1] / p[3]) / 2 * H;
                x0 = std::min(x0, sx);
                y0 = std::min(y0, sy);
                x1 = std::max(x1, sx);
                y1 = std::max(y1, sy);
            }
    return std::array<double, 4>{x0, y0, x1, y1};
}

std::shared_ptr<App::Blurb> App::blurb(const std::string& kind, const std::string& title) {
    auto k = std::make_pair(kind, title);
    auto it = blurbs.find(k);
    std::shared_ptr<Blurb> bl = it == blurbs.end() ? nullptr : it->second;
    if (!bl || (bl->state == "err" && t - bl->t > 30)) {  // failed lookups retry after a while
        bl = std::make_shared<Blurb>(Blurb{"loading", nullptr, t});
        blurbs[k] = bl;
        call("blurb", {{"title", title}, {"kind", kind}}, [this, bl](const json& res, const std::string& e) {
            bl->state = !e.empty() ? "err" : (!res.is_null() ? "ok" : "none");
            bl->data = res;
            bl->t = t;
        });
    }
    return bl;
}

std::vector<std::string> App::wrap(const std::string& s, double px, double width, size_t max_lines) const {
    auto fits = [&](const std::string& x) { return text_w(x, px) <= width; };
    std::vector<std::string> lines;
    std::string cur;
    for (auto& wd : su::split_ws(s)) {
        std::string cand = su::strip(cur + " " + wd);
        if (fits(cand)) {
            cur = cand;
            continue;
        }
        if (!cur.empty()) lines.push_back(cur);
        cur = wd;
        while (!fits(cur)) {  // one very long word: hard-break it
            size_t n = su::utf8_len(cur);
            size_t c = n - 1;
            while (c > 1 && !fits(cut(cur, c))) c--;
            lines.push_back(cut(cur, c));
            cur = su::utf8_substr(cur, c);
            if (c <= 1) break;
        }
    }
    if (!cur.empty()) lines.push_back(cur);
    if (lines.size() > max_lines) {
        lines.resize(max_lines);
        std::string last = lines.back();
        while (!last.empty() && !fits(last + "...")) last = cut(last, su::utf8_len(last) - 1);
        lines.back() = su::rstrip(last, " ,.;:-") + "...";
    }
    return lines;
}

void App::hover_card(double W, double H) {
    // what the hologram should describe: (key, kind, title, cover texture, screen anchor)
    std::string key, kind, title;
    TexP tex;
    std::optional<std::array<double, 4>> anchor;
    bool ok = false;
    if (!(arcade_menu || adv_open || lib_typing || (typing && !suggest.empty()))) {
        if (mode == "world") {
            if (hover.kind == "ped" && !(dragged && down)) {  // hidden while orbiting
                const Ped& p = scene.peds[hover.i];
                anchor = screen_box({p.x - PED_W / 2, 0, p.z - PED_D / 2}, {p.x + PED_W / 2, PED_H, p.z + PED_D / 2});
                key = "ped|" + p.id + "|" + p.title;
                kind = "anime";
                title = p.title;
                tex = p.poster;
                ok = true;
            }
        } else if (mode == "library" && lib && !lib->pick && !lib->drag && !lib->books.empty()) {
            std::optional<int> i;
            if (lib->ray && !over_hits()) i = lib->pick_book(lib->ray->first, lib->ray->second);
            if (i) {
                const Book& b = *lib->books[*i];
                double y0 = lib->row_y[b.row] + 0.05;
                anchor = screen_box({b.x - b.w / 2, y0, -libc_::BOOK_D / 2}, {b.x + b.w / 2, y0 + b.h, libc_::BOOK_D / 2});
            } else if (lib->near) {  // walking the shelves with the keyboard: describe the book the person stands at
                i = lib->near;
            }
            if (i) {
                BookP b = lib->books[*i];
                if (!b->card_tex) {
                    if (b->poster_img) b->card_tex = r.make_texture(b->poster_img->rgba.data(), b->poster_img->w, b->poster_img->h);
                    else if (!b->poster_loading) fetch_book_poster(b);
                }
                key = "book|" + lib->kind + "|" + b->id;
                kind = lib->kind;
                title = b->title;
                tex = b->card_tex;
                ok = true;
            }
        }
    }
    if (!ok) {
        card.reset();
        return;
    }
    if (!card || card->key != key) card = Card{key, t};
    double age = t - card->since;
    if (age < 0.3) return;  // a short dwell, so sweeping the mouse across the shelves doesn't flash a card on every cover
    auto bl = blurb(kind, title);
    double a = std::min(1.0, (age - 0.3) / 0.18) * (0.97 + 0.03 * std::sin(t * 31));  // fade in, then a faint hologram flicker
    const Theme& th = *theme_;
    auto ink = [&](C4 c, double k = 1.0) { return C4(c.r, c.g, c.b, c.a * a * k); };
    auto inkc = [&](C3 c, double k = 1.0) { return C4(c.r, c.g, c.b, a * k); };
    auto text_ = [&](const std::string& s, double x, double y, double px, C3 col) {
        TextTex tt = text.get(s, px, col);
        r.quad2d(tt.tex, x, y, tt.w, tt.h, C4(1, 1, 1, a));
        return std::make_pair(tt.w, tt.h);
    };

    // layout (logical pixels)
    double cw = W >= 900 ? 440 : 360;
    double pad = 14, thumb_w = cw >= 440 ? 112 : 92;
    double thumb_h = thumb_w * 1.5;
    double tx = pad + thumb_w + 14;
    double col_w = cw - tx - pad;
    json d = bl->data.is_object() ? bl->data : json::object();
    auto t_lines = wrap(title, 17, col_w, 2);
    std::vector<std::string> meta;
    for (auto m : {S(d, "format"), d.contains("year") && !d["year"].is_null() ? (d["year"].is_number() ? std::to_string(d["year"].get<long long>()) : S(d, "year")) : "",
                   d.contains("count") && d["count"].is_number() && d["count"].get<double>() ? std::to_string(d["count"].get<long long>()) + (kind == "anime" ? " eps" : " ch") : "",
                   S(d, "status")})
        if (!m.empty()) meta.push_back(m);
    std::vector<std::string> desc;
    if (bl->state == "ok") desc = wrap(!S(d, "desc").empty() ? S(d, "desc") : "No synopsis yet.", 13, col_w, cw >= 440 ? 7 : 6);
    else if (bl->state == "loading") desc = {"receiving transmission" + std::string(1 + ((long long)(t * 3)) % 3, '.')};
    else if (bl->state == "none") desc = {"No description found for this one."};
    else desc = {"Couldn't reach AniList or MyAnimeList right now."};
    bool has_score = d.contains("score") && d["score"].is_number() && d["score"].get<double>();
    double body_h = t_lines.size() * 21 + 6 + ((!meta.empty() || has_score) ? 18 : 0) + 8 + desc.size() * 18;
    std::vector<std::string> genres;
    if (d.contains("genres") && d["genres"].is_array())
        for (auto& g : d["genres"])
            if (g.is_string()) genres.push_back(g.get<std::string>());
    double ch = pad + std::max(thumb_h, body_h) + (genres.empty() ? 6 : 34) + 16;

    // placement: beside the hovered cover (like a store flyout), never on top of it
    double top = 66, bottom = H - 126;  // under the top bar, above the player bar
    double x, y, ax0 = 0, ay0 = 0, ax1 = 0, ay1 = 0;
    if (anchor) {
        ax0 = (*anchor)[0];
        ay0 = (*anchor)[1];
        ax1 = (*anchor)[2];
        ay1 = (*anchor)[3];
        x = ax1 + 18 + cw <= W - 10 ? ax1 + 18 : ax0 - 18 - cw;
        if (x < 10) x = std::min(std::max(mouse.first + 26, 10.0), W - cw - 10);
        y = std::min(std::max(ay0, top), std::max(bottom - ch, top));
    } else {  // keyboard: top left, under the logo (clear of the manga shelf's REMOVED drawer tab)
        x = 34;
        y = mode == "library" ? 128 : 112;
    }

    // hologram
    C4 e = th.edge;
    if (anchor && (ax1 + 2 <= x || x + cw <= ax0 - 2)) {  // a faint projection beam from the cover to the card
        double bx0 = x > ax1 ? ax1 : x + cw, bx1 = x > ax1 ? x : ax0;
        double by = std::min(std::max((ay0 + ay1) / 2, y + 12), y + ch - 12);
        r.rect(bx0, by - 0.75, bx1 - bx0, 1.5, ink(e, 0.55));
        r.rect(bx0 - 3, by - 3, 6, 6, inkc(th.accent, 0.9));
    }
    r.rect(x, y, cw, ch, ink(C4(th.panel.r * 0.6, th.panel.g * 0.6, th.panel.b * 0.6, 0.965)));
    r.rect(x, y, cw, 46, ink(e, 0.07));  // brighter "projector" band along the top
    for (int ry = (int)y + 2; ry < (int)(y + ch); ry += 3) r.rect(x, ry, cw, 1, ink(e, 0.045));  // scanlines
    double sweep = pmod(t * 70, ch + 40) - 20;
    if (0 <= sweep && sweep <= ch - 8) r.rect(x, y + sweep, cw, 8, ink(e, 0.05));
    const double fr[4][4] = {{x, y, cw, 1}, {x, y + ch - 1, cw, 1}, {x, y, 1, ch}, {x + cw - 1, y, 1, ch}};
    for (auto& q : fr) r.rect(q[0], q[1], q[2], q[3], ink(e, 0.45));
    const double L = 16;
    const double corners[4][4] = {{x, y, 1, 1}, {x + cw, y, -1, 1}, {x, y + ch, 1, -1}, {x + cw, y + ch, -1, -1}};
    for (auto& c : corners) {  // corner brackets
        double cx = c[0], cy = c[1], sxv = c[2], syv = c[3];
        r.rect(sxv > 0 ? cx : cx - L, cy - (syv > 0 ? 0 : 2), L, 2, inkc(th.accent));
        r.rect(cx - (sxv > 0 ? 0 : 2), syv > 0 ? cy : cy - L, 2, L, inkc(th.accent));
    }
    // cover
    double px0 = x + pad, py0 = y + pad;
    if (tex) {
        r.quad2d(tex, px0, py0, thumb_w, thumb_h, C4(0.92, 1.0, 1.0, a));
        for (int ry = (int)py0; ry < (int)(py0 + thumb_h); ry += 3) r.rect(px0, ry, thumb_w, 1, ink(e, 0.06));
    } else {
        r.rect(px0, py0, thumb_w, thumb_h, ink(e, 0.12));
        text_(bl->state != "loading" ? "NO SIGNAL" : "...", px0 + 8, py0 + thumb_h / 2 - 8, 11, th.dim);
    }
    const double cf[4][4] = {{px0, py0, thumb_w, 1}, {px0, py0 + thumb_h - 1, thumb_w, 1}, {px0, py0, 1, thumb_h}, {px0 + thumb_w - 1, py0, 1, thumb_h}};
    for (auto& q : cf) r.rect(q[0], q[1], q[2], q[3], ink(e, 0.6));
    // text column
    double cx_ = x + tx, cy_ = y + pad - 2;
    for (auto& ln : t_lines) {
        text_(ln, cx_, cy_, 17, {1, 1, 1});
        cy_ += 21;
    }
    cy_ += 4;
    if (!meta.empty() || has_score) {
        double mw = 0;
        if (has_score) {
            auto [sw2, sh2] = text_(std::to_string(d["score"].get<long long>()) + "%", cx_, cy_, 12, th.good);
            mw = sw2 + 10;
        }
        while (meta.size() > 1 && text_w(su::join(meta, "  -  "), 12) > col_w - mw) meta.pop_back();  // drop status, then the count
        text_(su::join(meta, "  -  "), cx_ + mw, cy_, 12, th.dim);
        cy_ += 18;
    }
    r.rect(cx_, cy_ + 2, col_w, 1, ink(e, 0.35));
    cy_ += 8;
    for (auto& ln : desc) {
        text_(ln, cx_, cy_, 13, bl->state == "ok" ? th.text : th.dim);
        cy_ += 18;
    }
    // genre chips along the bottom, like store tags
    double gx = x + pad, gy = y + ch - 16 - 24;
    for (auto& g : genres) {
        double gw = text_w(g, 11) + 16;
        if (gx + gw > x + cw - pad) break;
        r.rect(gx, gy, gw, 20, ink(e, 0.2));
        r.rect(gx, gy + 19, gw, 1, ink(e, 0.6));
        text_(g, gx + 6, gy + 1, 11, th.text);
        gx += gw + 6;
    }
    if (!S(d, "src").empty()) {
        double sw2 = text_w("via " + S(d, "src"), 10);
        text_("via " + S(d, "src"), x + cw - pad - sw2 - 6, y + ch - 17, 10, th.dim);
    }
}

void App::dino_hud(double W, double H) {
    Creatures& h = herd();
    const Theme& th = *theme_;
    if (dino_hover && *dino_hover < (int)h.dinos.size()) {  // name tag by the cursor
        const Beast& d = h.dinos[*dino_hover];
        std::string tag = d.name + "  -  click to poke it";
        auto [mx_, my_] = mouse;
        double w = su::utf8_len(tag) * 7.6 + 20;
        double x = std::min(mx_ + 16, W - w - 8);
        panel(x, std::max(my_ - 34, 70.0), w, 26);
        txt(tag, x + 10, std::max(my_ - 31, 73.0), 12, th.accent);
    }
    if (dino_card && t - dino_card->second < 10 && dino_card->first < (int)h.dinos.size()) {  // info card for the last creature you poked
        const Beast& d = h.dinos[dino_card->first];
        double x = 22, y = 112, w = 340, hh = 156;
        panel(x, y, w, hh);
        txt(su::upper(d.name), x + 14, y + 10, 17, th.accent);
        std::string era = th.fx == "halloween" ? "haunting since " + d.era : th.fx == "forest" ? "lives on " + d.era : d.era;
        txt(d.diet + "   |   " + d.length + "   |   " + era, x + 14, y + 38, 12, th.dim);
        std::vector<std::string> lines;
        std::string cur;
        for (auto& wd : su::split_ws(d.fact)) {
            if (cur.size() + wd.size() > 42 && !cur.empty()) {
                lines.push_back(cur);
                cur = wd;
            } else {
                cur = su::strip(cur + " " + wd);
            }
        }
        lines.push_back(cur);
        for (size_t i = 0; i < lines.size() && i < 3; i++) txt(lines[i], x + 14, y + 64 + i * 20, 13, th.text);
        button(follow ? "STOP FOLLOW (V)" : "FOLLOW (V)", x + 14, y + hh - 40, 150, 28, [this] { toggle_follow(); }, (bool)follow);
        button("CLOSE", x + 176, y + hh - 40, 80, 28, [this] { dino_card.reset(); });
    }
}

void App::arcade_overlay(double W, double H) {
    const std::vector<std::pair<std::string, std::string>> entries = {
        {"snake", "1  SNAKE"}, {"breakout", "2  BREAKOUT"}, {"quiz", "3  POSTER QUIZ  (guess the anime)"}, {"kong", "4  BARREL CLIMB  (hammers, 5 stages)"},
        {"trivia", "5  ANIME TRIVIA"}, {"maze", "6  DATA MAZE  (find the anime item)"}, {"galaga", "7  DINO GALAGA  (anime bosses)"}};
    double pw = 460, ph = 62 + entries.size() * 54 + 52;
    double x = W / 2 - pw / 2, y = H / 2 - ph / 2 - 40;
    panel(x, y, pw, ph);
    txt("ARCADE", x + pw / 2, y + 14, 24, theme_->logo, true);
    std::string back = mode == "library" ? "library" : "";
    for (size_t i = 0; i < entries.size(); i++) button(entries[i].second, x + 24, y + 62 + i * 54, pw - 48, 42, [this, k = entries[i].first, back] { start_game(k, back); });
    txt("Esc closes", x + pw / 2, y + ph - 28, 12, theme_->dim, true);
}

// the librarian's question box: a modal panel with the input field, recent searches and clear hints
void App::librarian_overlay(double W, double H) {
    const Theme& th = *theme_;
    size_t n0 = hits.size();
    r.rect(0, 0, W, H, C4(0, 0, 0, 0.55));
    double pw = std::min(W - 40, 640.0);
    auto choices = lib_choices();
    double ph = 150 + 34.0 * choices.size() + (choices.empty() ? 0 : 24) + 72;
    double x = W / 2 - pw / 2, y = std::max(90.0, H * 0.18);
    r.rect(x, y, pw, ph, C4(0.015, 0.02, 0.04, 0.98));
    panel(x, y, pw, ph);
    r.rect(x, y, pw, 5, a1(th.accent));
    txt("ASK THE LIBRARIAN", x + 24, y + 18, 24, th.logo);
    txt("Search MangaDex for manga with chapters you can read here", x + 24, y + 52, 12, th.dim);
    double fx = x + 24, fy = y + 80, fw = pw - 48, fh = 46;  // input field
    r.rect(fx, fy, fw, fh, C4(0.0, 0.0, 0.0, 0.75));
    const double rr[4][4] = {{fx, fy, fw, 2}, {fx, fy + fh - 2, fw, 2}, {fx, fy, 2, fh}, {fx + fw - 2, fy, 2, fh}};
    for (auto& q : rr) r.rect(q[0], q[1], q[2], q[3], a1(th.accent));
    size_t room = (size_t)std::max((int)((fw - 70) / 11), 8);
    std::string caret = ((long long)(t * 2)) % 2 == 0 ? "|" : " ";
    if (!lib_in.text().empty()) {
        auto [vis, cp] = lib_in.window(room);
        txt(su::utf8_substr(vis, 0, cp) + caret + su::utf8_substr(vis, cp), fx + 46, fy + 11, 20, {1, 1, 1});
    } else {
        txt(caret + "e.g. berserk, solo leveling, one piece", fx + 46, fy + 12, 18, th.dim);
    }
    txt("?", fx + 16, fy + 8, 26, th.accent);
    if (lib_busy) txt("searching...", fx + fw - 120, fy + 14, 14, th.accent);
    double cy = fy + fh + 14;  // choices
    if (!choices.empty()) {
        txt(su::strip(lib_in.text()).empty() ? "RECENT AND SUGGESTED" : "MATCHES", x + 24, cy, 11, th.dim);
        cy += 24;
    }
    std::vector<HitRect> hs;
    for (size_t i = 0; i < choices.size(); i++) {
        const std::string c = choices[i];
        bool on = (int)i == lib_sel || hit_test(fx, cy, fw, 30);
        if (on) r.rect(fx, cy, fw, 30, C4(th.edge.r, th.edge.g, th.edge.b, 0.35));
        std::string tag = std::find(recent.begin(), recent.end(), c) != recent.end() ? "recent" : !su::strip(lib_in.text()).empty() ? "shelf" : "idea";
        txt(cut(c, 56), fx + 12, cy + 6, 15, on ? C3(1, 1, 1) : th.text);
        txt(tag, fx + fw - 12 - tag.size() * 7, cy + 9, 11, th.dim);
        hs.push_back({fx, cy, fw, 30, [this, c](double, double) {
                          lib_in.set(c);
                          lib_typing = false;
                          librarian_search(c);
                      }});
        cy += 34;
    }
    double by = y + ph - 54;  // footer
    txt("Enter search  -  up/down choose  -  Esc close  -  hold Backspace to erase", x + 24, by - 22, 11, th.dim);
    button("SEARCH", x + pw - 24 - 110, by, 110, 36, [this] {
        lib_typing = false;
        librarian_search(lib_in.text());
    });
    button("CANCEL", x + pw - 24 - 230, by, 110, 36, [this] { lib_typing = false; });
    // modal: the panel's own hits first, then a full-screen catch-all so nothing behind it can be clicked
    std::vector<HitRect> rebuilt = hs;
    rebuilt.insert(rebuilt.end(), hits.begin() + (long)n0, hits.end());
    rebuilt.push_back({0, 0, W, H, [](double, double) {}});
    hits = rebuilt;
}

void App::info_panel(double W, double H) {
    if (!scene.field) return;
    const Ped& p = scene.peds[scene.field->idx];
    const Theme& th = *theme_;
    struct Line {
        std::string s;
        double px;
        C3 col;
        std::string link;
    };
    std::vector<Line> lines = {{cut(p.title, 34), 16, {1, 1, 1}, ""},
                               {std::to_string(scene.field->eps.size()) + " episodes - " + std::to_string(scene.field->watched.size()) + " watched", 12, th.dim, ""},
                               {"", 8, th.dim, ""},
                               {"MANGA // allmanga.to", 12, th.accent, ""}};
    for (size_t i = 0; i < info_manga.size() && i < 4; i++) {
        const json& m = info_manga[i];
        lines.push_back({"> " + cut(S(m, "title"), 30) + " (" + (m.contains("chapters") ? m["chapters"].dump() : "?") + " ch)", 13, th.text, S(m, "url")});
    }
    if (info_manga.empty()) {
        lines.push_back({"  none found on allmanga", 12, th.dim, ""});
        lines.push_back({"> try MangaFire instead", 13, th.good, core::mangafire_url(p.title)});
    }
    lines.push_back({"", 8, th.dim, ""});
    lines.push_back({"FAN EDITS // AMVs  (E)", 12, th.accent, ""});
    if (edits.is_null()) lines.push_back({"[ E ]  FIND FAN EDITS ON YOUTUBE", 12, th.good, "@edits"});
    else if (edits.is_string()) lines.push_back({"searching YouTube...", 12, th.dim, ""});
    else {
        for (size_t i = 0; i < edits.size() && i < 6; i++) lines.push_back({"> " + cut(S(edits[i], "title"), 30) + "  " + S(edits[i], "length"), 12, th.text, S(edits[i], "url")});
        if (edits.empty()) lines.push_back({"  none found", 12, th.dim, ""});
    }
    lines.push_back({"", 8, th.dim, ""});
    if (info_related.is_null()) lines.push_back({"[ R ]  LIGHT NOVELS / GAMES / FAN SITES", 12, th.good, "@load"});
    else if (info_related.is_string()) lines.push_back({"searching related content...", 12, th.dim, ""});
    else {
        for (auto [key, head] : {std::pair<const char*, const char*>{"novels", "LIGHT NOVELS"}, {"games", "GAMES"}, {"sites", "FAN SITES & WIKIS"}}) {
            lines.push_back({head, 12, th.accent, ""});
            const json& lst = info_related[key];
            for (size_t i = 0; i < lst.size() && i < 4; i++) lines.push_back({"> " + cut(S(lst[i], "title"), 32), 12, th.text, S(lst[i], "url")});
            if (lst.empty()) lines.push_back({"  none found", 12, th.dim, ""});
        }
    }
    double pw = 352;
    double ph = 20;
    for (auto& l : lines) ph += (int)(l.px * 1.3) + 9;
    double x = W - pw - 14, y = 66;
    panel(x, y, pw, ph);
    double cy = y + 10;
    for (auto& l : lines) {
        if (!l.s.empty()) {
            std::function<void(double, double)> cb;
            if (l.link == "@load") cb = [this](double, double) { load_related(); };
            else if (l.link == "@edits") cb = [this](double, double) { load_edits(); };
            else if (!l.link.empty()) cb = [this, u = l.link](double, double) { open_link(u); };
            txt_cb(l.s, x + 14, cy, l.px, l.col, cb);
        }
        cy += (int)(l.px * 1.3) + 9;
    }
    bool st = starred.count(p.id) > 0;
    std::string id = p.id, title = p.title, poster = p.poster_url;
    button(st ? "UNSTAR  (F)" : "* STAR: PLAN TO WATCH  (F)", x, y + ph + 8, pw, 34, [this, id, title, poster] { toggle_star(id, title, poster); }, st);
}

void App::player_bar(double W, double H) {
    double x = 14, y = H - 118, w = W - 28, h = 104;
    panel(x, y, w, h);
    const json& pl = player;
    bool playing = pl.value("playing", false);
    std::string title = playing && pl.contains("anime") && pl["anime"].is_object()
                            ? S(pl["anime"], "title") + " - EPISODE " + S(pl, "episode") + "  [" + su::upper(S(pl, "mode")) + "]"
                            : "NOTHING PLAYING";
    txt(title, x + 14, y + 8, 16, playing ? C3(1, 1, 1) : theme_->dim);
    double bx = x + 14, by = y + 36, bw = w - 150;  // seek bar
    C4 e = theme_->edge;
    r.rect(bx, by, bw, 10, C4(e.r * 0.25, e.g * 0.25, e.b * 0.25, 1));
    double pos = pl.value("pos", 0.0), dur = pl.value("duration", 0.0);
    double frac = playing && dur ? pos / dur : 0;
    r.rect(bx, by, bw * std::min(frac, 1.0), 10, a1(theme_->text));
    hits.push_back({bx, by - 6, bw, 22, [this, playing, bx, bw](double mxx, double) {
                        if (playing) control("seekpct", std::round(std::max(0.0, std::min(100.0, (mxx - bx) / bw * 100)) * 10) / 10);
                    }});
    auto fmt = [](double s) {
        long long v = (long long)s;
        return su::fmt("%02lld:%02lld", v / 60, v % 60);
    };
    txt(fmt(pos) + " / " + fmt(dur), bx + bw + 14, by - 4, 14, theme_->text);
    // buttons (widths shrink on narrow windows so the whole row always fits)
    double row = y + 60;
    json sk = pl.contains("skip") && pl["skip"].is_object() ? pl["skip"] : json::object();
    (void)sk;
    double speed = pl.value("speed", 1.0);
    struct Spec {
        std::string label;
        std::function<void()> cb;
        double w;
        bool act;
    };
    std::string spd = su::fmt("%g", speed) + "x";
    std::vector<Spec> specs = {
        {"<< PREV", [this] { control("prev"); }, 74, false},
        {"-10s", [this] { control("seek", -10); }, 58, false},
        {!pl.value("paused", false) ? "PAUSE" : "PLAY", [this] { control("pause"); }, 74, pl.value("paused", false)},
        {"+10s", [this] { control("seek", 10); }, 58, false},
        {"NEXT >>", [this] { control("next"); }, 74, false},
        {"SKIP INTRO", [this] { toggle_skip("intro"); }, 104, auto_intro},
        {"SKIP CREDITS", [this] { toggle_skip("outro"); }, 124, auto_outro},
        {su::upper(audio), [this] { toggle_mode(); }, 56, audio == "dub"},
        {"STOP", [this] { control("stop"); }, 58, false},
        {"FULLSCREEN", [this] { control("fullscreen"); }, 96, false},
        {"AUTO FS", [this] { toggle_autofs(); }, 74, auto_fs},
        {"SUBS", [this] { control("subs"); }, 58, false},
        {"MUTE", [this] { control("mute"); }, 58, false},
        {spd, [this, speed] {
             double nxt = speed == 0.75 ? 1.0 : speed == 1.0 ? 1.25 : speed == 1.25 ? 1.5 : speed == 1.5 ? 2.0 : 0.75;
             control("speed", nxt);
         },
         52, false},
        {"AUTO-NEXT", [this, an = pl.value("autonext", true)] { control("autonext", !an); }, 100, pl.value("autonext", true)},
    };
    double vol_w = 150;
    double avail = w - 28 - vol_w - 8.0 * specs.size();
    double sum = 0;
    for (auto& s : specs) sum += s.w;
    double k = std::min(1.0, avail / sum);
    double bx_ = x + 14;
    for (auto& s : specs) {
        double wd = std::max(44.0, s.w * k);
        button(s.label, bx_, row, wd, 32, s.cb, s.act);
        bx_ += wd + 8;
    }
    double vx = x + w - vol_w - 6, vw = 100;  // volume
    txt("VOL", vx, row + 8, 13, theme_->dim);
    r.rect(vx + 36, row + 12, vw, 8, C4(e.r * 0.25, e.g * 0.25, e.b * 0.25, 1));
    double vol = pl.value("volume", 100.0) / 130;
    r.rect(vx + 36, row + 12, vw * std::min(vol, 1.0), 8, a1(theme_->good));
    hits.push_back({vx + 36, row + 2, vw, 28, [this, vx, vw](double mxx, double) { control("volume", std::round(std::max(0.0, std::min(130.0, (mxx - vx - 36) / vw * 130)))); }});
}
