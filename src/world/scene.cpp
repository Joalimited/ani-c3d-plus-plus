#include "scene.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

#include "../util/strutil.hpp"

using json = nlohmann::json;

void Camera::goto_(V3 t, double y, double p, double d) {
    g_target = t;
    g_yaw = y;
    g_pitch = p;
    g_dist = d;
}

void Camera::update(double dt) {
    double k = 1 - std::exp(-dt * 3.5);
    target += (g_target - target) * k;
    double dy = pmod(g_yaw - yaw + PI, 2 * PI) - PI;
    yaw += dy * k;
    pitch += (g_pitch - pitch) * k;
    dist += (g_dist - dist) * k;
}

void Camera::snap() {
    target = g_target;
    yaw = g_yaw;
    pitch = g_pitch;
    dist = g_dist;
}

V3 Camera::eye() const {
    double c = std::cos(pitch);
    return target + V3(c * std::sin(yaw), std::sin(pitch), c * std::cos(yaw)) * dist;
}

void Camera::orbit(double dx, double dy) {
    g_yaw -= dx * 0.006;
    g_pitch = std::min(std::max(g_pitch + dy * 0.006, 0.12), 1.5);
    yaw = g_yaw;
    pitch = g_pitch;
}

void Camera::pan(double dx, double dy) {
    V3 right(std::cos(yaw), 0, -std::sin(yaw));
    V3 fwd(-std::sin(yaw), 0, -std::cos(yaw));
    double s = dist * 0.0016;
    g_target = g_target - right * (dx * s) + fwd * (dy * s * -1);
    target = g_target;
}

void Camera::zoom(double dy) { g_dist = std::min(std::max(g_dist * std::pow(1.0015, dy), 6.0), 400.0); }

Scene::Scene() {}

std::vector<std::pair<double, double>> Scene::ring_positions(int n) {
    std::vector<std::pair<double, double>> pos;
    int idx = 0, ring = 0;
    while (idx < n) {
        double radius = 17 + ring * 13;
        int cap = std::max(6, (int)(2 * PI * radius / 8.6));
        int take = std::min(cap, n - idx);
        for (int k = 0; k < take; k++) {
            double a = 2 * PI * (k + 0.5 * (ring % 2)) / take;
            pos.emplace_back(radius * std::sin(a), radius * std::cos(a));
        }
        idx += take;
        ring++;
    }
    return pos;
}

// Lay the rows out in labelled sections: each is a platform with a heading and a grid of pedestals, wired to the root
// by an orthogonal bus. Without groups everything goes into one section.
void Scene::set_results(const std::vector<json>& rows, double t, const std::string& head, const std::vector<std::pair<std::string, std::vector<json>>>* groups_in) {
    heading = head;
    field.reset();
    selected = -1;
    std::vector<std::pair<std::string, std::vector<json>>> groups;
    if (groups_in) {
        for (auto& g : *groups_in)
            if (!g.second.empty()) groups.push_back(g);
    } else if (!rows.empty()) {
        groups.emplace_back(head, rows);
    }
    peds.clear();
    sections.clear();
    double z_front = -13.0;
    const double limit = 118.0;
    struct Sized {
        std::string lab;
        const std::vector<json>* rs;
        int cols, nrows;
        double w;
    };
    std::vector<Sized> sized;
    for (auto& [lab, rs] : groups) {
        int n = (int)rs.size();
        int cols = std::min(n, n <= 8 ? 4 : n <= 15 ? 5 : 6);
        sized.push_back({lab, &rs, cols, (n + cols - 1) / cols, std::max(cols * PX + 2.0, (su::utf8_len(lab) + 6) * 0.98 + 3.0)});
    }
    std::vector<std::vector<Sized>> rows_of_secs;
    std::vector<Sized> cur;
    double cur_w = 0;
    for (auto& it : sized) {  // wrap sections into rows of platforms
        if (!cur.empty() && cur_w + it.w > limit) {
            rows_of_secs.push_back(cur);
            cur.clear();
            cur_w = 0;
        }
        cur.push_back(it);
        cur_w += it.w + 5.0;
    }
    if (!cur.empty()) rows_of_secs.push_back(cur);
    int i = 0;
    for (auto& row : rows_of_secs) {
        double total = 5.0 * (row.size() - 1);
        for (auto& it : row) total += it.w;
        double x = -total / 2;
        double depth = 0;
        for (auto& it : row) depth = std::max(depth, it.nrows * PZ + HEADER);
        for (auto& it : row) {
            sections.push_back({it.lab, x, it.w, z_front, it.nrows * PZ + HEADER, x + it.w / 2, (int)it.rs->size()});
            for (size_t k = 0; k < it.rs->size(); k++) {
                const json& r = (*it.rs)[k];
                int c = (int)k % it.cols, rw = (int)k / it.cols;
                double px = x + (it.w - it.cols * PX) / 2 + (c + 0.5) * PX;
                double pz = z_front - HEADER - (rw + 0.5) * PZ;
                Ped p;
                p.id = r.value("id", "");
                p.title = r.value("title", "");
                p.poster_url = r.contains("poster") && r["poster"].is_string() ? r["poster"].get<std::string>() : "";
                p.x = px;
                p.z = pz;
                p.born = t + 0.05 * i;
                p.eps = r.contains("eps") && r["eps"].is_number() ? r["eps"].get<int>() : -1;
                p.sub = r.contains("sub") && r["sub"].is_string() ? r["sub"].get<std::string>() : "";
                p.resume = r.contains("resume") ? r["resume"] : json(nullptr);
                p.sec = (int)sections.size() - 1;
                p.row = rw;
                p.col = c;
                peds.push_back(p);
                i++;
            }
            x += it.w + 5.0;
        }
        z_front -= depth + 6.0;
    }
    goto_home();
}

void Scene::extents(double& x0, double& x1, double& z0, double& z1) const {
    x0 = x1 = z0 = z1 = 0.0;
    for (auto& p : peds) {
        x0 = std::min(x0, p.x);
        x1 = std::max(x1, p.x);
        z0 = std::min(z0, p.z);
        z1 = std::max(z1, p.z);
    }
    for (auto& s : sections) {
        x0 = std::min(x0, s.x0);
        x1 = std::max(x1, s.x0 + s.w);
        z0 = std::min({z0, s.zf, s.zf - s.d});
        z1 = std::max({z1, s.zf, s.zf - s.d});
    }
}

void Scene::goto_home() {
    double x0, x1, z0, z1;
    extents(x0, x1, z0, z1);
    double w = x1 - x0, d = z1 - z0;
    double dist = (std::max({w * 0.52, d * 0.9, 16.0}) + 14) * dist_scale;
    cam.goto_({(x0 + x1) / 2, 0, (z0 + z1) / 2 + 4}, 0.0, 46 * PI / 180, dist);
}

void Scene::open_field(int idx, const std::vector<std::string>& episodes, const std::set<std::string>& watched, double t) {
    selected = idx;
    int n = std::max((int)episodes.size(), 1);
    int cols = std::min(n, std::max(5, (int)std::ceil(std::sqrt(n * 1.7))));
    int rows = (int)std::ceil((double)n / cols);
    double pitch = 1.5;
    double w = cols * pitch, d = rows * pitch;
    // the field sits in front of the root (+z, where the camera looks from) so its rows climb away from the viewer
    double fcx = 0.0, fcz = 15 + d / 2;
    field = Field{idx, episodes, watched, cols, rows, pitch, w, d, fcx, fcz, t};
    cam.goto_({fcx, 0, fcz - d * 0.1}, 0.0, 44 * PI / 180, std::max(w, d) * 0.8 + 15);
}

void Scene::close_field() {
    field.reset();
    selected = -1;
    goto_home();
}

std::pair<double, double> Scene::ep_pos(int i) const {
    const Field& f = *field;
    int c = i % f.cols, r = i / f.cols;
    return {f.cx - f.w / 2 + (c + 0.5) * f.pitch, f.cz + f.d / 2 - (r + 0.5) * f.pitch};
}

void Scene::build(Renderer& r, TextCache& text, double t, const Hit& hover, const json& player, const V3& cam_pos, const Theme& th) {
    std::vector<Box> boxes;
    pick_boxes.clear();
    auto pickable = [&](double x0, double y0, double z0, double x1, double y1, double z1, Hit ref) { pick_boxes.push_back({{x0, y0, z0}, {x1, y1, z1}, ref}); };

    // root
    double bob = 0.15 * std::sin(t * 1.4);
    double rg = hover.kind == "root" ? 0.8 : 0.15;
    boxes.push_back(box(0, 0.3, 0, 8, 0.6, 8, th.root, rg));
    boxes.push_back(box(0, 1.9 + bob, 0, 1.7, 2.6, 1.7, th.core, 0.5 + 0.2 * std::sin(t * 2), t * 0.4));
    pickable(-4, 0, -4, 4, 3.5, 4, {"root"});
    label(r, text, heading, {0, 0.04, 5.3}, 1.1, th.t_root, true);

    // section platforms, headings and wiring: root -> trunk -> a bus along each section's front edge -> one stub per pedestal
    double zmin = -6.0;
    for (auto& s : sections) zmin = std::min(zmin, s.zf - s.d);
    if (!sections.empty()) boxes.push_back(box(0, 0.37, (zmin + 4) / 2, 0.22, 0.1, 4 - zmin, th.wire, 0.7));  // the trunk runs behind the root
    for (size_t si = 0; si < sections.size(); si++) {
        const Section& sec = sections[si];
        boxes.push_back(box(sec.cx, 0.14, sec.zf - sec.d / 2, sec.w, 0.28, sec.d, th.slab, 0.12));
        boxes.push_back(box(sec.cx, 0.3, sec.zf - 0.05, sec.w, 0.1, 0.18, th.wire, 0.9));
        if (sec.cx != 0) boxes.push_back(box(sec.cx / 2, 0.37, sec.zf + 0.6, std::fabs(sec.cx), 0.1, 0.2, th.wire, 0.7));
        double f = pmod(t * 0.25 + si * 0.21, 1.0);
        boxes.push_back(box(0, 0.45, 4 + (sec.zf + 0.6 - 4) * f, 0.5, 0.3, 0.5, th.pulse, 1.4));
        std::string head = sections.size() > 1 ? sec.label + "  (" + std::to_string(sec.n) + ")" : sec.label;
        label(r, text, head, {sec.x0 + 0.9, 0.31, sec.zf - 2.2}, 1.5, th.t_root);
    }
    std::map<std::tuple<int, int, int>, const Ped*> slot;
    for (auto& p : peds) slot[{p.sec, p.row, p.col}] = &p;
    for (auto& p : peds) {
        const Section& sec = sections[p.sec];
        auto it = slot.find({p.sec, p.row - 1, p.col});  // wired to the pedestal in front of it, or to the bus
        double z_from = it != slot.end() ? (it->second->z - PED_D / 2) : sec.zf;
        double stub = z_from - (p.z + PED_D / 2);
        if (stub > 0.05) boxes.push_back(box(p.x - PED_W / 2 + 0.35, 0.3, z_from - stub / 2, 0.14, 0.1, stub, th.wire, 0.6));
    }

    // pedestals
    for (size_t i = 0; i < peds.size(); i++) {
        Ped& p = peds[i];
        double s = ease((t - p.born) / 0.7);
        double h = PED_H * std::max(s, 0.001);
        bool hov = hover.kind == "ped" && hover.i == (int)i;
        bool sel = selected == (int)i;
        double glow = 0.15 + (hov ? 0.7 : 0) + (sel ? 0.5 : 0);
        boxes.push_back(box(p.x, h / 2, p.z, PED_W, h, PED_D, sel ? th.ped_sel : th.ped, glow));
        if (starred && starred->count(p.id))  // gold star-gem: this one is on your watch list
            boxes.push_back(box(p.x - PED_W / 2 + 0.6, PED_H + 0.9 * s + 0.2 * std::sin(t * 2 + i), p.z - PED_D / 2 + 0.6, 0.7, 0.7, 0.4, {1.0, 0.82, 0.2}, 1.5, t * 1.5));
        pickable(p.x - PED_W / 2, 0, p.z - PED_D / 2, p.x + PED_W / 2, 1.2, p.z + PED_D / 2, {"ped", (int)i});
        if (p.poster && s > 0.5) {
            M4 m = mx::translate(p.x - (PED_W - 0.5) / 2, h + 0.02, p.z - (PED_D - 0.5) / 2) * mx::rot_x(PI / 2) * mx::scale(PED_W - 0.5, PED_D - 0.5, 1);
            r.quad3d(p.poster, m);
        }
        C3 col = (hov || sel) ? C3(1, 1, 1) : th.t_ped;
        auto lines = wrap_title(p.title);  // at most two short lines, so neighbours never overlap
        for (size_t ln = 0; ln < lines.size(); ln++) label(r, text, lines[ln], {p.x, 0.33, p.z + PED_D / 2 + 1.0 + ln * 0.95}, 0.8, col, true);
        if (!p.sub.empty())  // e.g. "EP 12 - 14:05" on a continue-watching pedestal
            label(r, text, p.sub, {p.x, 0.33, p.z + PED_D / 2 + 1.0 + lines.size() * 0.95}, 0.8, {1.0, 0.82, 0.2}, true);
    }

    // episode field
    if (field) {
        Field& f = *field;
        const Ped& p = peds[f.idx];
        double L = std::hypot(f.cx - p.x, f.cz - p.z);
        double yaw = std::atan2(-(f.cz - p.z), f.cx - p.x);
        boxes.push_back(box((p.x + f.cx) / 2, 0.06, (p.z + f.cz) / 2, L, 0.12, 0.2, th.link, 0.9, yaw));
        for (int k = 0; k < 3; k++) {
            double q = pmod(t * 0.3 + k / 3.0, 1.0);
            boxes.push_back(box(p.x + (f.cx - p.x) * q, 0.3, p.z + (f.cz - p.z) * q, 0.5, 0.3, 0.5, th.pulse, 1.4, yaw));
        }
        boxes.push_back(box(f.cx, 0.2, f.cz, f.w + 2.5, 0.4, f.d + 2.5, th.slab, 0.1));
        int n = (int)f.eps.size();
        std::string playing;
        if (player.is_object() && player.value("playing", false) && player.contains("anime") && player["anime"].is_object() &&
            player["anime"].value("id", "") == p.id && player.contains("episode") && player["episode"].is_string())
            playing = player["episode"].get<std::string>();
        struct Lab {
            std::string ep;
            double x, y, z;
            bool seen;
        };
        std::vector<Lab> labels;
        for (int i = 0; i < n; i++) {
            const std::string& ep = f.eps[i];
            double s = ease((t - f.born - 0.008 * i) / 0.5);
            if (s <= 0) continue;
            auto [x, z] = ep_pos(i);
            double h = (0.6 + 3.2 * std::pow((double)(i + 1) / n, 0.9)) * s;
            C3 col;
            double glow;
            if (!playing.empty() && ep == playing) {
                col = th.ep_play;
                glow = 0.9 + 0.5 * std::sin(t * 5);
            } else if (f.watched.count(ep)) {
                col = th.ep_seen;
                glow = 0.15;
            } else {
                col = th.ep;
                glow = 0.0;
            }
            if (hover.kind == "ep" && hover.s == ep) glow += 0.8;
            boxes.push_back(box(x, 0.4 + h / 2, z, 1.05, h, 1.05, col, glow));
            pickable(x - 0.53, 0.4, z - 0.53, x + 0.53, 0.4 + h, z + 0.53, {"ep", -1, ep});
            if (std::hypot(x - cam_pos.x, z - cam_pos.z) < 38 && labels.size() < 650)
                labels.push_back({ep, x, 0.4 + h + 0.02, z, f.watched.count(ep) > 0 || (!playing.empty() && ep == playing)});
        }
        for (auto& lb : labels) digits(r, text, lb.ep, lb.x, lb.y, lb.z, lb.seen ? C4(0.9, 1.0, 0.9, 1) : C4(0.8, 0.9, 1.0, 1));
        label(r, text, su::utf8_substr(p.title, 0, 40) + "  //  " + std::to_string(n) + " EPISODES", {f.cx, 0.04, f.cz + f.d / 2 + 2.6}, 0.9, th.t_field, true);
    }
    r.add_boxes(boxes);
}

std::vector<std::string> Scene::wrap_title(const std::string& title, size_t width) {
    std::vector<std::string> lines;
    std::string cur;
    for (auto& w : su::split_ws(title)) {
        if (su::utf8_len(cur) + su::utf8_len(w) + 1 > width && !cur.empty()) {
            lines.push_back(cur);
            cur = w;
        } else {
            cur = su::strip(cur + " " + w);
        }
    }
    lines.push_back(cur);
    if (lines.size() > 2) lines = {lines[0], su::utf8_substr(lines[1], 0, width - 2) + ".."};
    for (auto& ln : lines) ln = su::utf8_substr(ln, 0, width + 3);
    return lines;
}

// text lying flat in the world
void Scene::label(Renderer& r, TextCache& text, const std::string& s, V3 pos, double height, C3 color, bool center) {
    TextTex tt = text.get(s, 44, color);
    double ww = height * tt.w / tt.h;
    double x = pos.x - (center ? ww / 2 : 0);
    M4 m = mx::translate(x, pos.y, pos.z - height / 2) * mx::rot_x(PI / 2) * mx::scale(ww, height, 1);
    r.quad3d(tt.tex, m);
}

void Scene::digits(Renderer& r, TextCache& text, const std::string& ep, double x, double y, double z, C4 tint) {
    int n = (int)ep.size();
    double dw = std::min(0.34, 0.95 / n);
    double dh = dw / text.digit_aspect;
    double x0 = x - n * dw / 2;
    for (int k = 0; k < n; k++) {
        char ch = ep[k];
        if (ch >= '0' && ch <= '9') {
            M4 m = mx::translate(x0 + k * dw, y, z - dh / 2) * mx::rot_x(PI / 2) * mx::scale(dw, dh, 1);
            r.quad3d(text.digit_tex, m, tint, text.digit_uv(ch - '0'));
        }
    }
}

Hit Scene::pick(const V3& o, const V3& d) const {
    double best = 1e18;
    Hit out;
    auto inv = [](double v) { return 1.0 / (std::fabs(v) < 1e-9 ? 1e-9 : v); };
    double ix = inv(d.x), iy = inv(d.y), iz = inv(d.z);
    for (auto& b : pick_boxes) {
        double t1x = (b.mn.x - o.x) * ix, t2x = (b.mx.x - o.x) * ix;
        double t1y = (b.mn.y - o.y) * iy, t2y = (b.mx.y - o.y) * iy;
        double t1z = (b.mn.z - o.z) * iz, t2z = (b.mx.z - o.z) * iz;
        double tmin = std::max({std::min(t1x, t2x), std::min(t1y, t2y), std::min(t1z, t2z)});
        double tmax = std::min({std::max(t1x, t2x), std::max(t1y, t2y), std::max(t1z, t2z)});
        if (tmax >= std::max(tmin, 0.0) && tmin < best) {
            best = tmin;
            out = b.ref;
        }
    }
    return out;
}
