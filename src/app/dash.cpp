// The Fire TV "dashboard": the desktop's neon 3D world hybridised with the original Xbox dashboard. Category names float
// along the top (Left / Right), the titles of the selected category are a glowing list (Up / Down) beside a floating poster
// card in front of a slowly turning crystal, with the grid floor and drifting particles behind - few objects and one
// camera, so the stick's GPU stays at 60 fps. Opening a title replaces the list by a 3D grid of episode tiles.
#include <algorithm>
#include <cmath>

#include "../util/strutil.hpp"
#include "app.hpp"
#include "../world/props.hpp"

namespace {
double sm(double a, double b, double k) { return a + (b - a) * std::min(1.0, k); }
double text_units(const std::string& s, double size) { return su::utf8_len(s) * 0.62 * size; }  // monospace: width of a label in world units
}  // namespace

// a vertical text sign facing the camera: centred horizontally on x, `size` world units tall per line
void App::dash_text(const std::string& s, double x, double y, double z, double size, C3 col, double alpha, bool center) {
    if (s.empty() || alpha <= 0.01) return;
    TextTex tt = text.get(s, 30, col);
    if (!tt.tex) return;
    double k = size / std::max(1.0, tt.h);  // `size` = the height of one line in world units
    double w = tt.w * k, h = tt.h * k;
    M4 m = mx::translate(center ? x - w / 2 : x, y - h / 2, z) * mx::scale(w, h, 1);
    r.quad3d(tt.tex, m, C4(1, 1, 1, alpha), UV{0, 1, 1, 0});
}

std::string App::dash_clip(const std::string& s, size_t n) const {
    return su::utf8_len(s) <= n ? s : su::utf8_substr(s, 0, n - 1) + "...";
}

void App::dash_build() {
    const Theme& th = *theme_;
    auto cats = tv_cats();
    auto acts = tv_actions();
    int tabs = (int)cats.size() + (int)acts.size();
    tv_sec = std::clamp(tv_sec, 0, std::max(0, tabs - 1));
    bool on_items = tv_sec < (int)cats.size();
    if (on_items) tv_idx = std::clamp(tv_idx, 0, std::max(0, (int)cats[(size_t)tv_sec].items.size() - 1));
    TvDetail dt = tv_detail();
    bool detail = dt.open;
    double k = dt_last * 9;
    dash_tab = sm(dash_tab, tv_sec, k);
    dash_item = sm(dash_item, on_items ? tv_idx : 0, k);
    if (dash_last_tab != tv_sec) {
        dash_last_tab = tv_sec;
        dash_pulse = t;
    }
    double pulse = std::max(0.0, 1.0 - (t - dash_pulse) * 1.6);
    C3 base = th.accent, alt = th.core;
    double hue = tabs > 1 ? (double)tv_sec / (tabs - 1) : 0;
    C3 tint(base.r + (alt.r - base.r) * hue, base.g + (alt.g - base.g) * hue, base.b + (alt.b - base.b) * hue);

    std::vector<Box> bx;
    // ---- a CRT television floating and drifting around behind the menus (its screen takes the category colour)
    if (props::crt_theme(th.fx)) {  // sparse themes only; it faces you most of the time and now and then turns all the way round
        double cx = detail ? -4.0 : 3.0;
        auto tv = props::crt_tv(t, tint, 1.9);
        props::tilt(tv, 0.06 * std::sin(t * 0.8), 0.05 * std::sin(t * 0.6 + 1));
        place(bx, tv, cx - 8.0 + 11.0 * std::sin(t * 0.21), 13.0 + 3.0 * std::sin(t * 0.33) + pulse * 0.6, -9.0 + 2.5 * std::cos(t * 0.17), props::crt_heading(t), 1.0, 1.0);
    }
    // the card's frame and its pedestal take their colours from the theme (and drift with the category), so no two themes match
    auto mixc = [](C3 a, C3 b, double u) { return C3(a.r + (b.r - a.r) * u, a.g + (b.g - a.g) * u, a.b + (b.b - a.b) * u); };
    auto lift = [](C3 c, double kk) { return C3(std::min(1.0, c.r * kk), std::min(1.0, c.g * kk), std::min(1.0, c.b * kk)); };
    C3 frame_c = lift(mixc(th.ped_sel, th.accent, 0.35 + 0.65 * hue), 1.15);
    C3 pedestal_c = lift(mixc(th.root, th.link, 0.25 + 0.75 * hue), 1.9);
    C3 rim_c = mixc(th.wire, th.pulse, 0.5 + 0.5 * std::sin(t * 1.5));
    // ---- drifting particles
    for (int i = 0; i < 26; i++) {
        double x = -24 + std::fmod(i * 7.31 + t * (0.25 + (i % 5) * 0.12), 48.0);
        double y = 0.6 + std::fmod(i * 3.17 + t * (0.45 + (i % 3) * 0.25), 21.0);
        bx.push_back(box(x, y, -9 + (i % 7) * 1.3, 0.14, 0.14, 0.14, th.pulse, 1.3, t, 0.0));
    }
    // ---- category plates floating along the top
    if (!detail) {
        std::vector<std::string> labels;
        for (auto& c : cats) labels.push_back(c.label);
        for (auto& a : acts) labels.push_back(a);
        const double base_sz = 1.0, gap = 3.2;
        std::vector<double> centre((size_t)tabs);
        double acc = 0;
        for (int i = 0; i < tabs; i++) {
            double w = text_units(labels[(size_t)i], base_sz);
            centre[(size_t)i] = acc + w / 2;
            acc += w + gap;
        }
        int lo = std::clamp((int)std::floor(dash_tab), 0, tabs - 1), hi = std::min(tabs - 1, lo + 1);
        double fr = dash_tab - lo;
        double mid = centre[(size_t)lo] + (centre[(size_t)hi] - centre[(size_t)lo]) * fr;  // the strip slides so the focused name sits in the middle
        for (int i = 0; i < tabs; i++) {
            double dx = i - dash_tab;
            double a = std::clamp(1.15 - std::fabs(dx) * 0.33, 0.0, 1.0);
            if (a <= 0.02) continue;
            double focus = std::max(0.0, 1.0 - std::fabs(dx));
            double size = base_sz * (1.0 + 0.45 * focus);
            C3 col = focus > 0.5 ? C3(1, 1, 1) : th.t_ped;
            dash_text(labels[(size_t)i], centre[(size_t)i] - mid, 19.3 + 0.15 * std::sin(t * 1.2 + i), 0, size, col, a, true);
        }
        double fw = text_units(labels[(size_t)tv_sec], base_sz * 1.45);
        bx.push_back(box(0, 18.3, 0, fw + 1.0, 0.1, 0.3, tint, 1.2 + pulse));
    }

    if (!detail && on_items && !cats[(size_t)tv_sec].items.empty()) {
        // ---- the title list with its glowing selection bar
        auto& items = cats[(size_t)tv_sec].items;
        int n = (int)items.size();
        bx.push_back(box(-10.2, 10.5, -0.5, 19.8, 1.75, 0.3, C3(tint.r * 0.5, tint.g * 0.5, tint.b * 0.5), 0.55 + 0.15 * std::sin(t * 3) + pulse * 0.6, 0.0, 1.0));
        bx.push_back(box(-20.2, 10.5, -0.2, 0.35, 1.75, 0.5, C3(1, 1, 1), 1.4));
        for (int j = 0; j < n; j++) {
            double dy = j - dash_item;
            if (dy < -3.4 || dy > 3.4) continue;
            double a = std::clamp(1.2 - std::fabs(dy) * 0.28, 0.0, 1.0);
            const TvItem& it = items[(size_t)j];
            bool focus = j == tv_idx;
            dash_text(dash_clip(it.title, 30), -19.4, 10.5 - dy * 2.15, 0.3, focus ? 1.15 : 0.9, focus ? C3(1, 1, 1) : th.t_ped, a, false);
            if (it.starred) bx.push_back(box(-21.4, 10.5 - dy * 2.15, 0, 0.5, 0.5, 0.3, C3(1.0, 0.82, 0.2), 1.5, t * 1.5));
        }
        dash_text(std::to_string(tv_idx + 1) + " / " + std::to_string(n), 3.2, 4.4, 0, 0.8, th.dim, 0.9, false);
        // ---- the poster card of the focused title
        const TvItem& fp = items[(size_t)tv_idx];
        double bob = 0.35 * std::sin(t * 1.3);
        double w = 8.4, h = 12.6, px = 12.0, py = 11.4 + bob;
        bx.push_back(box(px, py, -0.45, w + 0.7, h + 0.7, 0.3, frame_c, 0.8 + pulse * 0.6, 0.0, 1.0));
        if (fp.poster) {
            M4 m = mx::translate(px - w / 2, py - h / 2, 0.0) * mx::scale(w, h, 1);
            r.quad3d(fp.poster, m, C4(1, 1, 1, 1), UV{0, 1, 1, 0});
        } else {
            bx.push_back(box(px, py, -0.1, w, h, 0.2, C3(0.08, 0.1, 0.18), 0.1, 0.0, 0.6));
            dash_text(dash_clip(fp.title, 18), px, py, 0.2, 0.9, th.t_ped, 0.9, true);
        }
        bx.push_back(box(px, 2.1, 0, w + 3, 0.34, 3.2, pedestal_c, 0.7 + 0.3 * pulse, 0.0, 1.6));  // the pedestal stays well below the card, bob included
        bx.push_back(box(px, 2.32, 1.5, w + 2.4, 0.1, 0.18, rim_c, 1.3, 0.0, 0.0));            // a lit rim along its front edge
    } else if (!detail && !on_items) {
        // an action tab (SEARCH / HISTORY / ...): its name and what Select does
        dash_text(acts[(size_t)(tv_sec - (int)cats.size())], 0, 10.5, 0.2, 3.0, C3(1, 1, 1), 1.0, true);
        dash_text("press SELECT", 0, 7.4, 0.2, 1.0, th.dim, 0.9, true);
    } else if (!detail) {
        dash_text(tv_manga ? "no manga here yet" : "nothing here yet", 0, 10.5, 0.2, 1.6, th.dim, 0.9, true);
    }

    if (detail) {
        // ---- the opened title: poster card on the left, tiles and the two buttons on the right
        double w = 9.2, h = 13.8, px = -12.5, py = 11.6 + 0.3 * std::sin(t * 1.3);
        bx.push_back(box(px, py, -0.45, w + 0.7, h + 0.7, 0.3, frame_c, 0.8, 0.0, 1.0));
        if (dt.poster) {
            M4 m = mx::translate(px - w / 2, py - h / 2, 0.0) * mx::scale(w, h, 1);
            r.quad3d(dt.poster, m, C4(1, 1, 1, 1), UV{0, 1, 1, 0});
        }
        bx.push_back(box(px, 2.1, 0, w + 3, 0.34, 3.2, pedestal_c, 0.7, 0.0, 1.6));
        bx.push_back(box(px, 2.32, 1.5, w + 2.4, 0.1, 0.18, rim_c, 1.3, 0.0, 0.0));
        int f = tv_ep;  // -2 PLAY, -1 LIST, 0.. tiles
        auto button = [&](const std::string& label, double x, double y, double bw, bool focus) {
            bx.push_back(box(x, y, focus ? 0.3 : -0.2, bw, 1.7, 0.5, focus ? tint : th.slab, focus ? 1.2 + 0.3 * std::sin(t * 4) : 0.15, 0.0, 1.0));
            dash_text(label, x, y, 0.6, 0.85, focus ? C3(0, 0, 0) : C3(1, 1, 1), 1.0, true);
        };
        std::string go;
        for (auto& e : dt.eps)
            if (!dt.watched.count(e)) {
                go = e;
                break;
            }
        bool any_watched = !dt.watched.empty();
        std::string verb = tv_manga ? "READING" : "WATCHING";
        button(go.empty() ? (tv_manga ? "READ AGAIN" : "WATCH AGAIN") : (any_watched ? "CONTINUE  " + dt.unit + " " + go : "START " + verb), 2.4, 18.2, 9.6, f == -2);
        std::string list_name = tv_manga ? "MY LIST" : "WATCHLIST";
        button(dt.starred ? "ON " + list_name + "  -  REMOVE" : "ADD TO " + list_name, 13.6, 18.2, 11.6, f == -1);
        int n = (int)dt.eps.size();
        tv_cols = 5;
        double pitch_x = 4.5, pitch_y = 3.05, x0 = 2.6, y0 = 14.6;
        double want = f >= 0 ? std::max(0.0, (double)(f / tv_cols) - 3.0) : 0.0;
        dash_grid = sm(dash_grid, want, k);
        for (int i = 0; i < n; i++) {
            int rr = i / tv_cols, cc = i % tv_cols;
            double dy = rr - dash_grid;
            if (dy < -0.6 || dy > 4.6) continue;
            double a = std::clamp(1.0 - std::max(0.0, -dy) * 1.5, 0.0, 1.0);
            double x = x0 + cc * pitch_x, y = y0 - dy * pitch_y;
            bool focus = f == i, seen = dt.watched.count(dt.eps[(size_t)i]) > 0;
            C3 col = focus ? tint : (seen ? th.ep_seen : th.ep);
            bx.push_back(box(x, y, focus ? 0.5 : 0.0, 4.0, 2.5, focus ? 0.9 : 0.5, col, focus ? 1.3 + 0.3 * std::sin(t * 4) : (seen ? 0.12 : 0.3) * a, 0.0, 1.0));
            dash_text(dt.unit + " " + dt.eps[(size_t)i], x, y + 0.25, focus ? 1.0 : 0.6, 0.9, focus ? C3(0, 0, 0) : C3(1, 1, 1), a, true);
            if (seen && !focus) dash_text(tv_manga ? "read" : "watched", x, y - 0.7, 0.6, 0.5, th.dim, a, true);
        }
    }
    r.add_boxes(bx);
}
