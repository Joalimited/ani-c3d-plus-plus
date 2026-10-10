#include "haunt.hpp"

#include <algorithm>
#include <cmath>
#include <map>

#include "../gfx/renderer.hpp"

namespace {
const C3 BONE{0.92, 0.9, 0.8}, DARK{0.03, 0.02, 0.04}, GREEN{0.45, 0.62, 0.35};
const double EDGE = 0.35;
const double SCALE = 1.35;

struct Species {
    const char *name, *trait, *height, *since, *fact;
    double speed, radius, pick_h, pick_r;
    const char *eats, *verb;
};
const std::map<std::string, Species> SPECIES = {
    {"ghost", {"Ghost", "Ectoplasm", "1.8 m", "1692", "Passes through walls but never quite through the cat.", 3.4, 3.5, 5.0, 4.6, "", "wails BOOOO"}},
    {"skeleton", {"Skeleton", "Calcium", "1.7 m", "1349", "Rattles at 120 bones per minute and has no sense of rhythm.", 3.6, 3.0, 3.8, 3.6, "candy", "rattles"}},
    {"zombie", {"Zombie", "Brains (candy will do)", "1.8 m", "1968", "Groans in a major key; walks toward anything shiny.", 2.2, 3.2, 3.8, 3.6, "candy", "groans"}},
    {"cat", {"Black Cat", "Mice and spite", "0.3 m", "1590", "Crossed your path on purpose.", 5.5, 1.8, 1.2, 2.2, "candy", "hisses"}},
    {"witch", {"Witch", "Eye of newt", "1.6 m", "1692", "Her broom does 140 km/h and is not street legal.", 0.0, 0.0, 0.0, 4.5, "", "cackles"}},
    {"spider", {"Giant Spider", "Flies, mostly", "0.6 m", "-", "Eight legs, zero patience, one very long thread.", 0.0, 0.0, 0.0, 3.0, "", "skitters"}},
};
const std::vector<std::string> KINDS = {"ghost", "ghost", "ghost", "skeleton", "skeleton", "skeleton", "zombie", "zombie", "cat", "cat", "witch", "spider", "spider", "spider"};

Beast make(const std::string& kind, int i, PyRandom& rnd) {
    Beast d;
    d.kind = kind;
    d.i = i;
    const Species& sp = SPECIES.at(kind);
    d.name = sp.name;
    d.diet = sp.trait;
    d.length = sp.height;
    d.era = sp.since;
    d.fact = sp.fact;
    d.speed = sp.speed;
    d.radius = sp.radius;
    d.pick_h = sp.pick_h;
    d.pick_r = sp.pick_r;
    d.eats = sp.eats;
    d.verb = sp.verb;
    d.flyer = kind == "ghost" || kind == "witch" || kind == "spider";
    d.x = rnd.uniform(-70, 70);
    d.z = rnd.uniform(-70, 70);
    d.y = 0.0;
    d.heading = rnd.uniform(0, 6.28);
    d.phase = rnd.uniform(0, 6);
    d.idle = rnd.uniform(0, 3);
    d.seed = rnd.random() * 100;
    if (kind == "ghost") {
        static const std::vector<C3> cols = {C3(0.9, 0.9, 1.0), C3(0.8, 1.0, 0.9), C3(1.0, 0.9, 0.95)};
        d.tint = rnd.choice(cols);
        d.alt = rnd.uniform(3.5, 9);
        d.y = d.alt;
    }
    if (kind == "witch") {
        d.cx = rnd.uniform(-20, 20);
        d.cz = rnd.uniform(-20, 20);
        d.rad = rnd.uniform(40, 70);
        d.w = rnd.choice(std::vector<int>{-1, 1}) * 0.3;
        d.a = rnd.uniform(0, 6.28);
        d.alt = rnd.uniform(16, 24);
        d.y = d.alt;
    }
    if (kind == "spider") {
        double ang = rnd.uniform(0, 6.28);
        double rad = rnd.uniform(25, 85);
        d.x = std::sin(ang) * rad;
        d.z = std::cos(ang) * rad;
        d.alt = rnd.uniform(8, 13);
        d.y = d.alt;
    }
    return d;
}

// a bone from (x,y,z) pointing `ang` up from forward (local frame); returns the end point
V3 seg(std::vector<Part>& out, double x, double y, double z, double length, double ang, double th, double wid, C3 col, double glow = 0.2, double yl = 0.0) {
    double ca = std::cos(ang), sa = std::sin(ang);
    out.push_back({x + ca * length / 2, y + sa * length / 2, z, length, th, wid, col, glow, ang, 0.0, yl});
    return {x + ca * length, y + sa * length, z};
}

struct St {
    double ph, mv, roar, look, collapse, turn;
};

// ------------------------------------------------------------ rigs: local frame, +x forward, +z side
std::vector<Part> rig_skeleton(const Beast& d, double t, const St& st) {
    double ph = st.ph, mv = st.mv, c = st.collapse;
    std::vector<Part> P;
    double hy = 2.6 + 0.08 * std::fabs(std::sin(ph)) * mv;
    int k = 0;
    for (int sz : {-1, 1}) {
        double s = std::sin(ph + k * PI) * 0.7 * mv;
        V3 kn = seg(P, 0, hy, sz * 0.3, 1.15, -PI / 2 + s, 0.26, 0.26, BONE);
        seg(P, kn.x, kn.y, kn.z, 1.15, -PI / 2 - std::max(0.0, -s) * 0.9, 0.22, 0.22, BONE);
        P.push_back({kn.x + 0.25, 0.1, kn.z, 0.6, 0.18, 0.28, BONE, 0.2, 0, 0, 0});
        V3 ar = seg(P, 0, hy + 1.65, sz * 0.62, 1.0, -PI / 2 - s * 0.9, 0.2, 0.2, BONE);
        seg(P, ar.x, ar.y, ar.z, 0.9, -PI / 2 - s * 0.9 + 0.3, 0.17, 0.17, BONE);
        k++;
    }
    P.push_back({0, hy + 0.15, 0, 0.5, 0.35, 0.9, BONE, 0.25, 0, 0, 0});  // pelvis
    P.push_back({0, hy + 0.9, 0, 0.18, 1.5, 0.18, BONE, 0.25, 0, 0, 0});  // spine
    for (int q = 0; q < 3; q++) P.push_back({0, hy + 0.9 + q * 0.42, 0, 0.55 - q * 0.06, 0.12, 1.0 - q * 0.1, BONE, 0.3, 0, 0, 0});  // ribs
    P.push_back({0.1, hy + 2.25, 0, 0.95, 0.85, 0.85, BONE, 0.4, 0, 0, st.look});  // skull
    P.push_back({0.55, hy + 2.35, -0.22, 0.12, 0.22, 0.22, DARK, 0.0, 0, 0, 0});
    P.push_back({0.55, hy + 2.35, 0.22, 0.12, 0.22, 0.22, DARK, 0.0, 0, 0, 0});
    double jaw = 0.12 + 0.5 * st.roar + 0.08 * std::sin(t * 9) * mv;
    P.push_back({0.35, hy + 1.8 - jaw * 0.5, 0, 0.55, 0.2, 0.6, BONE, 0.3, -jaw, 0, 0});
    if (c > 0) {  // fall apart: every part slides toward a pile on the ground
        std::vector<Part> out;
        for (size_t n = 0; n < P.size(); n++) {
            const Part& p = P[n];
            PyRandom r_((long long)(d.seed * 100) + (long long)n);
            double ux = r_.uniform(-1.6, 1.6), uz = r_.uniform(-1.6, 1.6), urz = r_.uniform(-2, 2), uyl = r_.uniform(-1.5, 1.5);
            out.push_back({p.x * (1 - c) + ux * c, std::max(p.y * (1 - c) + 0.2 * c, 0.15), p.z * (1 - c) + uz * c, p.sx, p.sy, p.sz, p.col, p.glow,
                           p.rz + urz * c, p.rx, p.yl + uyl * c});
        }
        return out;
    }
    return P;
}

std::vector<Part> rig_zombie(const Beast& d, double t, const St& st) {
    double ph = st.ph, mv = st.mv;
    std::vector<Part> P;
    double hy = 2.5 + 0.06 * std::fabs(std::sin(ph)) * mv;
    double lean = 0.18;
    const C3 pants{0.25, 0.28, 0.45};
    int k = 0;
    for (int sz : {-1, 1}) {
        double s = std::sin(ph * 0.8 + k * PI) * 0.5 * mv;
        V3 kn = seg(P, 0, hy, sz * 0.32, 1.2, -PI / 2 + s, 0.4, 0.4, pants);
        seg(P, kn.x, kn.y, kn.z, 1.15, -PI / 2 - std::max(0.0, -s) * 0.6, 0.36, 0.36, pants);
        P.push_back({kn.x + 0.2, 0.1, kn.z, 0.7, 0.2, 0.38, {0.1, 0.08, 0.06}, 0.1, 0, 0, 0});
        V3 ar = seg(P, 0.15, hy + 1.8, sz * 0.75, 1.0, 0.05 + 0.1 * std::sin(t * 2 + k), 0.3, 0.3, GREEN);  // arms stretched out
        seg(P, ar.x, ar.y, ar.z, 0.9, 0.0 + 0.1 * std::sin(t * 2.4 + k), 0.26, 0.26, GREEN);
        k++;
    }
    P.push_back({0, hy + 1.1, 0, 0.8, 1.8, 1.2, {0.55, 0.15, 0.15}, 0.2, -lean, 0, 0});  // torn shirt
    P.push_back({0.1, hy + 2.4, 0, 0.95, 0.9, 0.9, GREEN, 0.3, -lean, 0, st.look});
    P.push_back({0.6, hy + 2.55, -0.25, 0.12, 0.2, 0.2, {1, 1, 0.6}, 0.9, 0, 0, 0});
    P.push_back({0.6, hy + 2.55, 0.25, 0.12, 0.2, 0.2, {1, 1, 0.6}, 0.9, 0, 0, 0});
    P.push_back({0.55, hy + 2.1, 0, 0.12, 0.2, 0.55, DARK, 0.0, 0, 0, 0});
    for (int q : {-1, 0, 1}) P.push_back({-0.1, hy + 2.95, q * 0.3, 0.2, 0.3, 0.2, {0.15, 0.1, 0.05}, 0.1, 0.4 * q, 0, 0});
    return P;
}

std::vector<Part> rig_ghost(const Beast& d, double t, const St& st) {
    std::vector<Part> P;
    double bob = 0.35 * std::sin(t * 1.7 + d.seed);
    double b = st.roar;
    double h = 1.0 + 0.5 * b;
    C3 col = d.tint;
    for (int k = 0; k < 4; k++) {  // the sheet body: wider toward the hem
        double w = 1.3 + k * 0.28 + 0.5 * b;
        P.push_back({0, bob + 3.3 - k * 0.8 * h, 0, w, 0.9 * h, w, col, 0.8, 0, 0, 0});
    }
    for (int k = 0; k < 6; k++) {  // a wavy hem
        double a = k / 6.0 * TAU;
        P.push_back({std::cos(a) * 1.0, bob + 0.2 + 0.25 * std::sin(t * 5 + k * 1.3), std::sin(a) * 1.0, 0.7, 0.55, 0.7, col, 0.8, 0, 0, 0});
    }
    P.push_back({0, bob + 4.0, 0, 1.4, 1.1, 1.4, col, 0.9, 0, 0, st.look});
    P.push_back({0.7, bob + 4.2, -0.35, 0.14, 0.4 + 0.5 * b, 0.28, DARK, 0.0, 0, 0, 0});
    P.push_back({0.7, bob + 4.2, 0.35, 0.14, 0.4 + 0.5 * b, 0.28, DARK, 0.0, 0, 0, 0});
    P.push_back({0.72, bob + 3.6, 0, 0.14, 0.25 + 0.8 * b, 0.4 + 0.5 * b, DARK, 0.0, 0, 0, 0});
    for (int sz : {-1, 1}) {  // arms: dangling, or up when it booos
        double ang = -1.3 + b * 2.3 + 0.15 * std::sin(t * 2 + sz);
        seg(P, 0, bob + 3.2, sz * (1.0 + 0.4 * b), 1.5, ang, 0.35, 0.35, col, 0.7);
    }
    return P;
}

std::vector<Part> rig_cat(const Beast& d, double t, const St& st) {
    std::vector<Part> P;
    double ph = st.ph, mv = st.mv, r = st.roar;
    double arch = 0.5 * r;
    double hy = 0.9 + 0.35 * r;
    P.push_back({0, hy, 0, 1.5 - 0.3 * r, 0.7 + 0.25 * r, 0.65, DARK, 0.25, arch, 0, 0});
    P.push_back({0.95, hy + 0.2 + 0.2 * r, 0, 0.7, 0.62, 0.65, DARK, 0.25, 0, 0, st.look});
    for (int sz : {-1, 1}) {
        P.push_back({1.05, hy + 0.62 + 0.2 * r, sz * 0.22, 0.14, 0.3, 0.18, DARK, 0.35, 0, 0, 0});           // ears
        P.push_back({1.3, hy + 0.35 + 0.2 * r, sz * 0.18, 0.1, 0.16, 0.16, {1.0, 0.9, 0.2}, 1.6, 0, 0, 0});  // glowing eyes
    }
    const double sxs[4] = {0.55, 0.55, -0.55, -0.55};
    for (int k = 0; k < 4; k++) {
        double sz = (k % 2 == 0 ? -1 : 1) * 0.22;
        double s = std::sin(ph * 1.6 + k * PI / 2) * 0.6 * mv;
        seg(P, sxs[k], hy - 0.25, sz, 0.8, -PI / 2 + s, 0.2, 0.2, DARK, 0.2);
    }
    double x = -0.7, y = hy + 0.1;
    double ang = 0.4 + 0.9 * r;
    for (int k = 0; k < 5; k++) {
        V3 e = seg(P, x, y, 0, 0.5, PI - ang * (1 - k * 0.1) + 0.1 * k, 0.2 + 0.1 * r, 0.2 + 0.1 * r, DARK, 0.2);
        x = e.x;
        y = e.y;
    }
    return P;
}

std::vector<Part> rig_witch(const Beast& d, double t, const St& st) {
    std::vector<Part> P;
    double ang = -0.05 + 0.05 * std::sin(t * 2);
    seg(P, -2.2, 0, 0, 4.4, ang, 0.18, 0.18, {0.45, 0.28, 0.12}, 0.3);  // broom handle
    for (int k = 0; k < 7; k++) P.push_back({-2.4 - k * 0.12, 0.0 + 0.02 * k, (k - 3) * 0.1, 0.9, 0.08, 0.08, {0.8, 0.6, 0.2}, 0.35, 0.0, 0, 0});  // bristles
    P.push_back({0.2, 1.0, 0, 1.1, 1.3, 1.0, {0.12, 0.05, 0.18}, 0.3, 0, 0, 0});  // robe
    P.push_back({0.2, 0.7, 0, 1.4, 0.8, 1.2, {0.12, 0.05, 0.18}, 0.3, 0, 0, 0});
    P.push_back({0.25, 1.95, 0, 0.8, 0.8, 0.8, {0.55, 0.85, 0.4}, 0.4, 0, 0, 0});    // face
    P.push_back({0.7, 1.9, 0, 0.3, 0.2, 0.18, {0.45, 0.75, 0.3}, 0.4, -0.2, 0, 0});  // nose
    P.push_back({0.25, 2.4, 0, 1.6, 0.14, 1.6, {0.07, 0.03, 0.1}, 0.3, 0, 0, 0});    // hat brim
    P.push_back({0.2, 2.8, 0, 0.9, 0.8, 0.9, {0.07, 0.03, 0.1}, 0.3, 0, 0, 0});
    P.push_back({0.1, 3.4, 0, 0.5, 0.7, 0.5, {0.07, 0.03, 0.1}, 0.3, -0.25, 0, 0});
    P.push_back({0.35, 2.55, 0, 1.65, 0.1, 1.65, {0.9, 0.5, 0.1}, 0.9, 0, 0, 0});  // hat band
    P.push_back({-0.4, 1.2, 0.8, 0.8, 0.2, 0.2, {0.55, 0.85, 0.4}, 0.3, 0.2, 0, 0});  // arm on the broom
    return P;
}

const C3 SPIDER_BODY{0.08, 0.06, 0.10}, SPIDER_SHEEN{0.20, 0.14, 0.24}, SPIDER_LEG{0.11, 0.08, 0.12}, SPIDER_JOINT{0.55, 0.20, 0.06}, SPIDER_EYE{1.0, 0.12, 0.08};
const V3 SPIDER_SPINNERET{-1.85, 0.45, 0.0};  // where the silk leaves the abdomen (the thread is drawn from here)

// a dangling spider: cephalothorax + round abdomen, eight jointed three-part legs that paddle at the air (and flail when poked)
std::vector<Part> rig_spider(const Beast& d, double t, const St& st) {
    std::vector<Part> P;
    double r = st.roar;
    const double spreads[4] = {0.95, 0.38, -0.22, -0.82};  // front -> back pair, angle from the side axis toward +x
    for (int k = 0; k < 4; k++) {
        for (int sz : {-1, 1}) {
            double ph = t * (4.5 + 6 * r) + k * PI / 2 + (sz > 0 ? PI : 0) + d.seed;
            double a = spreads[k] + (0.16 + 0.25 * r) * std::sin(ph);  // paddle forward / back
            double lift = (0.18 + 0.35 * r) * std::max(0.0, std::sin(ph + 0.6));
            double curl = 0.25 * (1 - r) * (0.5 + 0.5 * std::sin(t * 0.7 + k + d.seed));  // idle legs slowly draw in
            double hx = std::sin(a), hz = std::cos(a) * sz;
            V3 sock{0.62 - k * 0.27, 0.18, sz * 0.36};
            V3 knee{sock.x + hx * 1.05, sock.y + 0.8 + lift, sock.z + hz * 1.05};
            V3 ankle{knee.x + hx * (1.0 - curl * 0.6), knee.y - 0.55 - curl * 0.4, knee.z + hz * (1.0 - curl * 0.6)};
            V3 foot{ankle.x + hx * (0.45 - curl * 0.4), ankle.y - 0.95 + curl * 0.3, ankle.z + hz * (0.45 - curl * 0.4)};
            P.push_back(limb(sock, knee, 0.15, SPIDER_LEG));
            P.push_back(limb(knee, ankle, 0.12, SPIDER_LEG));
            P.push_back(limb(ankle, foot, 0.085, SPIDER_SHEEN, 0.25));
            for (auto [j, jt] : {std::pair<V3, double>{knee, 0.19}, {ankle, 0.15}})  // knuckles hide the seams
                P.push_back({j.x, j.y, j.z, jt, jt, jt, SPIDER_JOINT, 0.45 + 0.5 * r, 0.0, 0.0, 0.0});
        }
    }
    P.push_back({0.42, 0.18, 0, 1.05, 0.55, 0.9, SPIDER_BODY, 0.25, 0, 0, 0});  // cephalothorax and head
    P.push_back({0.42, 0.48, 0, 0.8, 0.14, 0.66, SPIDER_SHEEN, 0.3, 0, 0, 0});  // carapace plate
    P.push_back({1.02, 0.24, 0, 0.42, 0.46, 0.62, SPIDER_BODY, 0.25, 0, 0, 0});
    double pulse = 1.5 + 0.4 * std::sin(t * 3 + d.seed) + 1.2 * r;  // eyes: two big glowing ones in front, six small around them
    for (int sz : {-1, 1}) {
        P.push_back({1.24, 0.34, sz * 0.13, 0.15, 0.15, 0.15, SPIDER_EYE, pulse, 0, 0, 0});
        P.push_back({1.2, 0.43, sz * 0.27, 0.08, 0.08, 0.08, SPIDER_EYE, pulse * 0.7, 0, 0, 0});
        P.push_back({1.12, 0.47, sz * 0.07, 0.07, 0.07, 0.07, SPIDER_EYE, pulse * 0.7, 0, 0, 0});
        P.push_back({1.08, 0.42, sz * 0.36, 0.06, 0.06, 0.06, SPIDER_EYE, pulse * 0.6, 0, 0, 0});
        // fangs (open wider when poked) and the little palps beside them
        P.push_back(limb({1.22, 0.1, sz * 0.12}, {1.36, -0.22, sz * (0.1 + 0.1 * r)}, 0.1, {0.30, 0.06, 0.06}, 0.4));
        P.push_back(limb({1.12, 0.12, sz * 0.28}, {1.55, 0.02 + 0.08 * std::sin(t * 5 + sz), sz * 0.36}, 0.08, SPIDER_LEG));
    }
    // waist and a round abdomen (three overlapping boxes read as a rounded shape)
    P.push_back({-0.18, 0.24, 0, 0.32, 0.3, 0.34, SPIDER_LEG, 0.2, 0, 0, 0});
    double breathe = 1.0 + 0.03 * std::sin(t * 2 + d.seed);
    P.push_back({-1.0, 0.42, 0, 1.55 * breathe, 1.0 * breathe, 1.2 * breathe, SPIDER_BODY, 0.25, 0, 0, 0});
    P.push_back({-1.0, 0.42, 0, 1.2, 1.28 * breathe, 0.95, SPIDER_BODY, 0.25, 0, 0, 0});
    P.push_back({-1.0, 0.42, 0, 1.8 * breathe, 0.72, 0.9, SPIDER_BODY, 0.25, 0, 0, 0});
    P.push_back({-1.0, 0.95 * breathe, 0, 1.1, 0.16, 0.8, SPIDER_SHEEN, 0.3, 0, 0, 0});  // glossy back
    P.push_back({-0.78, 1.05 * breathe, 0, 0.3, 0.06, 0.32, SPIDER_EYE, 1.1, 0, 0, 0});  // red hourglass on the back
    P.push_back({-1.22, 1.05 * breathe, 0, 0.3, 0.06, 0.32, SPIDER_EYE, 1.1, 0, 0, 0});
    P.push_back({-1.0, 1.05 * breathe, 0, 0.22, 0.06, 0.12, SPIDER_EYE, 1.1, 0, 0, 0});
    P.push_back({SPIDER_SPINNERET.x + 0.05, SPIDER_SPINNERET.y, 0, 0.22, 0.2, 0.22, SPIDER_SHEEN, 0.3, 0, 0, 0});
    return P;
}

using RigFn = std::vector<Part> (*)(const Beast&, double, const St&);
const std::map<std::string, RigFn> RIGS = {{"skeleton", rig_skeleton}, {"zombie", rig_zombie}, {"ghost", rig_ghost},
                                           {"cat", rig_cat},           {"witch", rig_witch},   {"spider", rig_spider}};
}  // namespace

Haunt::Haunt(long long seed) : rnd(seed) {
    for (size_t i = 0; i < KINDS.size(); i++) dinos.push_back(make(KINDS[i], (int)i, rnd));
    static_ = scenery();
}

std::vector<Box> Haunt::scenery() {
    PyRandom r2(99);
    std::vector<Box> out;
    for (int i = 0; i < 46; i++) {  // gravestones
        double a = r2.uniform(0, TAU), rad = r2.uniform(34, 150);
        double x = std::sin(a) * rad, z = std::cos(a) * rad;
        double h = r2.uniform(1.6, 3.4);
        double yaw = r2.uniform(-0.4, 0.4);
        C3 stone{0.38, 0.4, 0.46};
        out.push_back(box(x, h / 2, z, 1.5, h, 0.45, stone, 0.15, yaw, 0.5));
        if (i % 3 == 0) out.push_back(box(x, h + 0.4, z, 1.0, 0.45, 0.4, stone, 0.15, yaw, 0.5));       // rounded top
        else if (i % 5 == 0) out.push_back(box(x, h * 0.75, z, 1.9, 0.35, 0.4, stone, 0.15, yaw, 0.5));  // cross arm
        out.push_back(box(x, 0.12, z + 0.3, 1.8, 0.2, 2.2, {0.14, 0.1, 0.06}, 0.05, yaw, 0.0));        // earth mound
    }
    for (int i = 0; i < 16; i++) {  // dead trees: a trunk with forked branches
        double a = r2.uniform(0, TAU), rad = r2.uniform(60, 190);
        double x = std::sin(a) * rad, z = std::cos(a) * rad;
        double h = r2.uniform(9, 15);
        C3 dark{0.1, 0.07, 0.08};
        out.push_back(box(x, h / 2, z, 1.2, h, 1.2, dark, 0.1, 0.0, 0.3));
        for (int k = 0; k < 5; k++) {
            double yy = h * (0.45 + 0.1 * k);
            int side = k % 2 ? 1 : -1;
            out.push_back(box(x + side * 1.6, yy, z, 3.4, 0.35, 0.35, dark, 0.1, 0.0, 0.3, 0.0, 0.0, side * 0.5));
            out.push_back(box(x + side * 3.0, yy + 1.0, z, 1.8, 0.25, 0.25, dark, 0.1, 0.0, 0.3, 0.0, 0.0, side * 0.9));
        }
    }
    for (int i = 0; i < 12; i++) {  // iron fence posts around the yard
        double a = i / 12.0 * TAU;
        double x = std::sin(a) * 38, z = std::cos(a) * 38;
        for (int k = -2; k < 3; k++) out.push_back(box(x + std::cos(a) * k * 1.4, 1.4, z - std::sin(a) * k * 1.4, 0.18, 2.8, 0.18, {0.05, 0.05, 0.07}, 0.2, a, 0.4));
        out.push_back(box(x, 2.4, z, 7.4, 0.15, 0.15, {0.05, 0.05, 0.07}, 0.2, a, 0.4));
    }
    // a big pale moon with a halo and craters (close enough that the fog does not swallow it)
    out.push_back(box(46, 62, -112, 30, 30, 2, {1.0, 0.96, 0.78}, 1.8, 0.0, 0.0, 0.0, 1.0));
    out.push_back(box(46, 62, -113, 38, 38, 1, {0.45, 0.35, 0.6}, 1.0, 0.0, 0.0, 0.0, 1.0));
    out.push_back(box(52, 66, -110, 6, 6, 1, {0.82, 0.78, 0.6}, 1.0, 0.0, 0.0, 0.0, 1.0));
    out.push_back(box(40, 56, -110, 4.5, 4.5, 1, {0.82, 0.78, 0.6}, 1.0, 0.0, 0.0, 0.0, 1.0));
    return out;
}

// the haunted house on the hill: a dark silhouette whose windows flicker
std::vector<Box> Haunt::house(double t) {
    std::vector<Box> out;
    double hx = -40, hz = -150;
    C3 dark{0.08, 0.06, 0.1}, roof{0.05, 0.03, 0.07};
    out.push_back(box(hx, 11, hz, 38, 22, 20, dark, 0.15, 0.0, 0.2));
    out.push_back(box(hx, 25, hz, 40, 6, 22, roof, 0.1, 0.0, 0.2, 0.0, 0.0, 0.0));
    out.push_back(box(hx - 10, 29, hz, 18, 6, 18, roof, 0.1, 0.0, 0.2));
    out.push_back(box(hx + 16, 18, hz + 2, 8, 34, 8, dark, 0.15, 0.0, 0.2));  // tower
    out.push_back(box(hx + 16, 38, hz + 2, 10, 6, 10, roof, 0.1, 0.0, 0.2));
    for (int i = 0; i < 5; i++) {  // windows (they blink like candles)
        bool on = std::sin(t * 6 + i * 2.7) + std::sin(t * 2.3 + i) > -0.7;
        C3 col = on ? C3(1.0, 0.75, 0.2) : C3(0.12, 0.07, 0.05);
        out.push_back(box(hx - 14 + i * 7, 12 + (i % 2) * 6, hz + 10.2, 2.4, 3.4, 0.4, col, on ? 1.8 : 0.1, 0.0, 0.0));
    }
    out.push_back(box(hx + 16, 28, hz + 6.3, 2.2, 3.0, 0.4, {1.0, 0.8, 0.25}, 1.8));
    out.push_back(box(hx, 3, hz + 10.2, 3.6, 6, 0.4, {0.04, 0.02, 0.03}, 0.1, 0.0, 0.0));  // door
    return out;
}

std::optional<int> Haunt::pick(const V3& o, const V3& dir) {
    double best = 1e9;
    std::optional<int> bi;
    for (auto& d : dinos) {
        V3 c{d.x, d.flyer ? d.y : d.pick_h * SCALE, d.z};
        if (d.kind == "spider") c.y = d.y;
        V3 rv = o - c;
        double b = rv.dot(dir);
        double rr = d.pick_r * (!d.flyer ? SCALE : 1.0);
        double disc = b * b - (rv.dot(rv) - rr * rr);
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

Beast& Haunt::poke(int i, double t) {
    Beast& d = dinos[i];
    d.roar_t0 = t;
    d.look_goal = 0.0;
    d.look_t = t;
    d.idle = std::max(d.idle, 2.2);
    d.moving = false;
    shake = std::max(shake, (d.kind == "ghost" || d.kind == "witch") ? 0.55 : 0.25);
    rings.push_back({d.x, d.z, t, 0.0});
    for (int k = 0; k < 10; k++) {
        double vx = rnd.uniform(-4, 4), vy = rnd.uniform(1, 6), vz = rnd.uniform(-4, 4);
        dust.push_back({d.x, d.y + 2, d.z, vx, vy, vz, 0.9});
    }
    for (auto& o : dinos) {  // everything nearby gets spooked by the noise
        if (&o == &d || o.flyer) continue;
        double dist = std::hypot(o.x - d.x, o.z - d.z);
        if (dist < 30 && (o.kind == "cat" || o.kind == "zombie")) {
            o.flee_until = t + 2.5;
            o.target.reset();
        }
    }
    return d;
}

// drop candy: the nearest skeleton / zombie / cat walks over to eat it
Beast* Haunt::feed(double x, double z, double t) {
    auto bait = std::make_shared<Bait>(Bait{x, z, "candy", t});
    std::vector<Beast*> cands;
    for (auto& d : dinos)
        if (!d.eats.empty() && !d.bait && std::hypot(d.x - x, d.z - z) < 110) cands.push_back(&d);
    baits.push_back(bait);
    if (cands.empty()) return nullptr;
    Beast* d = *std::min_element(cands.begin(), cands.end(), [&](Beast* a, Beast* b) { return std::hypot(a->x - x, a->z - z) < std::hypot(b->x - x, b->z - z); });
    d->bait = bait;
    d->target = std::array<double, 3>{x, z, 0};
    d->idle = 0.0;
    d->flee_until = 0.0;
    return d;
}

std::array<double, 3> Haunt::pick_spot(const Beast& d, const World& w) {
    double rmin = w.bounds.first, rmax = w.bounds.second;
    for (int k = 0; k < 14; k++) {
        double a = rnd.uniform(0, TAU);
        double rad = rnd.uniform(rmin, rmax);
        double x = std::sin(a) * rad, z = std::cos(a) * rad;
        if (!blocked_basic(x, z, w, d.radius + 1)) return {x, z, 0};
    }
    return {std::sin((double)d.i) * rmax, std::cos((double)d.i) * rmax, 0};
}

void Haunt::step(double t, const World& world) {
    double dt = !has_last ? 0.0 : std::min(std::max(t - last, 0.0), 0.1);
    last = t;
    has_last = true;
    shake *= std::exp(-dt * 3.0);
    baits.erase(std::remove_if(baits.begin(), baits.end(), [&](const BaitP& b) { return !(t - b->t0 < 40); }), baits.end());
    rings.erase(std::remove_if(rings.begin(), rings.end(), [&](auto& r_) { return !(t - r_[2] < 1.4); }), rings.end());
    for (auto& r_ : rings) r_[3] = 3 + (t - r_[2]) * 24;
    for (auto& p : dust) {
        p[0] += p[3] * dt;
        p[1] += p[4] * dt;
        p[2] += p[5] * dt;
        p[4] -= 5 * dt;
        p[6] -= dt;
    }
    dust.erase(std::remove_if(dust.begin(), dust.end(), [](auto& p) { return !(p[6] > 0); }), dust.end());
    auto has_bait = [&](const BaitP& b) { return std::find(baits.begin(), baits.end(), b) != baits.end(); };
    for (auto& d : dinos) {
        bool roar = t - d.roar_t0 < 1.7;
        if (d.kind == "witch") {
            d.a += d.w * dt * (roar ? 2.5 : 1.0);
            d.x = d.cx + std::sin(d.a) * d.rad;
            d.z = d.cz + std::cos(d.a) * d.rad;
            d.y = d.alt + std::sin(t * 0.9 + d.i) * 2.0 + (roar ? 3 * std::sin(PI * (t - d.roar_t0) / 1.7) : 0);
            d.heading = std::atan2(std::sin(d.a) * d.w * d.rad, std::cos(d.a) * d.w * d.rad);
            if (rnd.random() < dt * 8) {  // a trail of sparks
                double px = d.x - std::cos(d.heading) * 3, pz = d.z + std::sin(d.heading) * 3;
                double vx = rnd.uniform(-1, 1), vy = rnd.uniform(0, 2), vz = rnd.uniform(-1, 1);
                dust.push_back({px, d.y, pz, vx, vy, vz, 0.8});
            }
            continue;
        }
        if (d.kind == "spider") {  // drop out of the dark, then climb back up the thread
            double drop = roar ? 4.5 * std::sin(PI * (t - d.roar_t0) / 1.7) : 0.0;
            d.y = d.alt - (3.2 + 3.0 * std::sin(t * 0.35 + d.seed)) - drop;
            d.heading = d.seed + 0.7 * std::sin(t * 0.23 + d.seed) + (roar ? 2.5 * std::sin(PI * (t - d.roar_t0) / 1.7) : 0.0);  // twists on the silk
            continue;
        }
        d.look += (d.look_goal - d.look) * std::min(1.0, dt * 4);
        if (t - d.look_t > 2.5) d.look_goal *= 0.97;
        if (d.kind == "ghost") d.y = d.alt + 0.6 * std::sin(t * 0.8 + d.seed);
        if (d.bait && !has_bait(d.bait)) d.bait.reset();
        if (d.eat_until > t) {
            d.moving = false;
            d.mv += (0 - d.mv) * std::min(1.0, dt * 4);
            continue;
        }
        if (d.bait && std::hypot(d.x - d.bait->x, d.z - d.bait->z) < 3.0 + d.radius * 0.3) {
            d.eat_until = t + 2.6;
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
            if (rnd.random() < dt * 0.2) {
                d.look_goal = rnd.uniform(-0.6, 0.6);
                d.look_t = t;
            }
            continue;
        }
        bool fleeing = d.flee_until > t;
        if (!d.target || (!d.bait && blocked_basic((*d.target)[0], (*d.target)[1], world, d.radius))) d.target = pick_spot(d, world);
        if (fleeing) d.target = std::array<double, 3>{d.x + std::cos(d.heading) * 30, d.z - std::sin(d.heading) * 30, 0};
        double dx = (*d.target)[0] - d.x, dz = (*d.target)[1] - d.z;
        double dist = std::hypot(dx, dz);
        if (dist < 3.0 && !d.bait) {
            d.target.reset();
            d.idle = rnd.random() < 0.6 ? rnd.uniform(1.0, 4.0) : 0.0;
            d.moving = false;
            continue;
        }
        double want = std::atan2(-dz, dx);
        double diff = pmod(want - d.heading + PI, TAU) - PI;
        d.heading += std::max(-1.4 * dt, std::min(1.4 * dt, diff));
        d.turn += (std::max(-0.5, std::min(0.5, diff)) - d.turn) * std::min(1.0, dt * 3);
        double spd = d.speed * (fleeing ? 1.8 : (d.bait ? 1.3 : 1.0));
        if (std::fabs(diff) < 1.2) {
            d.x += std::cos(d.heading) * spd * dt;
            d.z += -std::sin(d.heading) * spd * dt;
            d.phase += spd * dt * 0.8;
            d.moving = true;
            d.mv += (1 - d.mv) * std::min(1.0, dt * 4);
        } else {
            d.mv += (0 - d.mv) * std::min(1.0, dt * 4);
        }
        for (auto& c : world.circles) {
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

std::vector<Box> Haunt::boxes(double t, const V3& cam, Renderer* r) {
    std::vector<Box> out = static_;
    for (auto& b : house(t)) out.push_back(b);
    for (auto& d : dinos) {
        if (std::hypot(d.x - cam.x, d.z - cam.z) > 230) continue;
        if (dist3({d.x, d.flyer ? d.y : 3.0, d.z}, cam) < (d.flyer ? 20 : 8)) continue;
        double roar_u = (t - d.roar_t0) / 1.7;
        double roar = (0 <= roar_u && roar_u <= 1) ? std::pow(std::sin(PI * roar_u), 0.6) : 0.0;
        double collapse = 0.0;
        if (d.kind == "skeleton" && 0 <= roar_u && roar_u <= 1.3)  // falls apart, then pulls itself back together
            collapse = roar_u < 0.7 ? std::min(1.0, roar_u * 3) : std::max(0.0, 1 - (roar_u - 0.7) / 0.6);
        St st{d.phase * 2, d.mv, roar, d.look, collapse, d.turn};
        auto parts = RIGS.at(d.kind)(d, t, st);
        double c = std::cos(d.heading), s = std::sin(d.heading);
        double y0 = (d.flyer && d.kind != "spider") ? d.y : 0.0;
        if (d.kind == "spider") {
            y0 = d.y;
            double ax = d.x + SPIDER_SPINNERET.x * c;  // the silk leaves the spinnerets at the back of the abdomen
            double az = d.z - SPIDER_SPINNERET.x * s;
            double y_lo = y0 + SPIDER_SPINNERET.y, sway = 0.25 * std::sin(t * 0.8 + d.seed);  // the top end drifts a little
            double ln = d.alt + 25 - y_lo;
            out.push_back(box(ax + sway / 2, y_lo + ln / 2, az, 0.05, ln, 0.05, {0.8, 0.8, 0.88}, 0.35, 0.0, 0.0, 0.0, 0.0, -std::asin(sway / std::hypot(sway, ln)), 0.0));
        }
        double sc = (d.kind != "ghost" && d.kind != "witch" && d.kind != "spider") ? SCALE : 1.0;
        double edge = d.kind == "spider" ? 0.12 : EDGE;  // thin legs with a full neon outline read as scribbles
        if (d.kind == "cat") sc = SCALE * 0.9;
        place(out, parts, d.x, y0, d.z, d.heading, sc, edge);
        if (r && d.kind != "spider") {
            double ln = d.kind == "cat" ? 3.0 : 5.0;
            double alt = 1.0 / (1.0 + std::max(y0 - 2, 0.0) * 0.12);
            shadow(r, d.x, d.z, d.heading, ln, ln / 2, 0.3 * alt);
        }
    }
    const C3 candy[4] = {{1, 0.4, 0.1}, {0.8, 0.2, 1.0}, {0.2, 1.0, 0.4}, {1, 0.9, 0.2}};
    for (auto& b : baits)
        for (int k = 0; k < 4; k++) {
            double a = k * 1.6 + b->x;
            out.push_back(box(b->x + std::cos(a) * 0.7, 0.35, b->z + std::sin(a) * 0.7, 0.55, 0.3, 0.3, candy[k], 1.5, a + t));
        }
    for (auto& p : dust) {  // ectoplasm puffs and witch sparks
        double k = std::min(1.0, p[6] / 0.9);
        double s = 0.3 + 0.4 * (1 - k);
        out.push_back(box(p[0], p[1], p[2], s, s, s, {0.5 + 0.3 * k, 1.0 * k, 0.4 + 0.4 * k}, 1.2, 0.0, 0.0));
    }
    for (auto& rg : rings) {
        double fade = 1 - (t - rg[2]) / 1.4;
        for (int k = 0; k < 20; k++) {
            double a = k * TAU / 20;
            out.push_back(box(rg[0] + std::cos(a) * rg[3], 0.15, rg[1] + std::sin(a) * rg[3], 1.4 * fade + 0.2, 0.25, 0.5, {0.65, 0.3, 1.0}, 1.4 * fade, -a, 0.2));
        }
    }
    return out;
}
