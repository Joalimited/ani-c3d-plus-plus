#include "effects.hpp"

#include <cmath>
#include <memory>

#include "../games/games.hpp"
#include "../gfx/renderer.hpp"
#include "../gfx/text.hpp"
#include "dinos.hpp"
#include "forest.hpp"
#include "haunt.hpp"
#include "props.hpp"
#include "sakura.hpp"

namespace effects {
static Herd& the_herd() {
    static Herd h;
    return h;
}
static Haunt& the_haunt() {
    static Haunt h;
    return h;
}
static Forest& the_forest() {  // built on first use (the scenery takes a moment)
    static Forest f;
    return f;
}
static Garden& the_garden() {
    static Garden g;
    return g;
}

Creatures& herd() { return the_herd(); }

// lightning level (Halloween only): a double flicker every 8.5 seconds
double flash(const Theme& th, double t) {
    if (th.fx != "halloween") return 0.0;
    double ph = pmod(t, 8.5);
    if (ph < 0.12) return 0.7;
    if (0.22 < ph && ph < 0.4) return 0.5 * (1 - (ph - 0.22) / 0.18);
    return 0.0;
}

Creatures* creatures(const Theme& th) {
    if (th.fx == "jurassic") return &the_herd();
    if (th.fx == "halloween") return &the_haunt();
    if (th.fx == "forest") return &the_forest();
    return nullptr;
}

static void billboard_tex(Renderer& r, const TexP& tex, V3 pos, double w, double h, double yaw, C4 tint, UV uv) {
    M4 m = mx::translate(pos.x, pos.y, pos.z) * mx::rot_y(yaw) * mx::translate(-w / 2, h, 0) * mx::scale(w, -h, 1);
    r.quad3d(tex, m, tint, uv);
}

// a rolling jack-o'-lantern (axle along z): round from three rotated slabs, ribs and a carved face on each side
static std::vector<RollPart> pumpkin_parts(double R, double t, int i) {
    C3 orange{1.0, 0.45, 0.0}, rib{0.7, 0.25, 0.0}, lit{1.0, 0.85, 0.2};
    double flick = 1.4 + 0.4 * std::sin(t * 9 + i * 3) * std::sin(t * 5.3 + i);  // candle flicker
    double d = 1.7 * R;
    std::vector<RollPart> P;
    for (double a : {0.0, PI / 3, 2 * PI / 3}) P.push_back({0, 0, 0, 2 * R, 2 * R * 0.56, d, orange, 0.75, a});
    for (double a : {0.0, PI / 3, 2 * PI / 3}) P.push_back({0, 0, 0, 2 * R * 1.01, 0.09 * R, d * 0.92, rib, 0.4, a + PI / 6});  // grooves
    P.push_back({0, R * 1.02, 0, 0.26 * R, 0.5 * R, 0.26 * R, {0.25, 0.55, 0.1}, 0.3, 0.25});  // stem
    for (int sz : {-1, 1}) {
        double f = sz * (d / 2 + 0.02);
        P.push_back({-0.35 * R, 0.25 * R, f, 0.32 * R, 0.32 * R, 0.04, lit, flick, PI / 4});
        P.push_back({0.35 * R, 0.25 * R, f, 0.32 * R, 0.32 * R, 0.04, lit, flick, PI / 4});
        P.push_back({0, -0.05 * R, f, 0.16 * R, 0.16 * R, 0.04, lit, flick, PI / 4});
        P.push_back({0, -0.4 * R, f, 0.95 * R, 0.16 * R, 0.04, lit, flick, 0.0});
        for (int sx : {-1, 1}) P.push_back({sx * 0.28 * R, -0.3 * R, f, 0.13 * R, 0.14 * R, 0.04, lit, flick, 0.0});  // jagged teeth
    }
    return P;
}

// digital rain: a falling bright glyph with a glowing trail, wrapped around the camera
static std::vector<Box> matrix(Renderer& r, TextCache& text, double t, const V3& cam, const World& world) {
    std::vector<Box> out;
    int drops = world.z_clip < 1e20 ? 280 : 130;  // the TV only gets the half behind the menus: more streams
    for (int i = 0; i < drops; i++) {
        double x = cam.x + (hrand(i, 1) - 0.5) * 170;
        double z = cam.z + (hrand(i, 2) - 0.5) * 170;
        double speed = 7 + hrand(i, 3) * 12;
        double y = 48 - pmod(t * speed + hrand(i, 4) * 60, 60);
        if (std::hypot(x - cam.x, z - cam.z) < 16) continue;  // keep the rain off the camera lens
        if (z > world.z_clip) continue;
        out.push_back(box(x, y + 4.0, z, 0.07, 8, 0.07, {0.0, 0.6, 0.15}, 0.25));
        double yaw = std::atan2(cam.x - x, cam.z - z);
        int digit = (int)pmod(std::floor(t * 9 + i * 7), 10);
        billboard_tex(r, text.digit_tex, {x, y - 0.4, z}, 1.5, 2.1, yaw, C4(0.8, 1.0, 0.85, 1.0), text.digit_uv(digit));
    }
    return out;
}

static std::vector<Box> fireflies(double t, const V3& cam) {
    std::vector<Box> out;
    for (int i = 0; i < 70; i++) {
        double x = cam.x + (hrand(i, 1) - 0.5) * 90 + std::sin(t * 0.5 + i) * 2;
        double z = cam.z + (hrand(i, 2) - 0.5) * 90 + std::cos(t * 0.4 + i * 1.3) * 2;
        double y = 1 + hrand(i, 3) * 9 + std::sin(t * 0.8 + i * 2) * 1.5;
        double blink = 0.5 + 0.5 * std::sin(t * 2.1 + i * 3);
        out.push_back(box(x, y, z, 0.22, 0.22, 0.22, {1.0, 0.85, 0.2}, 0.6 + 1.6 * blink));
    }
    return out;
}

static std::vector<Box> halloween(Renderer& r, double t, const V3& cam, const World& world) {
    std::vector<Box> out;
    for (int i = 0; i < 12; i++) {  // jack-o'-lanterns rolling along the ground in big loops (rolling without slipping)
        double w = (0.05 + hrand(i) * 0.06) * (i % 2 ? 1 : -1);
        double rad = 22 + hrand(i, 5) * 55;
        double a = t * w + i * 1.9;
        double R = 0.95 + 0.25 * hrand(i, 6);  // pumpkin radius
        double x = cam.x * 0.3 + std::sin(a) * rad, z = cam.z * 0.3 + std::cos(a) * rad;
        if (dist3({x, R, z}, cam) < 14) continue;
        double yaw = w > 0 ? a : a + PI;  // +x of the pumpkin points along its path
        double roll = -t * std::fabs(w) * rad / R;  // distance rolled / radius (always forward)
        double hop = 0.12 * std::fabs(std::sin(roll * 1.5));  // lumpy pumpkins bump a little as they roll
        auto b = roller(x, R + hop, z, yaw, roll, pumpkin_parts(R, t, i));
        out.insert(out.end(), b.begin(), b.end());
    }
    for (int i = 0; i < 14; i++) {  // bats
        double a = t * (0.35 + hrand(i, 7) * 0.3) + i * 2.4;
        double rad = 12 + hrand(i, 8) * 40;
        double x = std::sin(a) * rad, z = std::cos(a) * rad;
        double y = 6 + hrand(i, 9) * 9 + std::sin(t * 2 + i) * 1.2;
        double flap = 0.4 + 0.6 * std::fabs(std::sin(t * 14 + i));
        out.push_back(box(x, y, z, 0.5, 0.3, 0.5, {0.05, 0.0, 0.08}, 0.0, -a));
        out.push_back(box(x, y + 0.2 * flap, z, 2.2, 0.25, 0.7 * flap + 0.3, {0.10, 0.0, 0.16}, 0.0, -a));
    }
    for (int i = 0; i < 40; i++) {  // drifting candle flames and mist wisps near the ground
        double a = t * (0.05 + hrand(i, 11) * 0.06) + i * 0.7;
        double rad = 15 + hrand(i, 12) * 90;
        double x = cam.x * 0.5 + std::sin(a) * rad, z = cam.z * 0.5 + std::cos(a) * rad;
        out.push_back(box(x, 0.8 + 0.4 * std::sin(t * 2 + i), z, 0.35, 0.6 + 0.2 * std::sin(t * 9 + i), 0.35, {0.8, 1.0, 0.5}, 1.8, 0.0, 0.0));
    }
    the_haunt().step(t, world);
    auto hb = the_haunt().boxes(t, cam, &r);
    out.insert(out.end(), hb.begin(), hb.end());
    return out;
}

// a round striped synthwave sun sinking into the horizon (fixed in the world, drawn flat: no neon outlines) plus blinking pixel stars
static std::vector<Box> retro(double t, const V3& cam, const World& world) {
    std::vector<Box> out;
    const double R = 62, cx = 0, cy = 22, cz = -185;
    const int N = 40;
    const double slice = 2 * R / N;
    for (int i = 0; i < N; i++) {
        double f = (i + 0.5) / N;  // 0 = bottom edge .. 1 = top
        double dy = (f - 0.5) * 2 * R;
        double half = std::sqrt(std::max(0.0, R * R - dy * dy));
        if (half < 1.0) continue;
        if (f < 0.5) {  // the classic gaps, thicker toward the bottom
            int k = (int)((0.5 - f) * N);
            if ((k % 4) >= std::max(1, 3 - k / 6)) continue;
        }
        C3 top{1.0, 0.95, 0.3}, mid{1.0, 0.5, 0.2}, bot{1.0, 0.1, 0.5};
        double u = f > 0.5 ? (f - 0.5) * 2 : f * 2;
        C3 a = f > 0.5 ? mid : bot, b = f > 0.5 ? top : mid;
        C3 c{a.r + (b.r - a.r) * u, a.g + (b.g - a.g) * u, a.b + (b.b - a.b) * u};
        out.push_back(box(cx, cy + dy, cz, 2 * half, slice * 1.03, 2.0, c, 1.8, 0.0, 0.0));
    }
    for (int i = 0; i < 60; i++) {
        double a = hrand(i, 1) * TAU;
        double rad = 120 + hrand(i, 2) * 200;
        double z = cam.z - std::fabs(std::cos(a)) * rad - 40;
        out.push_back(box(cam.x + std::sin(a) * rad, 30 + hrand(i, 3) * 90, z, 0.8, 0.8, 0.8, {1, 1, 1}, ((long long)std::floor(t * 2 + i)) % 3 ? 1.2 : 0.2));
    }
    (void)world;
    return out;
}

// flying props orbit loosely around the scene: VHS tapes, audio cassettes, Walkmans and Macintoshes (the screen overlay with
// scanlines and tracking noise is drawn by the HUD)
static void fly(std::vector<Box>& out, const std::vector<Part>& base, int i, double t, const V3& cam, const World& world, double r_min, double r_span, double edge) {
    double a = t * (0.05 + hrand(i, 1) * 0.07) + i * 1.7;
    double rad = r_min + hrand(i, 2) * r_span;
    double x = cam.x * 0.3 + std::sin(a) * rad, z = cam.z * 0.3 + std::cos(a) * rad;
    double y = 3 + hrand(i, 3) * 11 + std::sin(t * 0.9 + i) * 0.9;
    if (dist3({x, y, z}, cam) < 16 || z > world.z_clip) return;
    std::vector<Part> P = base;
    props::tilt(P, 0.22 * std::sin(t * 0.7 + i), 0.3 * std::sin(t * 0.5 + i * 1.3));
    double k = world.z_clip < 1e20 ? 1.7 : 1.0;  // bigger on the TV, where they sit far behind the menus
    place(out, P, x, y, z, a * 2 + t * 0.4, k, edge);
}

static std::vector<Box> vcr(double t, const V3& cam, const World& world) {
    std::vector<Box> out;
    for (int i = 0; i < 9; i++) {
        C3 label{1.0, 0.2 + 0.5 * hrand(i, 4), 0.2 + 0.3 * hrand(i, 6)};
        fly(out, props::vhs(label, 1.5), i, t, cam, world, 20, 70, 0.7);
    }
    for (int i = 0; i < 8; i++) {
        C3 shell{0.15 + 0.6 * hrand(i + 40, 1), 0.15 + 0.6 * hrand(i + 40, 2), 0.2 + 0.6 * hrand(i + 40, 3)};
        fly(out, props::cassette(shell, {0.95, 0.92, 0.8}, t + i, 1.3), i + 40, t, cam, world, 18, 60, 0.7);
    }
    for (int i = 0; i < 4; i++) fly(out, props::walkman(t + i, 1.2), i + 80, t, cam, world, 18, 55, 0.7);
    for (int i = 0; i < 3; i++) fly(out, props::macintosh(t + i, 1.2), i + 120, t, cam, world, 22, 55, 0.7);
    for (int i = 0; i < 7; i++) {  // tracking-error bars drifting across the sky
        double y = pmod(t * (3 + i) + i * 31, 70) + 2;
        out.push_back(box(cam.x, y, cam.z - 150, 380, 0.5 + hrand(i, 5) * 1.2, 0.4, {0.8, 0.85, 1.0}, 0.5 + hrand(i, 6), 0.0, 0.0));
    }
    return out;
}

// Windows Media Center (2005): soft blue haze, rising orbs and a flock of butterflies drifting in lazy loops
static std::vector<Box> mce(double t, const V3& cam, const World& world) {
    std::vector<Box> out;
    const C3 UP[] = {{0.12, 0.5, 1.0}, {0.2, 0.62, 1.0}, {0.1, 0.38, 0.9}};
    const C3 LOW[] = {{0.35, 0.85, 0.25}, {1.0, 0.55, 0.1}, {0.9, 0.2, 0.25}, {0.5, 0.9, 0.35}};
    for (int i = 0; i < 12; i++) {
        auto pos = [&](double tt, double& x, double& y, double& z) {
            double a = tt * (0.12 + 0.05 * hrand(i, 1)) + i * 2.1;
            x = cam.x * 0.2 + std::sin(a) * (14 + 16 * hrand(i, 2)) + std::sin(a * 2.3 + i) * 3;
            y = 4 + 10 * hrand(i, 3) + std::sin(a * 1.7 + i) * 3;
            z = cam.z * 0.2 - 8 - 22 * hrand(i, 4) + std::cos(a * 1.3) * 10;
        };
        double x, y, z, x2, y2, z2;
        pos(t, x, y, z);
        pos(t + 0.15, x2, y2, z2);
        if (z > world.z_clip) continue;
        double heading = std::atan2(-(z2 - z), x2 - x);
        auto P = props::butterfly(t, i * 1.7, UP[i % 3], LOW[i % 4], 1.6);
        props::tilt(P, 0.35 * std::atan2(y2 - y, std::hypot(x2 - x, z2 - z)) * 6, 0.0);
        place(out, P, x, y, z, heading, world.z_clip < 1e20 ? 1.5 : 1.0, 0.8);
    }
    for (int i = 0; i < 24; i++) {  // glowing bubbles rising through the blue
        double u = pmod(t * (0.04 + hrand(i, 1) * 0.04) + hrand(i, 2), 1.0);
        double x = (hrand(i, 3) - 0.5) * 70, z = -6 - hrand(i, 4) * 40;
        if (z > world.z_clip) continue;
        double s = 0.25 + 0.5 * hrand(i, 5);
        out.push_back(box(x + std::sin(t * 0.6 + i) * 1.2, 1 + u * 26, z, s, s, s, i % 3 ? C3(0.4, 0.8, 1.0) : C3(0.55, 1.0, 0.4), 1.2, t * 0.5, 0.2));
    }
    for (int k = 0; k < 3; k++)  // slow light ribbons across the back, like the Media Center start screen's waves
        out.push_back(box(cam.x, 10 + 4 * k + std::sin(t * 0.3 + k * 2) * 2.5, cam.z - 170 - k * 20, 420, 0.8 + 0.3 * k, 0.5, k % 2 ? C3(0.2, 0.6, 1.0) : C3(0.3, 0.9, 0.4), 0.6, 0.0, 0.0));
    return out;
}

std::vector<Box> build(const Theme& th, Renderer& r, TextCache& text, double t, const V3& cam, const World* world_in, double& flash_out) {
    World dflt;
    const World& world = world_in ? *world_in : dflt;
    flash_out = 0.0;
    if (th.fx == "jurassic") {  // fireflies plus a herd of wandering dinosaurs
        auto out = fireflies(t, cam);
        the_herd().step(t, world);
        auto hb = the_herd().boxes(t, cam, &r);
        out.insert(out.end(), hb.begin(), hb.end());
        return out;
    }
    if (th.fx == "halloween") {
        flash_out = flash(th, t);
        return halloween(r, t, cam, world);
    }
    if (th.fx == "sakura") return the_garden().boxes(t, cam, &r);
    if (th.fx == "forest") {
        the_forest().step(t, world);
        return the_forest().boxes(t, cam, &r);
    }
    if (th.fx == "matrix") return matrix(r, text, t, cam, world);
    if (th.fx == "retro") return retro(t, cam, world);
    if (th.fx == "vcr") return vcr(t, cam, world);
    if (th.fx == "mce") return mce(t, cam, world);
    return {};
}
}  // namespace effects
