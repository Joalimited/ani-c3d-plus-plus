#include "effects.hpp"

#include <cmath>
#include <memory>

#include "../games/games.hpp"
#include "../gfx/renderer.hpp"
#include "../gfx/text.hpp"
#include "dinos.hpp"
#include "forest.hpp"
#include "haunt.hpp"
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
static std::vector<Box> matrix(Renderer& r, TextCache& text, double t, const V3& cam) {
    std::vector<Box> out;
    for (int i = 0; i < 130; i++) {
        double x = cam.x + (hrand(i, 1) - 0.5) * 170;
        double z = cam.z + (hrand(i, 2) - 0.5) * 170;
        double speed = 7 + hrand(i, 3) * 12;
        double y = 48 - pmod(t * speed + hrand(i, 4) * 60, 60);
        if (std::hypot(x - cam.x, z - cam.z) < 16) continue;  // keep the rain off the camera lens
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

// a big striped synthwave sun on the horizon plus blinking pixel stars
static std::vector<Box> retro(double t, const V3& cam) {
    std::vector<Box> out;
    double cx = cam.x, cz = cam.z - 280;
    for (int k = 0; k < 11; k++) {
        double f = k / 10.0;
        double y = 6 + f * 70;
        double width = (0 <= y - 6 && y - 6 <= 80) ? 2 * std::sqrt(std::max(0.0, 46.0 * 46.0 - (y - 6 - 40) * (y - 6 - 40))) : 0;
        if (width <= 1 || (k % 2 == 0 && k < 5)) continue;
        out.push_back(box(cx, y, cz, width, 5.2, 1.5, {1.0, 0.25 + 0.7 * f, 0.55 - 0.45 * f}, 1.0));
    }
    for (int i = 0; i < 60; i++) {
        double a = hrand(i, 1) * TAU;
        double rad = 120 + hrand(i, 2) * 200;
        out.push_back(box(cam.x + std::sin(a) * rad, 30 + hrand(i, 3) * 90, cam.z - std::fabs(std::cos(a)) * rad - 40, 0.8, 0.8, 0.8, {1, 1, 1},
                          ((long long)std::floor(t * 2 + i)) % 3 ? 1.2 : 0.2));
    }
    return out;
}

// floating VHS cassettes (the screen overlay with scanlines and tracking noise is drawn by the HUD)
static std::vector<Box> vcr(double t, const V3& cam) {
    std::vector<Box> out;
    for (int i = 0; i < 16; i++) {
        double a = t * (0.05 + hrand(i, 1) * 0.07) + i * 1.7;
        double rad = 20 + hrand(i, 2) * 70;
        double x = cam.x * 0.3 + std::sin(a) * rad, z = cam.z * 0.3 + std::cos(a) * rad;
        double y = 3 + hrand(i, 3) * 10 + std::sin(t * 0.9 + i) * 0.8;
        if (dist3({x, y, z}, cam) < 16) continue;
        double yaw = a * 2 + t * 0.4, tilt = 0.3 * std::sin(t * 0.7 + i);
        out.push_back(box(x, y, z, 3.6, 0.7, 2.2, {0.06, 0.06, 0.08}, 0.15, yaw, 0.5, tilt));
        out.push_back(box(x, y + 0.36, z, 2.6, 0.06, 1.0, {0.9, 0.9, 0.8}, 0.6, yaw, 0.3, tilt));                       // white label
        out.push_back(box(x, y + 0.4, z, 2.6, 0.1, 0.25, {1.0, 0.2 + 0.3 * hrand(i, 4), 0.2}, 0.9, yaw, 0.3, tilt));  // coloured stripe
        for (int sgn : {-1, 1})  // the two reel hubs
            out.push_back(box(x + sgn * 0.8 * std::cos(yaw), y + 0.4, z - sgn * 0.8 * std::sin(yaw), 0.6, 0.12, 0.6, {0.75, 0.75, 0.8}, 0.5, yaw + t * sgn, 0.3, tilt));
    }
    for (int i = 0; i < 7; i++) {  // tracking-error bars drifting across the sky
        double y = pmod(t * (3 + i) + i * 31, 70) + 2;
        out.push_back(box(cam.x, y, cam.z - 150, 380, 0.5 + hrand(i, 5) * 1.2, 0.4, {0.8, 0.85, 1.0}, 0.5 + hrand(i, 6), 0.0, 0.0));
    }
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
    if (th.fx == "matrix") return matrix(r, text, t, cam);
    if (th.fx == "retro") return retro(t, cam);
    if (th.fx == "vcr") return vcr(t, cam);
    return {};
}
}  // namespace effects
