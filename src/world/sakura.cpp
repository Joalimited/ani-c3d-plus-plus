#include "sakura.hpp"

#include <cmath>
#include <tuple>

#include "../gfx/renderer.hpp"

namespace {
const double EDGE = 0.14;
const double CLEAR[4] = {-72.0, 72.0, -160.0, 22.0};  // x0, x1, z0, z1: where the pedestals live
const double PONDS[2][3] = {{-104.0, -40.0, 16.0}, {112.0, -18.0, 13.0}};  // x, z, radius
const double HOUSES[4][3] = {{-118.0, -118.0, 0.7}, {122.0, -96.0, -0.8}, {-112.0, 62.0, 2.3}, {108.0, 70.0, -2.4}};  // x, z, yaw
const std::vector<C3> PINKS = {{1.0, 0.74, 0.84}, {1.0, 0.66, 0.8}, {0.98, 0.82, 0.9}, {0.95, 0.58, 0.74}, {1.0, 0.88, 0.93}};
const C3 BARK{0.24, 0.13, 0.11}, RED{0.82, 0.16, 0.1}, BLACK{0.06, 0.05, 0.06};
const C3 WOOD{0.46, 0.3, 0.18}, DARKWOOD{0.22, 0.13, 0.09}, PAPER{1.0, 0.93, 0.78}, TILE{0.2, 0.21, 0.25}, STONE{0.5, 0.5, 0.48};
const std::pair<C3, C3> KOI[5] = {{{0.97, 0.96, 0.92}, {0.9, 0.2, 0.08}}, {{1.0, 0.5, 0.08}, {1.0, 0.75, 0.3}}, {{0.95, 0.8, 0.3}, {1.0, 0.95, 0.6}},
                                  {{0.1, 0.1, 0.12}, {0.92, 0.3, 0.1}}, {{0.97, 0.96, 0.92}, {0.1, 0.1, 0.12}}};

// a world-space box joining two points (its long y axis along p0 -> p1)
Box wlimb(V3 p0, V3 p1, double th, C3 col, double glow = 0.1, double edge = EDGE) {
    double dx = p1.x - p0.x, dy = p1.y - p0.y, dz = p1.z - p0.z;
    double ln = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (ln == 0) ln = 1e-6;
    double rz = -std::asin(clampd(dx / ln, -1.0, 1.0));
    double rx = std::atan2(dz / ln, dy / ln);
    return box((p0.x + p1.x) / 2, (p0.y + p1.y) / 2, (p0.z + p1.z) / 2, th, ln + th * 0.5, th, col, glow, 0.0, edge, 0.0, 0.0, rz, rx);
}

// places boxes in a rotated local frame (x right, z toward the front) - used for houses, bridges, gates
struct Local {
    std::vector<Box>& out;
    double x, z, yaw, c, s;
    Local(std::vector<Box>& o, double x_, double z_, double yaw_) : out(o), x(x_), z(z_), yaw(yaw_), c(std::cos(yaw_)), s(std::sin(yaw_)) {}
    std::pair<double, double> p(double lx, double lz) const { return {x + lx * c + lz * s, z - lx * s + lz * c}; }
    void add(double lx, double y, double lz, double sx, double sy, double sz, C3 col, double glow = 0.1, double edge = EDGE, double rz = 0.0, double rx = 0.0) {
        auto [wx, wz] = p(lx, lz);
        out.push_back(box(wx, y, wz, sx, sy, sz, col, glow, yaw, edge, 0.0, 0.0, rz, rx));
    }
};
}  // namespace

Garden::Garden(long long seed) : rnd(seed) {
    static_ = scenery();
    for (int pi = 0; pi < 2; pi++) {
        double pr = PONDS[pi][2];
        for (int k = 0; k < 7; k++) {
            Koi f;
            f.pond = pi;
            f.r = pr * rnd.uniform(0.3, 0.8);
            double w0 = rnd.uniform(0.12, 0.3);
            f.w = w0 * rnd.choice(std::vector<int>{-1, 1});
            f.a = rnd.uniform(0, TAU);
            f.wob = rnd.uniform(0.2, 0.5);
            f.base = KOI[(k + pi) % 5].first;
            f.spot = KOI[(k + pi) % 5].second;
            f.size = rnd.uniform(1.6, 2.4);
            koi.push_back(f);
        }
    }
}

bool Garden::free_(double x, double z, double margin) const {
    if (CLEAR[0] - margin < x && x < CLEAR[1] + margin && CLEAR[2] - margin < z && z < CLEAR[3] + margin) return false;
    for (auto& p : PONDS)
        if (std::hypot(x - p[0], z - p[1]) < p[2] + 3 + margin) return false;
    for (auto& b : blocked)
        if (!(std::hypot(x - b[0], z - b[1]) > b[2] + margin)) return false;
    return true;
}

bool Garden::spot(PyRandom& r, double rmin, double rmax, double margin, int tries, double& x, double& z) const {
    for (int k = 0; k < tries; k++) {
        double a = r.uniform(0, TAU), rad = r.uniform(rmin, rmax);
        x = std::sin(a) * rad;
        z = std::cos(a) * rad;
        if (free_(x, z, margin)) return true;
    }
    return false;
}

// a gnarled trunk that bends as it rises, branches fanning out and a cloud of blossom clusters
void Garden::cherry(std::vector<Box>& out, PyRandom& r, double x, double z, double s) {
    V3 p0{x, 0.0, z};
    double lean = r.uniform(0, TAU);
    V3 p1{x + std::cos(lean) * 0.8 * s, 3.2 * s, z - std::sin(lean) * 0.8 * s};
    V3 p2{p1.x - std::cos(lean) * 0.6 * s, 5.6 * s, p1.z + std::sin(lean) * 0.6 * s};
    out.push_back(wlimb(p0, p1, 1.1 * s, BARK));
    out.push_back(wlimb(p1, p2, 0.85 * s, BARK));
    for (int k = 0; k < 4; k++) {  // root flare
        double a = k * PI / 2 + 0.3;
        out.push_back(wlimb({x, 0.6 * s, z}, {x + std::cos(a) * 1.3 * s, 0.0, z - std::sin(a) * 1.3 * s}, 0.5 * s, BARK));
    }
    std::vector<V3> tips;
    for (int k = 0; k < 6; k++) {
        double a = k / 6.0 * TAU + r.uniform(-0.3, 0.3);
        double ln = r.uniform(3.0, 4.8) * s;
        double my = p2.y + r.uniform(0.8, 1.6) * s;
        V3 mid{p2.x + std::cos(a) * ln * 0.5, my, p2.z - std::sin(a) * ln * 0.5};
        double ty = p2.y + r.uniform(1.0, 2.6) * s;
        V3 tip{p2.x + std::cos(a) * ln, ty, p2.z - std::sin(a) * ln};
        out.push_back(wlimb(p2, mid, 0.5 * s, BARK));
        out.push_back(wlimb(mid, tip, 0.32 * s, BARK));
        tips.push_back(mid);
        tips.push_back(tip);
    }
    for (int k = 0; k < 34; k++) {  // blossom clusters, soft and slightly luminous
        V3 b = r.choice(tips);
        double cs = r.uniform(1.4, 2.6) * s;
        double bx = b.x + r.uniform(-1.4, 1.4) * s;
        double by = b.y + r.uniform(-0.3, 1.2) * s;
        double bz = b.z + r.uniform(-1.4, 1.4) * s;
        C3 col = r.choice(PINKS);
        double yaw = r.uniform(0, 1.5);
        out.push_back(box(bx, by, bz, cs, cs * 0.62, cs, col, 0.45, yaw, 0.08));
    }
    for (int k = 0; k < 6; k++) {  // fallen petals under the tree
        double a = r.uniform(0, TAU), rr = r.uniform(1.5, 6.5) * s;
        double ps = r.uniform(1.2, 2.8);
        C3 col = r.choice(PINKS);
        double yaw = r.uniform(0, 3);
        out.push_back(box(x + std::cos(a) * rr, 0.03, z - std::sin(a) * rr, ps, 0.04, ps * 0.7, col, 0.3, yaw, 0.0));
    }
    blocked.push_back({x, z, 1.6 * s});
}

// a traditional house: stone footings, raised floor, veranda, dark posts, glowing shoji walls, a tiled roof, upturned eaves
void Garden::house(std::vector<Box>& out, double x, double z, double yaw) {
    Local L(out, x, z, yaw);
    const double W = 14.0, D = 10.0;
    for (double lx : {-W / 2 + 0.5, 0.0, W / 2 - 0.5})
        for (double lz : {-D / 2 + 0.5, D / 2 - 0.5}) L.add(lx, 0.35, lz, 0.9, 0.7, 0.9, STONE);
    L.add(0, 0.85, 0, W, 0.3, D, WOOD, 0.1);
    L.add(0, 0.9, D / 2 + 1.0, W + 1.0, 0.16, 2.0, {0.55, 0.38, 0.22}, 0.12);  // engawa
    L.add(0, 0.45, D / 2 + 2.3, 2.2, 0.3, 1.0, STONE);                          // step stone
    const double post_x[6] = {-W / 2, -W / 2 + 3.5, -1.0, 1.0, W / 2 - 3.5, W / 2};
    for (double lx : post_x)
        for (double lz : {-D / 2, D / 2}) L.add(lx, 2.95, lz, 0.32, 4.0, 0.32, DARKWOOD);
    for (double lz : {-D / 2 + 3.3, D / 2 - 3.3})
        for (double lx : {-W / 2, W / 2}) L.add(lx, 2.95, lz, 0.32, 4.0, 0.32, DARKWOOD);
    for (int k = 0; k < 5; k++) {  // front: shoji panels lit from inside, a paper grid on each
        double a = post_x[k], b = post_x[k + 1];
        if (b - a < 2.5) continue;  // the doorway
        double cx = (a + b) / 2, pw = b - a - 0.32;
        L.add(cx, 2.95, D / 2, pw, 3.9, 0.12, PAPER, 0.95, 0.0);
        for (double gy : {1.9, 2.95, 4.0}) L.add(cx, gy, D / 2 + 0.08, pw, 0.05, 0.04, DARKWOOD, 0.05, 0.0);
        for (double gx : {-pw / 3, 0.0, pw / 3}) L.add(cx + gx, 2.95, D / 2 + 0.08, 0.05, 3.9, 0.04, DARKWOOD, 0.05, 0.0);
    }
    L.add(0, 2.4, D / 2 - 0.4, 1.7, 2.9, 0.1, {0.15, 0.08, 0.05}, 0.05, 0.0);  // dark doorway
    for (double k : {-0.45, 0.45}) L.add(k, 4.25, D / 2 + 0.05, 0.85, 1.0, 0.05, {0.12, 0.18, 0.4}, 0.2, 0.0);  // noren curtain
    L.add(0, 4.4, D / 2 + 0.07, 0.4, 0.4, 0.04, {0.95, 0.95, 0.9}, 0.4, 0.0);
    for (double lx : {-W / 2, W / 2}) {  // side walls: plaster panels between dark beams
        L.add(lx, 2.95, 0, 0.14, 3.9, D, {0.92, 0.9, 0.84}, 0.2, 0.0);
        L.add(lx, 4.7, 0, 0.2, 0.3, D, DARKWOOD);
    }
    L.add(0, 2.95, -D / 2, W, 3.9, 0.14, {0.4, 0.27, 0.17}, 0.1);
    L.add(0, 4.95, 0, W + 0.4, 0.3, D + 0.4, DARKWOOD);  // wall plate
    // roof: front and back slopes, hipped ends, a ridge with end tiles, upturned corners and tile courses
    L.add(0, 6.2, D / 4 + 1.2, W + 3.2, 0.35, D / 2 + 3.4, TILE, 0.12, EDGE, 0.0, 0.48);
    L.add(0, 6.2, -D / 4 - 1.2, W + 3.2, 0.35, D / 2 + 3.4, TILE, 0.12, EDGE, 0.0, -0.48);
    for (int k = 0; k < 5; k++) {  // tile courses as faint ridges on the front slope
        double f = (k + 0.5) / 5;
        L.add(0, 7.25 - f * 2.3, 0.4 + f * (D / 2 + 2.4), W + 3.0, 0.08, 0.16, {0.14, 0.15, 0.18}, 0.05, 0.0, 0.0, 0.48);
    }
    for (int sx : {-1, 1}) L.add(sx * (W / 2 + 0.3), 6.3, 0, 3.6, 0.35, D + 1.6, TILE, 0.12, EDGE, -sx * 0.55, 0.0);
    L.add(0, 7.55, 0, W * 0.62, 0.5, 0.75, {0.12, 0.12, 0.15}, 0.12);  // ridge
    for (int sx : {-1, 1}) {
        L.add(sx * W * 0.31, 7.95, 0, 0.6, 0.9, 0.8, {0.12, 0.12, 0.15}, 0.12);  // onigawara end tiles
        for (int sz : {-1, 1}) L.add(sx * (W / 2 + 1.4), 5.25, sz * (D / 2 + 1.6), 1.6, 0.25, 1.6, TILE, 0.12, EDGE, sx * 0.35, -sz * 0.35);  // eave corners
    }
    for (int sx : {-1, 1}) {  // paper lanterns under the front eave (they sway; drawn every frame)
        auto [wx, wz] = L.p(sx * 3.2, D / 2 + 1.6);
        lanterns.push_back({wx, 4.4, wz, {1.0, 0.25, 0.15}, wx * 0.3});
    }
    blocked.push_back({x, z, 10.0});
}

// five tiers, each a body with a wide roof whose corners turn up, and a spire of rings on top
void Garden::pagoda(std::vector<Box>& out, double x, double z, double s) {
    out.push_back(box(x, 0.6 * s, z, 12 * s, 1.2 * s, 12 * s, STONE, 0.1, 0.0, EDGE));
    double y = 1.2 * s;
    for (int k = 0; k < 5; k++) {
        double bw = (8.0 - k * 1.1) * s;
        double bh = 4.2 * s;
        out.push_back(box(x, y + bh / 2, z, bw, bh, bw, {0.75, 0.2, 0.12}, 0.25, 0.0, EDGE));
        out.push_back(box(x, y + bh / 2, z, bw * 0.6, bh * 0.7, bw + 0.05, PAPER, 0.6, 0.0, 0.0));
        double rw = bw + 4.2 * s;
        out.push_back(box(x, y + bh + 0.3 * s, z, rw, 0.6 * s, rw, TILE, 0.15, 0.0, EDGE));
        out.push_back(box(x, y + bh + 0.85 * s, z, rw * 0.62, 0.6 * s, rw * 0.62, TILE, 0.15, 0.0, EDGE));
        for (int sx : {-1, 1})
            for (int sz : {-1, 1})
                out.push_back(box(x + sx * rw * 0.47, y + bh + 0.6 * s, z + sz * rw * 0.47, 1.6 * s, 0.3 * s, 1.6 * s, TILE, 0.15, PI / 4, EDGE, 0.0, 0.0, 0.0, 0.0));
        y += bh + 1.3 * s;
    }
    out.push_back(box(x, y + 4 * s, z, 0.5 * s, 8 * s, 0.5 * s, {0.75, 0.6, 0.25}, 0.6, 0.0, EDGE));
    for (int k = 0; k < 6; k++) out.push_back(box(x, y + 1.5 * s + k * 1.0 * s, z, 1.3 * s, 0.25 * s, 1.3 * s, {0.8, 0.65, 0.25}, 0.6, 0.0, EDGE));
    blocked.push_back({x, z, 9 * s});
}

void Garden::torii(std::vector<Box>& out, double x, double z, double yaw, double s) {
    Local L(out, x, z, yaw);
    for (int sx : {-1, 1}) {
        L.add(sx * 4.5 * s, 5.0 * s, 0, 0.9 * s, 10 * s, 0.9 * s, RED, 0.45);
        L.add(sx * 4.5 * s, 0.4 * s, 0, 1.3 * s, 0.8 * s, 1.3 * s, BLACK, 0.1);
    }
    L.add(0, 7.6 * s, 0, 11.5 * s, 0.6 * s, 0.6 * s, RED, 0.45);    // nuki
    L.add(0, 9.9 * s, 0, 12.5 * s, 0.6 * s, 1.0 * s, RED, 0.45);    // shimaki
    L.add(0, 10.5 * s, 0, 13.2 * s, 0.55 * s, 1.3 * s, BLACK, 0.15);  // kasagi
    for (int sx : {-1, 1}) L.add(sx * 7.2 * s, 10.75 * s, 0, 2.0 * s, 0.5 * s, 1.3 * s, BLACK, 0.15, EDGE, sx * 0.3);  // the kasagi's ends sweep up
    L.add(0, 8.8 * s, 0.1, 1.3 * s, 1.8 * s, 0.4 * s, BLACK, 0.2);  // gakuzuka plaque
    L.add(0, 8.8 * s, 0.32, 0.9 * s, 1.3 * s, 0.05, {0.85, 0.7, 0.3}, 0.6, 0.0);
}

void Garden::stone_lantern(std::vector<Box>& out, double x, double z, double s) {
    out.push_back(box(x, 0.3 * s, z, 1.6 * s, 0.6 * s, 1.6 * s, STONE, 0.1, 0.4, EDGE));
    out.push_back(box(x, 1.4 * s, z, 0.6 * s, 1.8 * s, 0.6 * s, STONE, 0.1, 0.4, EDGE));
    out.push_back(box(x, 2.5 * s, z, 1.3 * s, 0.35 * s, 1.3 * s, STONE, 0.1, 0.4, EDGE));
    out.push_back(box(x, 3.2 * s, z, 1.0 * s, 1.0 * s, 1.0 * s, STONE, 0.1, 0.4, EDGE));
    out.push_back(box(x, 3.2 * s, z, 1.06 * s, 0.5 * s, 0.5 * s, {1.0, 0.8, 0.4}, 1.8, 0.4, 0.0));  // lit windows
    out.push_back(box(x, 3.2 * s, z, 0.5 * s, 0.5 * s, 1.06 * s, {1.0, 0.8, 0.4}, 1.8, 0.4, 0.0));
    out.push_back(box(x, 3.95 * s, z, 1.9 * s, 0.4 * s, 1.9 * s, STONE, 0.1, 0.4, EDGE));
    out.push_back(box(x, 4.35 * s, z, 0.9 * s, 0.4 * s, 0.9 * s, STONE, 0.1, 0.4, EDGE));
    out.push_back(box(x, 4.75 * s, z, 0.35 * s, 0.45 * s, 0.35 * s, STONE, 0.1, 0.4, EDGE));
    blocked.push_back({x, z, 1.2 * s});
}

void Garden::pond(std::vector<Box>& out, PyRandom& r, double px, double pz, double pr) {
    for (int k = 0; k < 3; k++) {
        out.push_back(box(px, 0.03, pz, pr * 2, 0.06, pr * 2 * 0.42, {0.06, 0.22, 0.27}, 0.3, k * PI / 3, 0.0));
        out.push_back(box(px, 0.05, pz, pr * 1.65, 0.06, pr * 1.65 * 0.42, {0.08, 0.3, 0.36}, 0.4, k * PI / 3 + 0.5, 0.0));
    }
    for (int k = 0; k < 30; k++) {  // rounded rocks around the edge, some mossy
        double a = k / 30.0 * TAU + r.uniform(-0.05, 0.05);
        double s = r.uniform(0.7, 1.5);
        C3 col = k % 3 ? STONE : C3(0.35, 0.45, 0.3);
        out.push_back(box(px + std::cos(a) * (pr + 0.6), 0.32 * s, pz - std::sin(a) * (pr + 0.6), 1.4 * s, 0.64 * s, 1.0 * s, col, 0.08, a, EDGE));
    }
    for (int k = 0; k < 10; k++) {  // lily pads and petals floating on the water
        double a = r.uniform(0, TAU), rr = r.uniform(0, pr * 0.85);
        double lx = px + std::cos(a) * rr, lz = pz - std::sin(a) * rr;
        if (k % 2) {
            for (int q = 0; q < 3; q++) out.push_back(box(lx, 0.1, lz, 1.3, 0.04, 0.55, {0.22, 0.5, 0.2}, 0.2, q * PI / 3 + a, 0.0));
        } else {
            C3 col = r.choice(PINKS);
            out.push_back(box(lx, 0.09, lz, 0.5, 0.03, 0.35, col, 0.5, a, 0.0));
        }
    }
    for (int k = 0; k < 12; k++) {  // irises on the bank
        double a = r.uniform(0, TAU);
        double rr = pr + r.uniform(1.4, 2.6);
        double ix = px + std::cos(a) * rr, iz = pz - std::sin(a) * rr;
        out.push_back(box(ix, 0.9, iz, 0.08, 1.8, 0.08, {0.25, 0.5, 0.2}, 0.1, 0.0, 0.0));
        out.push_back(box(ix + 0.15, 0.7, iz, 0.12, 1.4, 0.04, {0.3, 0.55, 0.22}, 0.1, a, 0.0, 0.0, 0.0, 0.2));
        for (int q = 0; q < 3; q++) out.push_back(box(ix, 1.85, iz, 0.55, 0.06, 0.18, {0.5, 0.3, 0.85}, 0.5, q * PI / 3, 0.0, 0.0, 0.0, 0.0, 0.3));
    }
}

// a red taiko (drum) bridge: planks following an arch, posts and a handrail that follow it too
void Garden::bridge(std::vector<Box>& out, double x, double z, double yaw, double span, double rise) {
    Local L(out, x, z, yaw);
    const int n = 16;
    std::vector<std::pair<double, double>> pts;
    for (int k = 0; k <= n; k++) {
        double u = 2.0 * k / n - 1;
        pts.emplace_back(-span / 2 + span * k / n, 0.3 + rise * (1 - u * u));
    }
    for (int k = 0; k < n; k++) {
        auto [x0, y0] = pts[k];
        auto [x1, y1] = pts[k + 1];
        double ang = std::atan2(y1 - y0, x1 - x0);
        double len = std::hypot(x1 - x0, y1 - y0) + 0.05;
        L.add((x0 + x1) / 2, (y0 + y1) / 2, 0, len, 0.3, 3.2, {0.55, 0.32, 0.18}, 0.12, EDGE, ang);
        for (int sz : {-1, 1}) {  // side beams and the rail
            L.add((x0 + x1) / 2, (y0 + y1) / 2 - 0.25, sz * 1.65, len, 0.35, 0.25, RED, 0.45, EDGE, ang);
            L.add((x0 + x1) / 2, (y0 + y1) / 2 + 1.25, sz * 1.65, len, 0.18, 0.2, RED, 0.45, EDGE, ang);
        }
    }
    for (int k = 0; k <= n; k += 2) {
        auto [px_, py_] = pts[k];
        for (int sz : {-1, 1}) {
            L.add(px_, py_ + 0.6, sz * 1.65, 0.25, (0 < k && k < n) ? 1.4 : 1.9, 0.25, RED, 0.45);
            if (k == 0 || k == n) L.add(px_, py_ + 1.65, sz * 1.65, 0.35, 0.35, 0.35, {0.85, 0.7, 0.3}, 0.8);  // brass caps
        }
    }
}

void Garden::bamboo(std::vector<Box>& out, PyRandom& r, double x, double z) {
    const std::vector<C3> greens = {{0.38, 0.6, 0.2}, {0.45, 0.65, 0.22}, {0.32, 0.55, 0.18}};
    long long n = r.randint(12, 18);
    for (long long k = 0; k < n; k++) {
        double a = r.uniform(0, TAU), rr = r.uniform(0, 3.0);
        double bx = x + std::cos(a) * rr, bz = z - std::sin(a) * rr;
        double h = r.uniform(9, 15);
        double lean = r.uniform(-0.06, 0.06);
        C3 g = r.choice(greens);
        out.push_back(box(bx, h / 2, bz, 0.3, h, 0.3, g, 0.15, a, 0.1, 0.0, 0.0, lean));
        for (int q = 0;; q++) {  // nodes (np.arange(1.4, h, 1.5))
            double yy = 1.4 + q * 1.5;
            if (yy >= h) break;
            out.push_back(box(bx - std::sin(lean) * yy, yy, bz, 0.38, 0.08, 0.38, {0.6, 0.72, 0.35}, 0.2, a, 0.0));
        }
        for (int q = 0; q < 4; q++) {  // leaf sprays near the top
            double la = r.uniform(0, TAU);
            double y = h - r.uniform(0, 2.5);
            out.push_back(box(bx + std::cos(la) * 0.9, y, bz - std::sin(la) * 0.9, 1.8, 0.05, 0.3, {0.35, 0.6, 0.2}, 0.2, la, 0.0, 0.0, 0.0, -0.3));
        }
    }
    blocked.push_back({x, z, 4.0});
}

// a raked gravel garden: a framed bed of pale gravel with straight rake lines and rings around three rocks
void Garden::zen(std::vector<Box>& out, double x, double z, double yaw) {
    Local L(out, x, z, yaw);
    const double W = 20.0, D = 13.0;
    L.add(0, 0.06, 0, W, 0.12, D, {0.82, 0.8, 0.74}, 0.15, 0.0);
    for (int sx : {-1, 1}) L.add(sx * W / 2, 0.15, 0, 0.4, 0.3, D + 0.4, DARKWOOD);
    for (int sz : {-1, 1}) L.add(0, 0.15, sz * D / 2, W + 0.4, 0.3, 0.4, DARKWOOD);
    const double rocks[3][3] = {{-5.0, -1.5, 1.6}, {3.5, 2.0, 1.1}, {6.5, -3.0, 0.8}};
    for (int k = -11; k < 12; k++) L.add(0, 0.13, k * 0.55, W - 0.6, 0.03, 0.08, {0.68, 0.66, 0.6}, 0.05, 0.0);
    for (auto& rk : rocks) {
        double rx = rk[0], rz = rk[1], rs = rk[2];
        for (double ring : {rs + 0.6, rs + 1.2, rs + 1.8})
            for (int q = 0; q < 24; q++) {
                double a = q / 24.0 * TAU;
                L.add(rx + std::cos(a) * ring, 0.14, rz + std::sin(a) * ring, ring * 0.28, 0.03, 0.09, {0.86, 0.84, 0.78}, 0.1, 0.0);
            }
        L.add(rx, 0.7 * rs, rz, 2.0 * rs, 1.4 * rs, 1.6 * rs, {0.38, 0.38, 0.38}, 0.08);
        L.add(rx + 0.2, 1.3 * rs, rz, 1.2 * rs, 0.6 * rs, 1.0 * rs, {0.34, 0.42, 0.3}, 0.1);
    }
    blocked.push_back({x, z, 12.0});
}

// Mount Fuji on the horizon: stepped blue-grey slopes and a snow cap
void Garden::fuji(std::vector<Box>& out) {
    double x = 60.0, z = -330.0;
    const int n = 30;
    for (int k = 0; k < n; k++) {  // many thin layers, so the slope reads as a smooth concave cone
        double f = (double)k / n;
        double w = 280 * std::pow(1 - f, 1.6) + 14;
        double y = 4 + k * 3.2;
        bool snow = k >= n - 9;
        C3 col = snow ? C3(0.95, 0.95, 1.0) : C3(0.32 - 0.1 * f, 0.33 - 0.08 * f, 0.48 - 0.05 * f);
        out.push_back(box(x, y, z, w, 3.4, w * 0.35, col, snow ? 0.5 : 0.15, 0.0, 0.0));
    }
    out.push_back(box(-80, 72, -300, 30, 30, 2, {1.0, 0.82, 0.7}, 1.5, 0.0, 0.0, 0.0, 1.0));  // low evening sun
    out.push_back(box(-80, 72, -301, 44, 44, 1, {0.7, 0.42, 0.5}, 0.8, 0.0, 0.0, 0.0, 1.0));
}

std::vector<Box> Garden::scenery() {
    PyRandom r(3);
    std::vector<Box> out;
    for (auto& h : HOUSES) house(out, h[0], h[1], h[2]);
    pagoda(out, -40.0, -215.0, 1.25);
    torii(out, 35.0, -185.0, 0.15, 1.2);
    torii(out, PONDS[0][0] + 6, PONDS[0][1] + PONDS[0][2] + 8, 0.4, 0.9);
    for (auto& p : PONDS) pond(out, r, p[0], p[1], p[2]);
    bridge(out, PONDS[0][0], PONDS[0][1], 0.35, PONDS[0][2] * 2 + 3, 3.4);
    for (auto& p : PONDS)  // stone lanterns watching each pond
        for (double a : {0.8, 2.6, 4.4}) stone_lantern(out, p[0] + std::cos(a) * (p[2] + 4), p[1] - std::sin(a) * (p[2] + 4), 1.1);
    zen(out, 108.0, -150.0, -0.5);
    fuji(out);
    // signature cherries framing the clearing, then a blossoming woodland further out
    const double sig[6][3] = {{-86, 12, 1.5}, {88, -8, 1.45}, {-84, -150, 1.4}, {86, -168, 1.5}, {-48, 44, 1.15}, {52, 46, 1.2}};
    for (auto& c : sig)
        if (free_(c[0], c[1], 2)) cherry(out, r, c[0], c[1], c[2]);
    double x, z;
    for (int i = 0; i < 55; i++)
        if (spot(r, 45, 220, 5, 30, x, z)) {
            double s = r.uniform(0.9, 1.35);
            cherry(out, r, x, z, s);
        }
    for (int i = 0; i < 7; i++)
        if (spot(r, 60, 200, 5, 30, x, z)) bamboo(out, r, x, z);
    for (int i = 0; i < 40; i++) {  // trimmed round azalea bushes, some in flower
        if (!spot(r, 30, 150, 1.5, 30, x, z)) continue;
        C3 g{0.18, 0.4, 0.15};
        for (int k = 0; k < 3; k++) out.push_back(box(x + (k - 1) * 0.6, 0.7, z, 1.8, 1.4, 1.8, g, 0.12, k * 0.5, EDGE));
        if (i % 2 == 0)
            for (int k = 0; k < 8; k++) {
                double a = r.uniform(0, TAU);
                double y = r.uniform(0.9, 1.5);
                out.push_back(box(x + std::cos(a) * 1.0, y, z - std::sin(a) * 1.0, 0.3, 0.2, 0.3, {1.0, 0.45, 0.65}, 0.8, a, 0.0));
            }
    }
    for (int i = 0; i < 30; i++) {  // mossy rocks
        if (spot(r, 30, 170, 1.0, 30, x, z)) {
            double s = r.uniform(0.8, 2.0);
            double y1 = r.uniform(0, 2);
            out.push_back(box(x, 0.45 * s, z, 1.8 * s, 0.9 * s, 1.4 * s, STONE, 0.08, y1, EDGE));
            double y2 = r.uniform(0, 2);
            out.push_back(box(x, 0.92 * s, z, 1.4 * s, 0.2 * s, 1.1 * s, {0.3, 0.45, 0.22}, 0.1, y2, EDGE));
        }
    }
    for (int i = 0; i < 260; i++) {  // grass tufts
        if (spot(r, 24, 180, 0.0, 4, x, z)) {
            for (int k = 0; k < 3; k++) {
                double a = k * 2.1 + r.uniform(0, 1);
                double h = r.uniform(0.6, 1.2);
                out.push_back(box(x + std::cos(a) * 0.1, h / 2, z - std::sin(a) * 0.1, 0.07, h, 0.03, {0.3, 0.52, 0.18}, 0.08, a, 0.0, 0, 0, 0.0, 0.25));
            }
        }
    }
    for (auto& h : HOUSES) {  // stepping-stone paths from the clearing out to each house
        double hx = h[0], hz = h[1];
        double d = std::hypot(hx, hz);
        for (int k = 0; k < (int)(d / 3.2); k++) {
            double f = k * 3.2 / d;
            double sx = hx * f, sz = hz * f;
            bool inside = CLEAR[0] < sx && sx < CLEAR[1] && CLEAR[2] < sz && sz < CLEAR[3];
            if (free_(sx, sz, -1.0) || !inside) {
                bool skip = false;
                for (auto& b : blocked)
                    if (std::hypot(sx - b[0], sz - b[1]) < b[2] - 2) skip = true;
                if (skip) continue;
                double ox = r.uniform(-0.4, 0.4), oz = r.uniform(-0.4, 0.4), yaw = r.uniform(0, 1);
                out.push_back(box(sx + ox, 0.06, sz + oz, 1.8, 0.12, 1.4, {0.55, 0.54, 0.5}, 0.1, yaw, EDGE));
            }
        }
    }
    for (int side : {-1, 1}) {  // lantern lines on poles along the sides of the clearing
        for (int k = 0; k < 7; k++) {
            double lz = 10 - k * 26;
            double lx = side * 80;
            out.push_back(box(lx, 3.5, lz, 0.3, 7.0, 0.3, DARKWOOD, 0.1, 0.0, EDGE));
            if (k < 6) {
                out.push_back(box(lx, 6.6, lz - 13, 0.05, 0.05, 26, BLACK, 0.0, 0.0, 0.0));
                for (int q = 0; q < 3; q++)
                    lanterns.push_back({lx, 6.1, lz - 6.5 * (q + 1), (k + q) % 2 ? C3(1.0, 0.95, 0.85) : C3(1.0, 0.3, 0.2), k * 1.3 + q});
            }
        }
    }
    return out;
}

std::vector<Box> Garden::boxes(double t, const V3& cam, Renderer* r) {
    if (r) r->add_boxes(static_);
    std::vector<Box> out;
    for (auto& L : lanterns) {  // paper lanterns swaying in the breeze
        double sw = 0.12 * std::sin(t * 1.3 + L.ph);
        double lx = L.x + std::sin(sw) * 0.5;
        out.push_back(box(lx, L.y, L.z, 1.0, 1.25, 1.0, L.col, 1.5, sw, 0.0, 0.0, 0.0, sw));
        out.push_back(box(lx, L.y + 0.7, L.z, 0.75, 0.16, 0.75, BLACK, 0.1, sw, 0.0));
        out.push_back(box(lx, L.y - 0.7, L.z, 0.75, 0.16, 0.75, BLACK, 0.1, sw, 0.0));
        for (double k : {-0.3, 0.0, 0.3})  // ribs
            out.push_back(box(lx, L.y + k, L.z, 1.04, 0.04, 1.04, L.col.g < 0.5 ? C3(0.5, 0.15, 0.1) : C3(0.8, 0.7, 0.55), 0.6, sw, 0.0));
    }
    for (auto& f : koi) {  // koi gliding in loops just under the surface, tails swishing
        double px = PONDS[f.pond][0], pz = PONDS[f.pond][1];
        double a = f.a + t * f.w;
        double rr = f.r * (1 + 0.15 * std::sin(t * 0.3 + f.wob * 10));
        double x = px + std::cos(a) * rr, z = pz - std::sin(a) * rr * (0.8 + 0.2 * f.wob);
        double head = a + (f.w > 0 ? PI / 2 : -PI / 2);
        double sz = f.size;
        double sw = std::sin(t * 6 + f.wob * 20);
        double fx = std::cos(head), fz = -std::sin(head);   // forward
        double sx_ = std::sin(head), sz_ = std::cos(head);  // sideways (local +z)
        const std::tuple<double, double, C3> segs[4] = {{0.55, 0.42, f.base}, {0.0, 0.5, f.spot}, {-0.55, 0.36, f.base}, {-1.0, 0.2, f.base}};
        for (int k = 0; k < 4; k++) {
            auto [off, w_, col] = segs[k];
            double lat = sw * 0.07 * k * k * sz;  // the body bends more toward the tail
            double bx = x + fx * off * sz + sx_ * lat, bz = z + fz * off * sz + sz_ * lat;
            out.push_back(box(bx, 0.12, bz, 0.6 * sz, 0.14 * sz, w_ * sz, col, 0.35, head + sw * 0.15 * k, 0.0));
        }
        double tail = head + sw * 0.6;
        double tx = x - fx * 1.35 * sz + sx_ * sw * 0.7 * sz, tz = z - fz * 1.35 * sz + sz_ * sw * 0.7 * sz;
        out.push_back(box(tx, 0.12, tz, 0.5 * sz, 0.04, 0.55 * sz, f.spot, 0.5, tail, 0.0));
        for (int sd : {-1, 1})  // pectoral fins
            out.push_back(box(x + std::cos(head) * 0.3 * sz + std::sin(head) * sd * 0.3 * sz, 0.11, z - std::sin(head) * 0.3 * sz + std::cos(head) * sd * 0.3 * sz,
                              0.3 * sz, 0.03, 0.22 * sz, f.base, 0.3, head + sd * (0.5 + 0.2 * sw), 0.0));
    }
    for (int i = 0; i < 140; i++) {  // cherry petals drifting down on the breeze
        double fall = 0.8 + hrand(i, 1) * 0.8;
        double period = 16.0 / fall;
        double u = pmod(t + hrand(i, 2) * period, period) / period;
        double y = 16 * (1 - u);
        double ang = t * (0.8 + hrand(i, 3)) + i;
        double x = cam.x + (hrand(i, 4) - 0.5) * 130 + std::sin(ang * 0.6) * 3 + u * 6;  // a steady breeze carries them sideways
        double z = cam.z + (hrand(i, 5) - 0.5) * 130 + std::cos(ang * 0.5) * 3;
        if (dist3({x, y, z}, cam) < 8) continue;
        out.push_back(box(x, y + 0.05, z, 0.32, 0.03, 0.24, PINKS[i % PINKS.size()], 0.6, ang, 0.0, 0.0, 0.0, std::sin(ang * 1.9), std::cos(ang * 1.4)));
    }
    return out;
}
