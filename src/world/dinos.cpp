// Each dinosaur is a small rig built from boxes: legs plant their feet on the ground and bend at the knee to reach them
// (two-bone IK), tails and necks are chains of tapering segments that sway, jaws hinge, wings flap in three parts.
#include "dinos.hpp"

#include <algorithm>
#include <cmath>
#include <map>

#include "../gfx/renderer.hpp"

namespace {
const double SCALE = 1.25;
const double EDGE = 0.4;  // softer neon outline than the world's boxes, so they read as solid animals
const C3 WHITE{0.95, 0.93, 0.85}, DARK{0.04, 0.03, 0.03};

struct Pose {
    std::vector<Part> parts;
    void add(double x, double y, double z, double sx, double sy, double sz, C3 col, double glow = 0.25, double rz = 0, double rx = 0, double yl = 0) {
        parts.push_back({x, y, z, sx, sy, sz, col, glow, rz, rx, yl});
    }
    // a bone starting at (x,y,z) pointing `ang` radians up from forward and `psi` sideways; returns its end point
    V3 seg(double x, double y, double z, double length, double ang, double thick, double wid, C3 col, double glow = 0.25, double psi = 0, double rx = 0) {
        double ca = std::cos(ang), sa = std::sin(ang), cp = std::cos(psi), sp = std::sin(psi);
        double dx = ca * cp, dy = sa, dz = -ca * sp;
        parts.push_back({x + dx * length / 2, y + dy * length / 2, z + dz * length / 2, length, thick, wid, col, glow, ang, rx, psi});
        return {x + dx * length, y + dy * length, z + dz * length};
    }
    void eye(double x, double y, double z, double size = 0.32) {
        for (int sz : {-1, 1}) {
            add(x, y, z + sz * size * 1.6, size, size, size * 0.5, WHITE, 0.6);
            add(x + size * 0.18, y, z + sz * size * 1.95, size * 0.5, size * 0.55, size * 0.3, DARK, 0.0);
        }
    }
    void teeth(double x0, double y, int count, double spacing, std::vector<double> zs, double size = 0.16) {
        for (int i = 0; i < count; i++)
            for (double z : zs) add(x0 + i * spacing, y, z, size, size * 1.5, size, WHITE, 0.5);
    }
};

const double STRIDE = 0.6 * TAU / (1.24 * SCALE);  // stride that keeps a planted foot still while the body walks over it
const double STANCE = 0.6;                         // share of the gait cycle a foot spends on the ground

// where the foot is along the body (offset from the hip) and how high it is lifted, at gait phase `ph`
void gait(double ph, double mv, double hip_h, double lift, double& off, double& up, double& swing) {
    double stride = std::min(STRIDE, hip_h) * mv;
    double u = pmod(ph / TAU, 1.0);
    if (u < STANCE) {
        off = stride * (0.5 - u / STANCE);
        up = swing = 0;
        return;
    }
    double v = (u - STANCE) / (1 - STANCE);
    double upk = std::sin(PI * v);
    off = stride * (-0.5 + smooth01(v));
    up = lift * hip_h * mv * upk;
    swing = upk * mv;
}

// two-bone leg in the side plane: angles of the upper and lower bone that put the joint end on (ax, ay)
void ik(double hx, double hy, double ax, double ay, double l1, double l2, double knee, double& a1, double& a2) {
    double dx = ax - hx, dy = ay - hy;
    double d = std::min(std::max(std::hypot(dx, dy), std::fabs(l1 - l2) + 1e-3), l1 + l2 - 1e-3);
    double base = std::atan2(dy, dx);
    double a = std::acos(std::min(1.0, std::max(-1.0, (l1 * l1 + d * d - l2 * l2) / (2 * l1 * d))));
    a1 = base + knee * a;
    double kx = hx + l1 * std::cos(a1), ky = hy + l1 * std::sin(a1);
    a2 = std::atan2(ay - ky, ax - kx);
}

// a leg whose foot really stands on the ground (y=0)
void leg(Pose& P, double ph, double mv, double hx, double hy, double hz, double tw, C3 col, std::optional<C3> dark_ = std::nullopt, bool biped = true,
         double knee = 1, bool claws = true, bool sickle = false, double lift = 0.22) {
    C3 dark = dark_ ? *dark_ : col;
    double off, up, swing;
    gait(ph, mv, hy, lift, off, up, swing);
    if (biped) {
        double meta = 0.3 * hy, toe = 0.26 * hy;
        double l1 = 0.44 * hy, l2 = 0.4 * hy;
        double bx = hx + off - 0.05 * hy, by = up;  // where the toes start
        double lean = 0.42 + 0.5 * swing;            // the ankle bone leans back, more while the foot is carried
        double ax = bx - meta * std::sin(lean), ay = by + meta * std::cos(lean);
        double a1, a2;
        ik(hx, hy, ax, ay, l1, l2, knee, a1, a2);
        V3 k = P.seg(hx, hy, hz, l1, a1, tw, tw, col);
        P.seg(k.x, k.y, k.z, l2, a2, tw * 0.72, tw * 0.75, dark);
        P.seg(ax, ay, hz, meta, -PI / 2 + lean, tw * 0.45, tw * 0.5, dark);
        P.add(bx, by + tw * 0.12, hz, tw * 0.5, tw * 0.42, tw * 0.6, dark);  // the ball of the foot
        double tth = tw * 0.24;
        double droop = -0.5 * swing;  // toes curl down a little in the air
        const double psis[3] = {-0.38, 0.0, 0.38};
        for (int kk = 0; kk < 3; kk++) {
            double ln = toe * (kk == 1 ? 1.0 : 0.78);
            V3 e = P.seg(bx, by + tth / 2, hz, ln, droop, tth, tth * 1.1, dark, 0.25, psis[kk]);
            if (claws) P.seg(e.x - 0.03, e.y, e.z, tw * 0.32, droop - 0.6, tth * 0.7, tth * 0.6, WHITE, 0.5, psis[kk]);
        }
        if (sickle) P.seg(bx + 0.05, by + tth, hz, tw * 0.75, 1.0, tth * 0.8, tth * 0.6, WHITE, 0.8);  // the raptor's killing claw
    } else {
        double pad = tw * 0.32;
        double l1 = (hy - pad) * 0.53, l2 = l1;
        double ax = hx + off, ay = up + pad;
        double a1, a2;
        ik(hx, hy, ax, ay, l1, l2, knee, a1, a2);
        V3 k = P.seg(hx, hy, hz, l1, a1, tw, tw, col);
        P.seg(k.x, k.y, k.z, l2, a2, tw * 0.82, tw * 0.85, dark);
        P.add(ax, up + pad / 2, hz, tw * 1.05, pad, tw * 1.05, dark);
        P.add(ax, up + pad / 2, hz, tw * 0.75, pad * 1.01, tw * 1.25, dark, 0.25, 0.0, 0.0, PI / 4);  // rounder pad
        if (claws)
            for (double dz : {-0.32, 0.0, 0.32}) P.add(ax + tw * 0.5, up + pad * 0.4, hz + dz * tw, tw * 0.18, pad * 0.7, tw * 0.2, WHITE, 0.5);
    }
}

void tail(Pose& P, double t, double x, double y, int n, double length, double thick0, double thick1, C3 col, std::optional<C3> belly = std::nullopt,
          double sway = 0.25, double droop = 0.06, double lag = 0.0, double wave = 2.2, double wid_k = 1.0) {
    double px = x, py = y, pz = 0.0;
    for (int i = 0; i < n; i++) {
        double f = (double)i / std::max(n - 1, 1);
        double th = thick0 + (thick1 - thick0) * f;
        double psi = sway * std::sin(t * wave - i * 0.7) * (i + 1) / n + lag * (i + 1) / n;
        double ang = -PI - droop * 0.5 + (0.12 * std::sin(t * 1.3 - i * 0.5)) * f - 0.04 * i * (1 - f);
        V3 e = P.seg(px, py, pz, length, ang, th, th * wid_k, (i % 2 == 0 || !belly) ? col : *belly, 0.2, psi);
        px = e.x;
        py = e.y;
        pz = e.z;
    }
}

// returns end x, y, z and the last angle
std::array<double, 4> neck(Pose& P, double t, double x, double y, int n, double length, double ang0, double ang1, double th0, double th1, C3 col,
                           double look = 0, double lift = 0, double wid_k = 1.0) {
    double px = x, py = y, pz = 0.0, ang = ang0;
    for (int i = 0; i < n; i++) {
        double f = (double)i / std::max(n - 1, 1);
        ang = ang0 + (ang1 - ang0) * f + lift * (1 - f * 0.4) + 0.05 * std::sin(t * 1.2 - i * 0.6);
        double th = th0 + (th1 - th0) * f;
        V3 e = P.seg(px, py, pz, length, ang, th, th * wid_k, col, 0.25, look * (i + 1) / n);
        px = e.x;
        py = e.y;
        pz = e.z;
    }
    return {px, py, pz, ang};
}

struct St {
    double t, ph, mv, roar, look, graze, turn, flap, seed;
};

// ---------------------------------------------------------------- species rigs
void rig_trex(Pose& P, const St& st, const std::array<C3, 3>& pal) {
    C3 back = pal[0], belly = pal[1];
    double t = st.t, ph = st.ph, mv = st.mv, r = st.roar, look = st.look, graze = st.graze;
    double bob = 0.16 * std::fabs(std::sin(ph)) * mv + 0.05 * std::sin(t * 1.6);
    double hy = 5.0 + bob + 0.35 * r;
    P.add(-0.6, hy + 0.6, 0, 3.0, 2.8, 2.5, back, 0.25, -0.05);
    P.add(1.8, hy + 0.9, 0, 2.8, 2.7, 2.3, back, 0.25, 0.1);
    P.add(0.4, hy - 0.4, 0, 5.2, 0.9, 2.0, belly, 0.3, 0.0);
    auto [nx, ny, nz, nang] = neck(P, t, 3.0, hy + 1.7, 3, 1.15, 0.65, 0.05, 1.8, 1.5, back, look, r * 0.5 - graze * 0.9);
    double hx = nx;
    P.add(hx + 0.9, ny + 0.1, nz, 2.7, 1.8, 2.0, back, 0.3, nang * 0.4, 0.0, look);
    P.add(hx + 2.3, ny - 0.1, nz, 1.7, 1.1, 1.4, back, 0.3, nang * 0.3, 0.0, look);
    double jaw = -(0.08 + 0.75 * r + 0.1 * std::sin(t * 6) * mv);
    P.seg(hx + 0.1, ny - 0.75, nz, 3.2, jaw, 0.8, 1.5, belly, 0.3, look);
    P.teeth(hx + 1.6, ny - 0.55, 6, 0.38, {-0.55, 0.55});
    P.teeth(hx + 0.9 + 0.1, ny - 0.95 + jaw * 0.5, 5, 0.38, {-0.5, 0.5});
    P.eye(hx + 1.3, ny + 0.55, nz, 0.34);
    for (int sz : {-1, 1}) {  // tiny arms
        P.seg(2.9, hy + 0.2, sz * 1.15, 0.9, -0.9 - 0.3 * std::sin(t * 2), 0.38, 0.38, back);
        P.seg(3.4, hy - 0.5, sz * 1.15, 0.7, -1.5, 0.3, 0.3, belly);
    }
    tail(P, t, -2.0, hy + 0.8, 9, 1.35, 2.0, 0.35, back, belly, 0.28, 0.03, st.turn);
    int k = 0;
    for (int sz : {-1, 1}) leg(P, ph + (k++) * PI, mv, 0.1, hy - 0.25, sz * 1.15, 1.25, back, belly);
}

void rig_trike(Pose& P, const St& st, const std::array<C3, 3>& pal) {
    C3 back = pal[0], belly = pal[1], acc = pal[2];
    double t = st.t, ph = st.ph, mv = st.mv, r = st.roar, look = st.look, graze = st.graze;
    double hy = 3.4 + 0.1 * std::fabs(std::sin(ph)) * mv;
    P.add(0, hy + 0.9, 0, 5.6, 3.1, 3.4, back, 0.25);
    P.add(0, hy - 0.4, 0, 5.0, 0.9, 3.0, belly, 0.3);
    P.add(-2.3, hy + 0.9, 0, 1.5, 2.6, 2.8, back, 0.25);
    double hx = 3.4 + 0.15 * r;
    double hyd = hy + 0.2 - 0.6 * graze + 0.2 * r;
    P.add(hx, hyd + 0.2, 0, 2.5, 2.0, 2.2, back, 0.3, -0.1, 0.0, look);
    P.add(hx + 1.5, hyd - 0.3, 0, 1.2, 1.1, 1.3, belly, 0.3, -0.1, 0.0, look);  // beak
    P.add(hx - 1.1, hyd + 2.0, 0, 0.55, 4.0, 4.4, acc, 0.4, 0.5, 0.0, look);    // frill
    for (int k = 0; k < 7; k++) {
        double a = -1.0 + k * 0.33;
        P.add(hx - 1.4, hyd + 2.3 + 1.6 * std::cos(a * 1.5), 2.3 * std::sin(a * 1.2) * 1.0 + 0.0, 0.5, 0.5, 0.5, WHITE, 0.6);
    }
    for (int sz : {-1, 1}) P.seg(hx + 0.2, hyd + 0.9, sz * 0.75, 2.3, 0.55, 0.38, 0.38, WHITE, 0.6, sz * 0.12);  // brow horns
    P.seg(hx + 1.6, hyd + 0.3, 0, 0.9, 0.9, 0.5, 0.5, WHITE, 0.6);
    P.eye(hx + 0.6, hyd + 0.5, 0, 0.3);
    tail(P, t, -3.0, hy + 1.0, 4, 1.2, 1.6, 0.5, back, belly, 0.18);
    const double L[4][3] = {{1.9, -1, 0.0}, {1.9, 1, PI}, {-1.9, -1, PI}, {-1.9, 1, 0.0}};
    for (auto& l : L) leg(P, ph + l[2], mv, l[0], hy - 0.2, l[1] * 1.25, 1.15, back, belly, false, l[0] > 0 ? -1 : 1);
}

void rig_brachio(Pose& P, const St& st, const std::array<C3, 3>& pal) {
    C3 back = pal[0], belly = pal[1];
    double t = st.t, ph = st.ph, mv = st.mv, r = st.roar, look = st.look, graze = st.graze;
    double hy = 6.6 + 0.08 * std::fabs(std::sin(ph)) * mv + 0.05 * std::sin(t * 1.2);
    P.add(0, hy + 0.9, 0, 6.2, 3.6, 3.4, back, 0.22);
    P.add(0, hy - 0.5, 0, 5.6, 0.9, 3.0, belly, 0.3);
    auto [nx, ny, nz, nang] = neck(P, t, 3.1, hy + 1.8, 8, 1.75, 1.15, 0.2, 2.0, 0.9, back, look * 1.4, r * 0.25 - graze * 1.35);
    P.add(nx + 0.7, ny + 0.1, nz, 1.9, 1.15, 1.2, back, 0.3, nang * 0.2, 0.0, look);
    P.add(nx + 1.6, ny - 0.15, nz, 0.9, 0.7, 0.9, belly, 0.3, -0.2, 0.0, look);
    P.eye(nx + 0.9, ny + 0.4, nz, 0.26);
    tail(P, t, -3.1, hy + 1.2, 9, 1.6, 2.0, 0.4, back, belly, 0.3, 0.1, st.turn, 1.6);
    const double L[4][3] = {{2.2, -1, 0.0}, {2.2, 1, PI}, {-2.2, -1, PI}, {-2.2, 1, 0.0}};
    for (auto& l : L) leg(P, ph + l[2], mv, l[0], hy - 0.3, l[1] * 1.3, 1.5, back, belly, false, l[0] > 0 ? -1 : 1, true, false, 0.1);
}

void rig_stego(Pose& P, const St& st, const std::array<C3, 3>& pal) {
    C3 back = pal[0], belly = pal[1], acc = pal[2];
    double t = st.t, ph = st.ph, mv = st.mv, look = st.look, graze = st.graze;
    double hy = 3.0 + 0.1 * std::fabs(std::sin(ph)) * mv;
    for (double x : {-2.4, -0.8, 0.8, 2.4}) {  // an arched back
        double top = hy + 1.2 + 1.1 * std::cos((x + 0.4) * 0.55) * (1 - 0.0);
        P.add(x, top - 0.3, 0, 1.9, 2.3, 2.6, back, 0.25, -0.2 * std::sin(x * 0.5));
    }
    P.add(0, hy, 0, 5.6, 0.9, 2.3, belly, 0.3);
    for (int i = 0; i < 9; i++) {  // two staggered rows of plates
        double x = -2.9 + i * 0.72;
        double top = hy + 2.6 + 1.1 * std::cos((x + 0.4) * 0.55) - 0.3;
        double size = 1.3 + 0.9 * std::sin(PI * (i + 0.5) / 9);
        P.add(x, top + size * 0.35, 0.45 * (i % 2 ? 1 : -1), size, size, 0.26, acc, 0.9, PI / 4, 0.0, 0.0);
    }
    double hd = hy + 0.2 - 0.5 * graze;
    P.seg(3.0, hy + 1.5, 0, 1.5, -0.35 - graze * 0.5, 1.1, 1.2, back, 0.25, look * 0.5);
    P.add(4.4, hd, 0, 1.7, 0.95, 1.1, back, 0.3, -0.2, 0.0, look);
    P.eye(4.2, hd + 0.3, 0, 0.22);
    double tx = -3.2, ty = hy + 1.0, tz = 0.0;
    for (int i = 0; i < 4; i++) {
        double th = 1.5 - i * 0.28;
        double psi = 0.22 * std::sin(t * 2 - i * 0.8) * (i + 1) / 4 + st.turn * (i + 1) / 4;
        V3 e = P.seg(tx, ty, tz, 1.3, -PI - 0.05 + 0.1 * i, th, th, back, 0.2, psi);
        tx = e.x;
        ty = e.y;
        tz = e.z;
    }
    for (int k = 0; k < 4; k++) {  // the thagomizer
        int sz = k % 2 == 0 ? -1 : 1;
        P.seg(tx - 0.2 - 0.25 * (k / 2), ty, tz + sz * 0.3, 1.7, sz > 0 ? 0.5 + 0.25 * (k / 2) : 0.5, 0.28, 0.28, WHITE, 0.7, sz * (0.35 + 0.1 * (k / 2)));
    }
    const double L[4][4] = {{1.9, -1, 0.0, 0.5}, {1.9, 1, PI, 0.5}, {-1.9, -1, PI, 0.0}, {-1.9, 1, 0.0, 0.0}};
    for (auto& l : L) leg(P, ph + l[2], mv, l[0], hy - 0.15 - l[3], l[1] * 1.25, 0.95, back, belly, false, l[0] > 0 ? -1 : 1);  // short front legs
}

void rig_raptor(Pose& P, const St& st, const std::array<C3, 3>& pal) {
    C3 back = pal[0], belly = pal[1], acc = pal[2];
    double t = st.t, ph = st.ph, mv = st.mv, r = st.roar, look = st.look, graze = st.graze;
    double hy = 2.5 + 0.2 * std::fabs(std::sin(ph)) * mv;
    P.add(0, hy + 0.4, 0, 2.7, 1.2, 1.0, back, 0.3, 0.08);
    P.add(0.1, hy - 0.2, 0, 2.4, 0.5, 0.8, belly, 0.35);
    for (int k = 0; k < 4; k++) P.add(-0.9 + k * 0.7, hy + 0.7, 0, 0.25, 1.25, 1.05, acc, 0.4, 0.08);  // stripes
    auto [nx, ny, nz, nang] = neck(P, t, 1.3, hy + 0.6, 2, 0.8, 0.9, 0.3, 0.6, 0.5, back, look, r * 0.4 - graze * 0.6);
    P.add(nx + 0.5, ny, nz, 1.3, 0.75, 0.8, back, 0.35, nang * 0.3, 0.0, look);
    P.add(nx + 1.2, ny - 0.1, nz, 0.9, 0.45, 0.55, back, 0.35, -0.2, 0.0, look);
    double jaw = -(0.05 + 0.45 * r + 0.25 * std::max(0.0, std::sin(t * 9)) * mv);
    P.seg(nx + 0.1, ny - 0.4, nz, 1.5, jaw, 0.3, 0.55, belly, 0.35, look);
    P.teeth(nx + 0.6, ny - 0.3, 4, 0.25, {-0.2, 0.2}, 0.1);
    P.eye(nx + 0.6, ny + 0.2, nz, 0.17);
    for (int k = 0; k < 3; k++) P.seg(nx - 0.3, ny + 0.4, nz, 0.7, 0.8 + 0.3 * k, 0.12, 0.16, acc, 0.6, (k - 1) * 0.2);  // head crest of feathers
    tail(P, t, -1.3, hy + 0.4, 8, 0.85, 0.6, 0.12, back, acc, 0.12, 0.0, st.turn, 3.0);
    int k = 0;
    for (int sz : {-1, 1}) leg(P, ph + (k++) * PI, mv, 0.1, hy - 0.1, sz * 0.5, 0.5, back, belly, true, 1, true, true, 0.3);
}

void rig_anky(Pose& P, const St& st, const std::array<C3, 3>& pal) {
    C3 back = pal[0], belly = pal[1], acc = pal[2];
    double t = st.t, ph = st.ph, mv = st.mv, look = st.look, graze = st.graze;
    double hy = 1.9 + 0.05 * std::fabs(std::sin(ph)) * mv;
    P.add(0, hy + 0.5, 0, 5.0, 1.8, 4.0, back, 0.25);
    P.add(0, hy - 0.3, 0, 4.6, 0.7, 3.5, belly, 0.3);
    for (int i = 0; i < 4; i++)  // rows of armour bumps
        for (int j = 0; j < 5; j++) P.add(-1.8 + i * 1.2, hy + 1.55 + 0.1 * (i % 2), -1.6 + j * 0.8, 0.8, 0.6, 0.7, acc, 0.45, 0.0, 0.0, 0.0);
    for (int sz : {-1, 1})
        for (int i = 0; i < 4; i++) P.seg(-1.7 + i * 1.1, hy + 0.3, sz * 1.95, 0.9, 0.2, 0.3, 0.3, WHITE, 0.6, sz * 1.1);
    double hd = hy + 0.1 - 0.4 * graze;
    P.add(3.1, hd, 0, 2.2, 1.2, 2.5, back, 0.3, 0.0, 0.0, look);
    P.eye(3.7, hd + 0.4, 0, 0.26);
    for (int sz : {-1, 1}) P.add(2.7, hd + 0.8, sz * 1.3, 0.7, 0.7, 0.5, WHITE, 0.6);
    double tx = -2.6, ty = hy + 0.4, tz = 0.0;
    for (int i = 0; i < 3; i++) {
        double psi = 0.35 * std::sin(t * 1.8 - i * 0.8) * (i + 1) / 3 + st.turn * (i + 1) / 3;
        V3 e = P.seg(tx, ty, tz, 1.5, -PI - 0.02, 1.2 - i * 0.2, 1.8 - i * 0.4, back, 0.25, psi);
        tx = e.x;
        ty = e.y;
        tz = e.z;
    }
    P.add(tx - 0.9, ty, tz + 0.35 * std::sin(t * 1.8 - 2.4), 1.6, 1.4, 2.0, acc, 0.6);  // the club
    const double L[4][3] = {{1.8, -1, 0.0}, {1.8, 1, PI}, {-1.8, -1, PI}, {-1.8, 1, 0.0}};
    for (auto& l : L) leg(P, ph + l[2], mv, l[0], hy - 0.3, l[1] * 1.5, 1.0, back, belly, false, l[0] > 0 ? -1 : 1, true, false, 0.18);
}

void rig_para(Pose& P, const St& st, const std::array<C3, 3>& pal) {
    C3 back = pal[0], belly = pal[1], acc = pal[2];
    double t = st.t, ph = st.ph, mv = st.mv, r = st.roar, look = st.look, graze = st.graze;
    double hy = 3.9 + 0.14 * std::fabs(std::sin(ph)) * mv;
    P.add(0, hy + 0.5, 0, 4.2, 2.3, 2.0, back, 0.25, 0.06);
    P.add(0, hy - 0.4, 0, 3.8, 0.7, 1.7, belly, 0.3);
    auto [nx, ny, nz, nang] = neck(P, t, 2.0, hy + 1.3, 3, 1.0, 0.95, 0.35, 1.2, 0.9, back, look, r * 0.5 - graze * 1.2);
    P.add(nx + 0.7, ny, nz, 1.9, 1.0, 1.2, back, 0.3, nang * 0.2, 0.0, look);
    P.add(nx + 1.7, ny - 0.15, nz, 1.2, 0.55, 1.5, belly, 0.3, -0.1, 0.0, look);  // duck bill
    P.eye(nx + 0.8, ny + 0.3, nz, 0.26);
    double cx = nx - 0.2, cy = ny + 0.4, cz = 0.0;
    for (double a : {0.9, 0.35, -0.3, -0.9}) {  // the hollow crest sweeps back
        V3 e = P.seg(cx, cy, cz, 1.1, a, 0.34, 0.34, acc, 0.7, look * 0.5);
        cx = e.x;
        cy = e.y;
        cz = e.z;
    }
    for (int sz : {-1, 1}) P.seg(2.0, hy + 0.5, sz * 1.05, 1.1, -0.9, 0.4, 0.4, back);
    tail(P, t, -2.1, hy + 0.7, 7, 1.25, 1.3, 0.3, back, belly, 0.2, 0.05, st.turn, 2.0);
    int k = 0;
    for (int sz : {-1, 1}) leg(P, ph + (k++) * PI, mv, 0.0, hy - 0.2, sz * 0.9, 1.0, back, belly, true, 1, false);  // hadrosaur: blunt toes
}

void rig_ptero(Pose& P, const St& st, const std::array<C3, 3>& pal) {
    C3 back = pal[0], belly = pal[1], acc = pal[2];
    double t = st.t;
    double beat = std::sin(t * st.flap);
    double glide = 0.35 + 0.65 * smooth01(0.5 + 0.5 * std::sin(t * 0.7 + st.seed));  // flaps in bursts, glides in between
    double flap = beat * 0.5 * glide - 0.05;
    double bob = 0.25 * beat * glide;
    P.add(0, bob, 0, 2.2, 0.8, 0.9, back, 0.3);
    P.add(0, bob - 0.15, 0, 1.8, 0.4, 0.8, belly, 0.3);
    P.add(1.5, bob + 0.2, 0, 1.0, 0.7, 0.7, back, 0.3, 0.2);
    P.seg(1.9, bob + 0.2, 0, 2.6, -0.05, 0.28, 0.32, acc, 0.5);  // long beak
    P.seg(0.7, bob + 0.5, 0, 1.8, 0.4, 0.22, 0.25, acc, 0.6);    // crest
    P.eye(1.65, bob + 0.4, 0, 0.2);
    P.add(-1.7, bob, 0, 1.3, 0.25, 0.25, back, 0.3);
    for (int sz : {-1, 1}) {  // three wing segments: each tilts a little more than the one before, so the wing bends as it flaps
        double x = 0.2, y = bob + 0.2, z = sz * 0.4;
        const double segs[3][2] = {{1.9, 2.0}, {2.5, 1.6}, {2.6, 1.1}};
        for (int k = 0; k < 3; k++) {
            double ln = segs[k][0], wd = segs[k][1];
            double phi = flap * (1.0 + 0.3 * k) + 0.06 * k;  // elevation: up is positive, both wings flap together
            double dy = std::sin(phi), dz = sz * std::cos(phi);
            P.parts.push_back({x - 0.15 * k, y + dy * ln / 2, z + dz * ln / 2, wd, 0.12, ln, k == 2 ? acc : back, 0.35, 0.0, -sz * phi, 0.0});
            y += dy * ln;
            z += dz * ln;
        }
    }
}

using RigFn = void (*)(Pose&, const St&, const std::array<C3, 3>&);
const std::map<std::string, RigFn> RIGS = {{"trex", rig_trex},     {"trike", rig_trike}, {"brachio", rig_brachio}, {"stego", rig_stego},
                                           {"raptor", rig_raptor}, {"anky", rig_anky},   {"para", rig_para},       {"ptero", rig_ptero}};

struct Species {
    const char *name, *diet, *length, *era, *fact;
    double speed, radius, pick_h, pick_r;
    std::pair<double, double> foot;
    std::array<C3, 3> pal;
};
const std::map<std::string, Species> SPECIES = {
    {"trex", {"Tyrannosaurus rex", "carnivore", "12 m", "Late Cretaceous", "Its bite was the strongest of any land animal ever.", 4.2, 6.5, 6.0, 7.0, {13, 5},
              {C3{0.58, 0.22, 0.12}, C3{0.88, 0.58, 0.26}, C3{0.95, 0.7, 0.3}}}},
    {"trike", {"Triceratops", "herbivore", "9 m", "Late Cretaceous", "The frill and three horns defended against T. rex.", 3.0, 5.5, 3.8, 5.5, {11, 5},
               {C3{0.42, 0.52, 0.22}, C3{0.78, 0.7, 0.4}, C3{0.72, 0.38, 0.30}}}},
    {"brachio", {"Brachiosaurus", "herbivore", "22 m", "Late Jurassic", "Its raised neck reached the tops of tall conifers.", 2.2, 8.0, 8.5, 10.0, {16, 6},
                 {C3{0.38, 0.52, 0.66}, C3{0.72, 0.8, 0.86}, C3{0.6, 0.7, 0.8}}}},
    {"stego", {"Stegosaurus", "herbivore", "9 m", "Late Jurassic", "The four tail spikes are called the thagomizer.", 2.6, 5.5, 3.8, 5.5, {11, 5},
               {C3{0.42, 0.5, 0.2}, C3{0.8, 0.74, 0.45}, C3{1.0, 0.55, 0.15}}}},
    {"raptor", {"Velociraptor", "carnivore", "2 m", "Late Cretaceous", "Turkey-sized and feathered, it hunted in quick bursts.", 8.5, 2.8, 2.2, 3.0, {5, 2},
                {C3{0.9, 0.46, 0.1}, C3{0.95, 0.85, 0.6}, C3{0.18, 0.1, 0.05}}}},
    {"anky", {"Ankylosaurus", "herbivore", "8 m", "Late Cretaceous", "A living tank with a bony tail club.", 2.0, 4.5, 2.4, 4.6, {10, 5},
              {C3{0.6, 0.48, 0.3}, C3{0.85, 0.74, 0.5}, C3{0.72, 0.58, 0.36}}}},
    {"para", {"Parasaurolophus", "herbivore", "10 m", "Late Cretaceous", "Its hollow crest worked like a trumpet.", 3.5, 4.5, 4.5, 5.5, {11, 3},
              {C3{0.2, 0.58, 0.58}, C3{0.78, 0.85, 0.7}, C3{0.98, 0.78, 0.22}}}},
    {"ptero", {"Pteranodon", "piscivore", "6 m wingspan", "Late Cretaceous", "A toothless flyer that skimmed the seas for fish.", 0.0, 0.0, 0.0, 4.5, {5, 8},
               {C3{0.5, 0.26, 0.34}, C3{0.8, 0.6, 0.6}, C3{0.9, 0.7, 0.3}}}},
};
const std::vector<std::string> HERD = {"trex", "trike", "brachio", "stego", "raptor", "raptor", "raptor", "anky", "para", "ptero", "ptero", "ptero"};
std::string fur(const std::string& diet) { return diet == "herbivore" ? "leaf" : "meat"; }

Beast make_dino(const std::string& kind, int i, PyRandom& rnd) {
    Beast d;
    d.kind = kind;
    d.i = i;
    const Species& sp = SPECIES.at(kind);
    d.name = sp.name;
    d.diet = sp.diet;
    d.length = sp.length;
    d.era = sp.era;
    d.fact = sp.fact;
    d.speed = sp.speed;
    d.radius = sp.radius;
    d.pick_h = sp.pick_h;
    d.pick_r = sp.pick_r;
    d.foot = sp.foot;
    double j = rnd.uniform(-0.07, 0.07);  // each animal is tinted a little differently
    for (int k = 0; k < 3; k++) {
        const C3& col = sp.pal[k];
        auto f = [&](double c) { return std::min(1.0, std::max(0.0, c + j * (k + 1) * 0.5)); };
        d.pal[k] = C3(f(col.r), f(col.g), f(col.b));
    }
    d.flyer = kind == "ptero";
    d.heading = rnd.uniform(0, 6.28);
    d.phase = rnd.uniform(0, 6);
    d.idle = rnd.uniform(0, 3);
    d.flap = 6.0;
    if (d.flyer) {
        d.cx = rnd.uniform(-25, 25);
        d.cz = rnd.uniform(-25, 25);
        d.rad = rnd.uniform(28, 62);
        double sgn = rnd.choice(std::vector<int>{-1, 1});
        d.w = sgn * rnd.uniform(0.22, 0.38);
        d.a = rnd.uniform(0, 6.28);
        d.alt = rnd.uniform(14, 26);
        d.y = d.alt;
    } else {
        d.x = rnd.uniform(-70, 70);
        d.z = rnd.uniform(-70, 70);
        d.y = 0.0;
    }
    return d;
}
}  // namespace

Herd::Herd(long long seed) : rnd(seed) {
    for (size_t i = 0; i < HERD.size(); i++) dinos.push_back(make_dino(HERD[i], (int)i, rnd));
}

V3 Herd::centre(const Beast& d) const { return {d.x, !d.flyer ? d.pick_h * SCALE : d.y, d.z}; }

// index of the dinosaur under the mouse ray (nearest first)
std::optional<int> Herd::pick(const V3& o, const V3& dir) {
    double best = 1e9;
    std::optional<int> bi;
    for (auto& d : dinos) {
        V3 c = centre(d);
        V3 rv = o - c;
        double b = rv.dot(dir);
        double rr = d.pick_r * (!d.flyer ? SCALE : 1.0);
        double cc = rv.dot(rv) - rr * rr;
        double disc = b * b - cc;
        if (disc >= 0) {
            double t = -b - std::sqrt(disc);
            if (t > 0 && t < best) {
                best = t;
                bi = d.i;
            }
        }
    }
    return bi;
}

// click a dinosaur: it roars (or screeches), turns to look at you and startles the animals around it
Beast& Herd::poke(int i, double t) {
    Beast& d = dinos[i];
    d.roar_t0 = t;
    d.look_goal = 0.0;
    d.look_t = t;
    d.idle = std::max(d.idle, 1.8);
    d.moving = false;
    if (d.flyer) d.flap = 14.0;
    bool big = d.kind == "trex" || d.kind == "brachio" || d.kind == "trike";
    shake = std::max(shake, d.kind == "trex" ? 0.9 : big ? 0.5 : 0.0);
    rings.push_back({d.x, d.z, t, 0.0});
    for (auto& o : dinos) {
        if (&o == &d || o.flyer) continue;
        double dist = std::hypot(o.x - d.x, o.z - d.z);
        if (dist < 38) {
            if (o.kind == "raptor" || (d.kind == "trex" && o.diet == "herbivore")) {
                o.flee_until = t + 3.5 + (38 - dist) * 0.05;  // the small and the prey run away
                o.target.reset();
            } else {
                o.look_goal = 0.6;
                o.idle = std::max(o.idle, 1.2);
            }
        }
    }
    return d;
}

// drop food on the ground: the nearest animal that eats it walks over
Beast* Herd::feed(double x, double z, double t) {
    std::string want = rnd.choice(std::vector<std::string>{"leaf", "meat"});
    auto cands_for = [&](const std::string& w) {
        std::vector<Beast*> c;
        for (auto& d : dinos)
            if (!d.flyer && !d.bait && fur(d.diet) == w && std::hypot(d.x - x, d.z - z) < 90) c.push_back(&d);
        return c;
    };
    auto cands = cands_for(want);
    if (cands.empty()) {
        want = want == "leaf" ? "meat" : "leaf";
        cands = cands_for(want);
    }
    auto bait = std::make_shared<Bait>(Bait{x, z, want, t});
    baits.push_back(bait);
    if (cands.empty()) return nullptr;
    Beast* d = *std::min_element(cands.begin(), cands.end(), [&](Beast* a, Beast* b) { return std::hypot(a->x - x, a->z - z) < std::hypot(b->x - x, b->z - z); });
    d->bait = bait;
    d->target = std::array<double, 3>{x, z, 0};
    d->idle = 0.0;
    d->flee_until = 0.0;
    return d;
}

std::array<double, 3> Herd::pick_spot(const Beast& d, const World& w) {
    double rmin = w.bounds.first, rmax = w.bounds.second;
    for (int k = 0; k < 14; k++) {
        double a = rnd.uniform(0, TAU);
        double rad = rnd.uniform(rmin, rmax);
        double x = std::sin(a) * rad, z = std::cos(a) * rad;
        if (!blocked_basic(x, z, w, d.radius + 1)) return {x, z, 0};
    }
    return {std::sin((double)d.i) * rmax, std::cos((double)d.i) * rmax, 0};
}

void Herd::step(double t, const World& world) {
    double dt = !has_last ? 0.0 : std::min(std::max(t - last, 0.0), 0.1);
    last = t;
    has_last = true;
    shake *= std::exp(-dt * 3.0);
    baits.erase(std::remove_if(baits.begin(), baits.end(), [&](const BaitP& b) { return !(t - b->t0 < 40); }), baits.end());
    rings.erase(std::remove_if(rings.begin(), rings.end(), [&](auto& r_) { return !(t - r_[2] < 1.4); }), rings.end());
    for (auto& r_ : rings) r_[3] = 3 + (t - r_[2]) * 26;
    for (auto& p : dust) {
        p[0] += p[3] * dt;
        p[1] += p[4] * dt;
        p[2] += p[5] * dt;
        p[4] -= 9 * dt;
        p[6] -= dt;
    }
    dust.erase(std::remove_if(dust.begin(), dust.end(), [](auto& p) { return !(p[6] > 0); }), dust.end());
    auto has_bait = [&](const BaitP& b) { return std::find(baits.begin(), baits.end(), b) != baits.end(); };
    for (auto& d : dinos) {
        bool roar = t - d.roar_t0 < 1.7;
        if (d.flyer) {
            d.flap += (6.0 - d.flap) * std::min(1.0, dt * 0.8);
            d.a += d.w * dt * (roar ? 2.2 : 1.0);
            d.x = d.cx + std::sin(d.a) * d.rad;
            d.z = d.cz + std::cos(d.a) * d.rad;
            d.y = d.alt + std::sin(t * 0.9 + d.i) * 1.5;
            d.heading = std::atan2(std::sin(d.a) * d.w * d.rad, std::cos(d.a) * d.w * d.rad);
            continue;
        }
        d.look += (d.look_goal - d.look) * std::min(1.0, dt * 4);
        if (t - d.look_t > 2.5) d.look_goal *= 0.97;
        if (d.bait && !has_bait(d.bait)) d.bait.reset();
        if (d.eat_until > t) {  // busy eating
            d.moving = false;
            d.mv += (0 - d.mv) * std::min(1.0, dt * 4);
            d.graze += (1.0 - d.graze) * std::min(1.0, dt * 5);
            continue;
        }
        if (d.bait && std::hypot(d.x - d.bait->x, d.z - d.bait->z) < 3.5 + d.radius * 0.3) {
            d.eat_until = t + 3.2;
            d.idle = 0.0;
            BaitP b = d.bait;
            baits.erase(std::remove(baits.begin(), baits.end(), b), baits.end());
            d.bait.reset();
            d.target.reset();
            continue;
        }
        if (d.idle > 0 && d.flee_until <= t) {
            d.idle -= dt;
            d.moving = false;
            d.mv += (0 - d.mv) * std::min(1.0, dt * 4);
            d.graze += ((roar ? 0.0 : 1.0) - d.graze) * std::min(1.0, dt * 3);
            if (rnd.random() < dt * 0.15) {
                d.look_goal = rnd.uniform(-0.6, 0.6);  // look around
                d.look_t = t;
            }
            continue;
        }
        d.graze += (0.0 - d.graze) * std::min(1.0, dt * 3);
        bool fleeing = d.flee_until > t;
        if (!d.target || (!d.bait && blocked_basic((*d.target)[0], (*d.target)[1], world, d.radius))) d.target = pick_spot(d, world);
        if (fleeing) d.target = std::array<double, 3>{d.x + std::cos(d.heading) * 30, d.z - std::sin(d.heading) * 30, 0};  // run directly away
        double dx = (*d.target)[0] - d.x, dz = (*d.target)[1] - d.z;
        double dist = std::hypot(dx, dz);
        if (dist < 3.0 && !d.bait) {
            d.target.reset();
            d.idle = rnd.random() < 0.6 ? rnd.uniform(1.5, 5.0) : 0.0;
            d.moving = false;
            continue;
        }
        double want = std::atan2(-dz, dx);
        double diff = pmod(want - d.heading + PI, TAU) - PI;
        d.heading += std::max(-1.3 * dt, std::min(1.3 * dt, diff));
        d.turn += (std::max(-0.5, std::min(0.5, diff)) - d.turn) * std::min(1.0, dt * 3);
        double spd = d.speed * (fleeing ? 1.9 : (d.bait ? 1.25 : 1.0));
        if (std::fabs(diff) < 1.2) {
            d.x += std::cos(d.heading) * spd * dt;
            d.z += -std::sin(d.heading) * spd * dt;
            d.phase += spd * dt * 0.62;
            d.moving = true;
            d.mv += (1 - d.mv) * std::min(1.0, dt * 4);
            int step_id = (int)(d.phase / PI);
            if (step_id != d.last_step) {  // a foot came down
                d.last_step = step_id;
                bool heavy = d.kind == "trex" || d.kind == "brachio" || d.kind == "trike" || d.kind == "stego" || d.kind == "anky";
                if (heavy && rnd.random() < 0.9) {
                    for (int k = 0; k < 3; k++) {
                        double px = d.x + std::cos(d.heading) * 1.5 + rnd.uniform(-1, 1);
                        double pz = d.z - std::sin(d.heading) * 1.5 + rnd.uniform(-1, 1);
                        double vx = rnd.uniform(-1.2, 1.2);
                        double vy = rnd.uniform(1.5, 3.5);
                        double vz = rnd.uniform(-1.2, 1.2);
                        dust.push_back({px, 0.2, pz, vx, vy, vz, 0.9});
                    }
                }
            }
        } else {
            d.mv += (0 - d.mv) * std::min(1.0, dt * 4);
        }
        for (auto& c : world.circles) {  // shove out of obstacles so they walk around pedestals, not through them
            double ox = d.x - c[0], oz = d.z - c[1];
            double dd = std::hypot(ox, oz);
            if (dd == 0) dd = 1e-6;
            if (dd < c[2] + d.radius) {
                d.x = c[0] + ox / dd * (c[2] + d.radius);
                d.z = c[1] + oz / dd * (c[2] + d.radius);
                if (!d.bait) d.target.reset();
            }
        }
        for (auto& q : world.rects) {
            if (std::fabs(d.x - q[0]) < q[2] + d.radius && std::fabs(d.z - q[1]) < q[3] + d.radius) {
                if ((q[2] + d.radius - std::fabs(d.x - q[0])) < (q[3] + d.radius - std::fabs(d.z - q[1]))) d.x = q[0] + std::copysign(q[2] + d.radius, d.x - q[0]);
                else d.z = q[1] + std::copysign(q[3] + d.radius, d.z - q[1]);
                if (!d.bait) d.target.reset();
            }
        }
    }
}

std::vector<Box> Herd::boxes(double t, const V3& cam, Renderer* r) {
    std::vector<Box> out;
    for (auto& d : dinos) {
        if (std::hypot(d.x - cam.x, d.z - cam.z) > 230) continue;
        if (dist3({d.x, d.flyer ? d.y : 3.0, d.z}, cam) < (d.flyer ? 26 : 9)) continue;  // too close to the lens: hide it
        double roar_u = (t - d.roar_t0) / 1.7;
        double roar = (0 <= roar_u && roar_u <= 1) ? std::pow(std::sin(PI * roar_u), 0.6) : 0.0;
        double graze = d.graze + (d.eat_until <= t ? 0.0 : 0.15 * std::sin(t * 7));  // chewing
        St st{t, d.phase * 2, d.mv, roar, d.look, graze, d.turn, d.flap, d.i * 1.7};
        Pose P;
        RIGS.at(d.kind)(P, st, d.pal);
        double y0 = d.flyer ? d.y : 0.0;
        place(out, P.parts, d.x, y0, d.z, d.heading, SCALE, EDGE);
        if (r) {  // a soft shadow on the ground
            double alt = 1.0 / (1.0 + std::max(y0 - 2, 0.0) * 0.12);
            shadow(r, d.x, d.z, d.heading, d.foot.first * SCALE, d.foot.second * SCALE, 0.32 * alt);
        }
    }
    for (auto& b : baits) {
        double wob = std::sin(t * 3 + b->x);
        if (b->kind == "leaf") {
            for (int k = 0; k < 5; k++)
                out.push_back(box(b->x + std::cos(k * 1.3) * 0.7, 0.35 + 0.1 * k, b->z + std::sin(k * 1.3) * 0.7, 0.9, 0.3, 0.6, {0.25, 0.8, 0.25}, 0.9, k, 0.2));
        } else {
            out.push_back(box(b->x, 0.55 + 0.1 * wob, b->z, 1.5, 1.0, 1.0, {0.85, 0.15, 0.15}, 0.9, t, 0.3));
            out.push_back(box(b->x + 0.9, 0.55, b->z, 0.9, 0.3, 0.3, WHITE, 0.6, t, 0.3));
        }
    }
    for (auto& p : dust) {
        double k = std::min(1.0, p[6] / 0.9);
        double s = 0.35 + 0.5 * (1 - k);
        out.push_back(box(p[0], p[1], p[2], s, s, s, {0.55 * k + 0.2, 0.5 * k + 0.18, 0.38 * k + 0.12}, 0.1, 0.0, 0.0));
    }
    for (auto& rg : rings) {  // roar shockwave
        double fade = 1 - (t - rg[2]) / 1.4;
        for (int k = 0; k < 20; k++) {
            double a = k * TAU / 20;
            out.push_back(box(rg[0] + std::cos(a) * rg[3], 0.15, rg[1] + std::sin(a) * rg[3], 1.4 * fade + 0.2, 0.25, 0.5, {1.0, 0.75, 0.3}, 1.2 * fade, -a, 0.2));
        }
    }
    return out;
}

std::vector<Part> dino_pose(const std::string& kind, double t, double ph, double mv, double roar, double flap) {
    St st{t, ph, mv, roar, 0.0, 0.0, 0.0, flap, 0.0};
    Pose P;
    RIGS.at(kind)(P, st, SPECIES.at(kind).pal);
    return P.parts;
}

std::array<C3, 3> dino_base_palette(const std::string& kind) { return SPECIES.at(kind).pal; }
