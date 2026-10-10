// Frogs live around a lily pond and travel in real hops; ladybugs and beetles walk on six legs with a tripod gait; a
// poked ladybug opens its shell and flies, a poked beetle rears up; butterflies flutter on hinged wings.
#include "forest.hpp"

#include <algorithm>
#include <cmath>
#include <map>

#include "../gfx/renderer.hpp"

namespace {
const double EDGE = 0.14;  // soft outlines: this theme should look natural, not neon
const double POND[3] = {-104.0, -38.0, 15.0};       // x, z, radius
const double CLEAR[4] = {-72.0, 72.0, -160.0, 22.0};  // x0, x1, z0, z1: where the pedestals live

struct Species {
    const char *name, *diet, *size, *habitat, *fact;
    double speed, radius, pick_h, pick_r;
    const char *eats, *verb;
    double sc;
};
const std::map<std::string, Species> SPECIES = {
    {"frog", {"Tree Frog", "Flies, berries in a pinch", "7 cm", "pond edges", "Can jump twenty times its own length - and does, when poked.", 0.0, 3.0, 0.9, 1.9, "berries", "croaks RIBBIT", 2.6}},
    {"ladybug", {"Ladybug", "Aphids", "8 mm", "leaves and stems", "Counts its spots to tell the time (it does not).", 3.4, 2.4, 0.6, 1.6, "berries", "pops its shell and flies off", 2.4}},
    {"stag", {"Stag Beetle", "Tree sap", "7 cm", "old logs", "Those huge jaws are for wrestling rivals, not for eating.", 2.6, 3.3, 0.7, 2.0, "berries", "clacks its jaws", 2.4}},
    {"rhino", {"Rhinoceros Beetle", "Fruit and sap", "6 cm", "the forest floor", "Lifts 850 times its own weight - pound for pound the strongest.", 2.3, 3.3, 0.7, 2.0, "berries", "rears its horn", 2.4}},
    {"jewel", {"Jewel Beetle", "Nectar and leaves", "4 cm", "sunny branches", "Its shell has no pigment: the colour is light bending in the layers.", 3.1, 2.7, 0.6, 1.7, "berries", "shimmers", 2.4}},
    {"butterfly", {"Butterfly", "Nectar, fruit juice", "10 cm span", "meadow flowers", "Tastes with its feet before it sips.", 5.5, 0.0, 0.0, 2.2, "berries", "flutters away", 2.3}},
};
std::vector<std::string> kinds() {
    std::vector<std::string> k(6, "frog");
    for (int i = 0; i < 6; i++) k.push_back("ladybug");
    for (auto s : {"stag", "stag", "rhino", "rhino", "jewel", "jewel"}) k.push_back(s);
    for (int i = 0; i < 10; i++) k.push_back("butterfly");
    return k;
}

struct Wings {
    C3 base, border, spot, hind;
    double glow;
};
const std::vector<std::string> BUTTERFLY_NAMES = {"monarch", "morpho", "lemon", "cabbage", "swallowtail"};
const std::map<std::string, Wings> BUTTERFLIES = {
    {"monarch", {{1.0, 0.5, 0.05}, {0.06, 0.04, 0.03}, {1.0, 1.0, 0.95}, {1.0, 0.5, 0.05}, 0.55}},
    {"morpho", {{0.15, 0.5, 1.0}, {0.04, 0.05, 0.1}, {0.85, 0.95, 1.0}, {0.1, 0.35, 0.9}, 1.1}},
    {"lemon", {{1.0, 0.9, 0.25}, {0.75, 0.55, 0.05}, {1.0, 0.5, 0.1}, {0.95, 0.85, 0.2}, 0.6}},
    {"cabbage", {{0.96, 0.96, 0.9}, {0.15, 0.15, 0.15}, {0.12, 0.12, 0.12}, {0.92, 0.92, 0.85}, 0.55}},
    {"swallowtail", {{1.0, 0.85, 0.3}, {0.05, 0.05, 0.05}, {0.3, 0.55, 1.0}, {1.0, 0.3, 0.15}, 0.6}},
};

const C3 BLACK{0.04, 0.04, 0.05}, WHITE{0.95, 0.95, 0.92};
const C3 FROG_G{0.32, 0.66, 0.16}, FROG_D{0.16, 0.38, 0.08}, FROG_B{0.88, 0.9, 0.6};

Beast make(const std::string& kind, int i, PyRandom& rnd) {
    Beast d;
    d.kind = kind;
    d.i = i;
    const Species& sp = SPECIES.at(kind);
    d.name = sp.name;
    d.diet = sp.diet;
    d.length = sp.size;
    d.era = sp.habitat;
    d.fact = sp.fact;
    d.speed = sp.speed;
    d.radius = sp.radius;
    d.pick_h = sp.pick_h;
    d.pick_r = sp.pick_r;
    d.eats = sp.eats;
    d.verb = sp.verb;
    d.sc = sp.sc;
    d.flyer = kind == "butterfly";
    d.heading = rnd.uniform(0, 6.28);
    d.phase = rnd.uniform(0, 6);
    d.idle = rnd.uniform(0, 3);
    d.seed = rnd.random() * 100;
    d.y = 0.0;
    d.fly_until = -1.0;
    double a = rnd.uniform(0, TAU), rad = rnd.uniform(50, 110);
    d.x = std::sin(a) * rad;
    d.z = std::cos(a) * rad;
    if (kind == "frog") {
        double a2 = rnd.uniform(0, TAU), rad2 = POND[2] + rnd.uniform(2, 14);
        d.x = POND[0] + std::sin(a2) * rad2;
        d.z = POND[1] + std::cos(a2) * rad2;
    }
    if (kind == "butterfly") {
        d.pal_name = rnd.choice(BUTTERFLY_NAMES);
        d.y = rnd.uniform(3, 8);
        d.vx = d.vy = d.vz = 0.0;
        d.flap_w = rnd.uniform(15, 20);
    }
    if (kind == "jewel") d.pal_k = rnd.random();
    return d;
}

// pitch a whole rig about a pivot (x, y) in the side plane (used when a beetle rears up)
std::vector<Part> rot_parts(const std::vector<Part>& P, double ang, double px, double py) {
    double c = std::cos(ang), s = std::sin(ang);
    std::vector<Part> out;
    for (auto p : P) {
        double dx = p.x - px, dy = p.y - py;
        p.x = px + dx * c - dy * s;
        p.y = py + dx * s + dy * c;
        p.rz += ang;
        out.push_back(p);
    }
    return out;
}

// six legs with a tripod gait: legs 0, 3, 4 step while 1, 2, 5 push (and the other way round)
void six_legs(std::vector<Part>& P, const Beast& d, const std::vector<V3>& socks, double reach, double lift_h, double thick, C3 col, double mv,
              double knee_h = 0.45, bool three = false) {
    double ph = d.phase * 6.0;
    for (size_t k = 0; k < socks.size(); k++) {
        const V3& s = socks[k];
        int side = s.z > 0 ? 1 : -1;
        double grp = (k == 0 || k == 3 || k == 4) ? 0 : PI;
        double stride = std::sin(ph + grp) * 0.32 * mv;
        double lift = std::max(0.0, std::cos(ph + grp)) * lift_h * mv;
        const double splays[3] = {0.35, 0.0, -0.4};  // front legs reach forward, back legs back
        double splay = splays[k / 2];
        V3 foot{s.x + splay + stride, 0.03 + lift, s.z + side * reach};
        V3 knee{s.x + splay * 0.5 + stride * 0.5, s.y + knee_h + lift * 0.5, s.z + side * reach * 0.55};
        if (three) {
            V3 hip{s.x, s.y + 0.05, s.z + side * 0.12};
            P.push_back(limb(s, hip, thick * 1.2, col));
            P.push_back(limb(hip, knee, thick, col));
        } else {
            P.push_back(limb(s, knee, thick, col));
        }
        P.push_back(limb(knee, foot, thick * 0.8, col));
        P.push_back({foot.x, foot.y, foot.z, thick * 1.1, thick * 0.6, thick * 1.1, col, 0.1, 0, 0, 0});
    }
}

struct St {
    double ph, mv, roar, look, air, throat, open;
    bool eat;
};

// ------------------------------------------------------------ rigs: local frame, +x forward, +z side, y up from the ground
std::vector<Part> rig_frog(const Beast& d, double t, const St& st) {
    std::vector<Part> P;
    double air = st.air, throat = st.throat, r = st.roar;
    double tilt = 0.28 * (1 - air);  // sits nose-up, stretches flat in the air
    P.push_back({0, 0.78, 0, 1.8, 0.95, 1.35, FROG_G, 0.3, tilt, 0, 0});        // body
    P.push_back({-0.15, 1.08, 0, 1.4, 0.5, 1.1, FROG_D, 0.25, tilt, 0, 0});     // darker back
    const double spots[4][2] = {{-0.4, 0.25}, {0.15, -0.3}, {-0.65, -0.2}, {0.05, 0.35}};
    for (auto& s : spots) P.push_back({s[0], 1.3 + s[0] * 0.25, s[1], 0.26, 0.08, 0.26, {0.1, 0.25, 0.05}, 0.2, tilt, 0, 0});
    P.push_back({0.1, 0.42, 0, 1.5, 0.36, 1.12, FROG_B, 0.3, tilt, 0, 0});  // belly
    double hy = 1.0 + 0.15 * r;
    P.push_back({0.85, hy, 0, 0.92, 0.72, 1.18, FROG_G, 0.3, tilt * 0.6, 0, st.look});                  // head
    P.push_back({1.13, hy - 0.2, 0, 0.5, 0.05, 1.12, {0.12, 0.2, 0.05}, 0.1, tilt * 0.6, 0, st.look});  // mouth line
    double puff = throat + 0.8 * r;
    P.push_back({0.98, 0.58, 0, 0.5, 0.3 + 0.38 * puff, 0.66 + 0.4 * puff, FROG_B, 0.35 + 0.4 * puff, 0, 0, 0});  // throat sac
    double blink = pmod(t + d.seed, 4.3) < 0.12 ? 0.15 : 1.0;
    for (int sz : {-1, 1}) {
        P.push_back({0.82, hy + 0.42, sz * 0.4, 0.44, 0.44 * blink, 0.44, {0.95, 0.8, 0.15}, 0.7, 0, 0, 0});  // bulging eyes
        P.push_back({1.04, hy + 0.44, sz * 0.42, 0.06, 0.2 * blink, 0.28, BLACK, 0.0, 0, 0, 0});           // horizontal pupils
        P.push_back({0.78, hy + 0.66, sz * 0.4, 0.38, 0.08, 0.38, FROG_G, 0.3, 0, 0, 0});                    // lid ridge
        // front legs: planted while sitting, swept back in the air
        V3 sock{0.6, 0.55, sz * 0.58};
        V3 hand{1.05 - 0.75 * air, 0.05 + 0.3 * air, sz * (0.72 + 0.1 * air)};
        V3 elbow{(sock.x + hand.x) / 2 + 0.1, 0.35 + 0.1 * air, sz * 0.82};
        P.push_back(limb(sock, elbow, 0.2, FROG_G));
        P.push_back(limb(elbow, hand, 0.17, FROG_G));
        for (int f : {-1, 0, 1}) {  // toes with sticky pads
            P.push_back({hand.x + 0.15, hand.y, hand.z + f * 0.12, 0.2, 0.06, 0.07, FROG_D, 0.2, 0, 0, 0});
            P.push_back({hand.x + 0.26, hand.y, hand.z + f * 0.14, 0.09, 0.08, 0.09, FROG_B, 0.3, 0, 0, 0});
        }
        // back legs: folded Z under the body while sitting, fully extended behind during a hop
        V3 hip{-0.72, 0.62, sz * 0.6};
        V3 knee{-0.1 - 1.2 * air, 0.5, sz * (1.05 - 0.2 * air)};
        V3 ankle{-0.95 - 1.15 * air, 0.18 + 0.15 * air, sz * (0.95 - 0.15 * air)};
        V3 foot{-0.3 - 2.15 * air, 0.06 + 0.2 * air, sz * (1.0 - 0.2 * air)};
        P.push_back(limb(hip, knee, 0.42, FROG_G));
        P.push_back(limb(knee, ankle, 0.32, FROG_G));
        P.push_back(limb(ankle, foot, 0.2, FROG_D));
        P.push_back({foot.x + (air < 0.5 ? 0.2 : -0.2), foot.y, foot.z, 0.5, 0.05, 0.42, FROG_D, 0.2, 0, 0, 0});  // webbed foot
    }
    return P;
}

std::vector<Part> rig_ladybug(const Beast& d, double t, const St& st) {
    std::vector<Part> P;
    double op = st.open;
    C3 red{0.92, 0.08, 0.05};
    double glossy = 0.4;
    six_legs(P, d, {{0.35, 0.22, -0.3}, {0.35, 0.22, 0.3}, {0.0, 0.22, -0.34}, {0.0, 0.22, 0.34}, {-0.35, 0.22, -0.3}, {-0.35, 0.22, 0.3}}, 0.42, 0.12,
             0.06, BLACK, op < 0.5 ? st.mv : 0.0, 0.16);
    P.push_back({0, 0.3, 0, 1.25, 0.24, 0.95, BLACK, 0.1, 0, 0, 0});     // underside
    P.push_back({0.56, 0.5, 0, 0.36, 0.3, 0.85, BLACK, 0.15, 0, 0, 0});  // pronotum
    P.push_back({0.56, 0.66, 0, 0.3, 0.06, 0.6, BLACK, 0.15, 0, 0, 0});
    for (int sz : {-1, 1}) P.push_back({0.6, 0.53, sz * 0.34, 0.24, 0.2, 0.16, WHITE, 0.3, 0, 0, 0});
    P.push_back({0.8, 0.38, 0, 0.34, 0.3, 0.52, BLACK, 0.15, 0, 0, 0});  // head
    for (int sz : {-1, 1}) {
        P.push_back({0.95, 0.55, sz * 0.17, 0.12, 0.1, 0.12, WHITE, 0.4, 0, 0, 0});
        P.push_back(limb({0.95, 0.58, sz * 0.12}, {1.3, 0.86 + 0.04 * std::sin(t * 4 + sz), sz * 0.32}, 0.04, BLACK));  // antennae
        P.push_back({1.3, 0.86 + 0.04 * std::sin(t * 4 + sz), sz * 0.32, 0.09, 0.09, 0.09, BLACK, 0.1, 0, 0, 0});
    }
    // elytra: each red half hinges at the seam and swings up and out to fly
    const double piv_y = 0.86;
    const double spots[4][3] = {{0.25, 0.84, 0.3}, {-0.2, 0.9, 0.22}, {-0.45, 0.76, 0.42}, {0.02, 0.66, 0.53}};
    for (int sz : {-1, 1}) {
        double a = -sz * op * 1.25;
        double ca = std::cos(a), sa = std::sin(a);
        auto hinge = [&](double x, double y, double z) {
            double dy = y - piv_y, dz = z;
            return V3{x, piv_y + dy * ca - dz * sa, dy * sa + dz * ca};
        };
        struct L {
            double x, y, z, sx, sy, sz;
            C3 col;
            double gl;
        };
        const L layers[5] = {{-0.05, 0.52, sz * 0.27, 1.4, 0.36, 0.58, red, glossy},  // a dome from stacked, tapering layers
                             {-0.05, 0.7, sz * 0.25, 1.28, 0.16, 0.52, red, glossy},
                             {-0.05, 0.8, sz * 0.21, 1.08, 0.12, 0.42, red, glossy + 0.05},
                             {-0.08, 0.88, sz * 0.15, 0.8, 0.08, 0.3, red, glossy + 0.1},
                             {0.22, 0.9, sz * 0.14, 0.26, 0.04, 0.12, {1.0, 0.6, 0.55}, 0.7}};  // gloss highlight
        for (auto& l : layers) {
            V3 h = hinge(l.x, l.y, l.z);
            P.push_back({h.x, h.y, h.z, l.sx, l.sy, l.sz, l.col, l.gl, 0, a, 0});
        }
        for (auto& s : spots) {
            V3 h = hinge(s[0], s[1], sz * s[2]);
            P.push_back({h.x, h.y + 0.01, h.z, 0.22, 0.07, 0.22, BLACK, 0.05, 0, a, 0});
        }
        if (op > 0.05) {  // hind wings unfold and buzz (two phases drawn so the beat reads as a blur)
            for (int k = 0; k < 2; k++) {
                double fl = sz * (0.35 + 0.45 * std::sin(t * 42 + k * 1.6));
                P.push_back({-0.25, 0.9 + 0.15 * op, sz * 0.95 * op, 1.25 * op, 0.03, 1.2 * op, {0.85, 0.85, 0.8}, 0.6, -0.15, fl, 0});
            }
        }
    }
    P.push_back({0.5, 0.82, 0, 0.14, 0.08, 0.22, BLACK, 0.05, 0, 0, 0});  // the spot on the seam
    return P;
}

std::vector<Part> rig_beetle(const Beast& d, double t, const St& st) {
    std::vector<Part> P;
    double r = st.roar, mv = st.mv;
    const std::string& kind = d.kind;
    C3 shell, dark, sheen;
    double glow;
    if (kind == "jewel") {  // structural colour: shifts with the light
        double k = 0.5 + 0.5 * std::sin(t * 0.8 + d.pal_k * 6);
        double g2 = 0.5 + 0.5 * std::sin(t * 0.5 + d.pal_k * 4 + 2);
        const double A[3] = {0.1, 0.72, 0.35}, B[3] = {0.1, 0.45, 0.85}, Cc[3] = {0.85, 0.7, 0.15};
        double v[3];
        for (int i = 0; i < 3; i++) v[i] = (1 - k) * A[i] + k * (B[i] * (1 - g2) + Cc[i] * g2);
        shell = C3(v[0], v[1], v[2]);
        dark = C3(0.05, 0.2, 0.12);
        sheen = C3(std::min(1.0, v[0] * 1.4 + 0.15), std::min(1.0, v[1] * 1.4 + 0.15), std::min(1.0, v[2] * 1.4 + 0.15));
        glow = 0.75;
    } else if (kind == "stag") {
        shell = C3(0.32, 0.15, 0.07);
        dark = C3(0.14, 0.07, 0.04);
        sheen = C3(0.75, 0.45, 0.3);
        glow = 0.3;
    } else {
        shell = C3(0.17, 0.1, 0.06);
        dark = C3(0.07, 0.05, 0.04);
        sheen = C3(0.6, 0.45, 0.35);
        glow = 0.3;
    }
    six_legs(P, d, {{0.45, 0.32, -0.36}, {0.45, 0.32, 0.36}, {0.0, 0.32, -0.42}, {0.0, 0.32, 0.42}, {-0.45, 0.32, -0.38}, {-0.45, 0.32, 0.38}}, 1.0, 0.2,
             0.09, dark, mv, 0.42, true);
    P.push_back({-0.4, 0.5, 0, 1.6, 0.46, 1.1, shell, glow, 0, 0, 0});  // elytra, rounded by stacked tapering layers
    P.push_back({-0.4, 0.74, 0, 1.5, 0.12, 0.98, shell, glow, 0, 0, 0});
    P.push_back({-0.42, 0.83, 0, 1.3, 0.1, 0.8, shell, glow + 0.05, 0, 0, 0});
    P.push_back({-0.45, 0.9, 0, 1.0, 0.06, 0.55, shell, glow + 0.1, 0, 0, 0});
    P.push_back({-1.18, 0.55, 0, 0.2, 0.4, 0.8, shell, glow, 0, 0, 0});    // rounded rear
    P.push_back({-0.42, 0.94, 0, 1.3, 0.03, 0.04, dark, 0.05, 0, 0, 0});   // seam
    for (int sz : {-1, 1}) {
        P.push_back({-0.2, 0.92, sz * 0.18, 0.7, 0.02, 0.05, sheen, 0.55, 0, 0, 0});  // a soft highlight
        for (int k = 0; k < 2; k++) P.push_back({-0.4, 0.8 - k * 0.14, sz * (0.42 + k * 0.08), 1.35, 0.025, 0.025, dark, 0.05, 0, 0, 0});  // grooves
    }
    P.push_back({0.52, 0.52, 0, 0.62, 0.44, 0.98, shell, glow, 0, 0, 0});  // thorax
    P.push_back({0.52, 0.72, 0, 0.5, 0.08, 0.8, shell, glow, 0, 0, 0});
    P.push_back({0.98, 0.46, 0, 0.42, 0.34, kind != "stag" ? 0.72 : 0.95, dark, 0.2, 0, 0, st.look});  // head
    for (int sz : {-1, 1}) {
        P.push_back({1.12, 0.6, sz * 0.3, 0.12, 0.12, 0.12, {0.05, 0.05, 0.05}, 0.6, 0, 0, 0});  // eyes
        V3 base{1.15, 0.55, sz * 0.18};
        P.push_back(limb(base, {1.5, 0.8, sz * 0.42}, 0.04, dark));  // elbowed antennae with a club
        P.push_back(limb({1.5, 0.8, sz * 0.42}, {1.75, 0.85, sz * 0.5}, 0.04, dark));
        P.push_back({1.78, 0.86, sz * 0.52, 0.14, 0.1, 0.1, dark, 0.2, 0, 0, 0});
    }
    if (kind == "stag") {  // antler jaws that open and clack when poked
        double clack = r * (0.5 + 0.5 * std::sin(t * 22));
        for (int sz : {-1, 1}) {
            C3 jaw{0.5, 0.2, 0.09};
            V3 a0{1.15, 0.5, sz * 0.32};
            V3 a1{1.6, 0.66 + 0.2 * r, sz * (0.55 + 0.12 * clack)};
            V3 a2{2.05, 0.78 + 0.3 * r, sz * (0.48 + 0.1 * clack)};
            V3 a3{2.35, 0.8 + 0.35 * r, sz * (0.2 + 0.25 * clack)};  // the tips curve in toward each other
            P.push_back(limb(a0, a1, 0.13, jaw));
            P.push_back(limb(a1, a2, 0.1, jaw));
            P.push_back(limb(a2, a3, 0.08, jaw));
            P.push_back(limb({1.85, 0.72 + 0.25 * r, sz * (0.52 + 0.1 * clack)}, {1.88, 0.92 + 0.25 * r, sz * (0.44 + 0.1 * clack)}, 0.06, jaw));  // tooth
        }
    } else if (kind == "rhino") {  // a curved head horn and a short one on the thorax
        double lift = 0.35 * r;
        P.push_back(limb({1.15, 0.65, 0}, {1.55, 1.15 + lift, 0}, 0.2, dark));
        P.push_back(limb({1.55, 1.15 + lift, 0}, {1.62, 1.6 + lift, 0}, 0.14, dark));
        P.push_back(limb({1.62, 1.6 + lift, 0}, {1.45, 1.85 + lift, 0}, 0.1, dark));
        P.push_back(limb({0.6, 0.82, 0}, {1.05, 1.15 + lift * 0.5, 0}, 0.16, dark));
    }
    if (r > 0) P = rot_parts(P, 0.45 * r, -1.0, 0.3);  // rear up on the back legs
    return P;
}

std::vector<Part> rig_butterfly(const Beast& d, double t, const St& st) {
    std::vector<Part> P;
    const Wings& W = BUTTERFLIES.at(d.pal_name);
    double rate = d.flap_w * (st.eat ? 0.45 : 1.0) * (st.roar ? 1.6 : 1.0);
    double f = 0.2 + 1.05 * (0.5 + 0.5 * std::sin(t * rate + d.seed));  // wing angle above level
    if (st.eat) f = 0.9 + 0.5 * (0.5 + 0.5 * std::sin(t * rate + d.seed));  // slow open-and-close while sipping
    C3 body{0.12, 0.09, 0.07};
    P.push_back({0.15, 0, 0, 0.42, 0.26, 0.26, body, 0.2, 0, 0, 0});     // thorax
    P.push_back({-0.4, -0.04, 0, 0.75, 0.17, 0.17, body, 0.2, 0.08, 0, 0});  // abdomen
    P.push_back({0.45, 0.02, 0, 0.2, 0.2, 0.2, body, 0.2, 0, 0, 0});     // head
    for (int sz : {-1, 1}) {
        P.push_back(limb({0.5, 0.08, sz * 0.05}, {1.0, 0.45, sz * 0.25}, 0.03, body));  // antennae
        P.push_back({1.0, 0.45, sz * 0.25, 0.08, 0.08, 0.08, body, 0.2, 0, 0, 0});
        double a = -sz * f;
        double ca = std::cos(a), sa = std::sin(a);
        auto w = [&](double x, double z, double sx, double sz_, C3 col, double gl, double rz = 0.0, double y = 0.0) {
            double zz = sz * z;
            P.push_back({x, y * ca - zz * sa, y * sa + zz * ca, sx, 0.035, sz_, col, gl, rz, a, 0});
        };
        double lag = std::sin(t * rate + d.seed - 0.5);  // the hind wing trails a little
        // forewing: strips from the root outward, longer and swept forward toward the tip -> a triangular wing
        const double fore[6][3] = {{0.18, 0.62, 0.0}, {0.3, 0.86, 0.06}, {0.52, 1.0, 0.14}, {0.78, 1.0, 0.24}, {1.02, 0.86, 0.3}, {1.22, 0.56, 0.34}};
        for (int k = 0; k < 6; k++) {
            bool lastk = k >= 6 - 2;
            double zc = fore[k][0], ln = fore[k][1], fwd = fore[k][2];
            w(0.12 + fwd, zc, ln, 0.26, lastk ? W.border : W.base, lastk ? 0.1 : W.glow);
            w(0.12 + fwd + ln / 2 - 0.06, zc, 0.12, 0.26, W.border, 0.1);  // dark leading edge
        }
        for (int k = 0; k < 4; k++) w(0.25 + k * 0.17, 1.18 + 0.05 * (k % 2), 0.07, 0.07, W.spot, 0.7, 0, 0.02);  // white dots in the dark tip
        w(0.35, 0.62, 0.8, 0.03, W.border, 0.1, 0, 0.02);  // veins
        w(0.45, 0.95, 0.6, 0.03, W.border, 0.1, 0, 0.02);
        // hindwing: rounder, trailing behind the forewing
        double hy = 0.04 * lag;
        const double hind_s[5][2] = {{0.18, 0.7}, {0.36, 0.86}, {0.56, 0.86}, {0.76, 0.72}, {0.94, 0.48}};
        for (int k = 0; k < 5; k++) {
            bool lastk = k == 4;
            double zc = hind_s[k][0], ln = hind_s[k][1];
            w(-0.3, zc, ln, 0.22, lastk ? W.border : W.hind, lastk ? 0.1 : W.glow, 0, hy);
            w(-0.3 - ln / 2 + 0.05, zc, 0.1, 0.22, W.border, 0.1, 0, hy);  // dark trailing edge
        }
        if (d.pal_name == "swallowtail" || d.pal_name == "morpho" || d.pal_name == "monarch")
            w(-0.48, 0.7, 0.16, 0.16, d.pal_name != "swallowtail" ? W.spot : C3(1.0, 0.3, 0.1), 0.9, 0, hy + 0.02);
        if (d.pal_name == "swallowtail") w(-0.95, 0.82, 0.5, 0.1, W.border, 0.1, 0, hy);  // the tail
    }
    return P;
}

using RigFn = std::vector<Part> (*)(const Beast&, double, const St&);
RigFn rig_of(const std::string& k) {
    if (k == "frog") return rig_frog;
    if (k == "ladybug") return rig_ladybug;
    if (k == "butterfly") return rig_butterfly;
    return rig_beetle;
}
}  // namespace

Forest::Forest(long long seed) : rnd(seed) {
    auto ks = kinds();
    for (size_t i = 0; i < ks.size(); i++) dinos.push_back(make(ks[i], (int)i, rnd));
    static_ = scenery();
    clearing_cover();
    inner_vis_ = inner_;
}

bool Forest::free_(double x, double z, double margin) const {
    if (CLEAR[0] - margin < x && x < CLEAR[1] + margin && CLEAR[2] - margin < z && z < CLEAR[3] + margin) return false;
    if (std::hypot(x - POND[0], z - POND[1]) < POND[2] + 3 + margin) return false;
    for (auto& tr : trees)
        if (!(std::hypot(x - tr[0], z - tr[1]) > tr[2] + margin)) return false;
    return true;
}

std::optional<std::pair<double, double>> Forest::spot(PyRandom& r, double rmin, double rmax, double margin, int tries) const {
    for (int k = 0; k < tries; k++) {
        double a = r.uniform(0, TAU), rad = r.uniform(rmin, rmax);
        double x = std::sin(a) * rad, z = std::cos(a) * rad;
        if (free_(x, z, margin)) return std::make_pair(x, z);
    }
    return std::nullopt;
}

std::vector<Box> Forest::scenery() {
    PyRandom r(7);
    std::vector<Box> out;
    const std::vector<C3> leafs = {{0.18, 0.42, 0.12}, {0.22, 0.5, 0.14}, {0.15, 0.36, 0.1}, {0.28, 0.52, 0.16}, {0.32, 0.5, 0.12}};
    const C3 bark{0.3, 0.2, 0.12};

    auto oak = [&](double x, double z, double s) {
        double h = 7.5 * s;
        double u = r.uniform(0, 1);
        out.push_back(box(x, h / 2, z, 1.5 * s, h, 1.5 * s, bark, 0.08, u, EDGE));
        for (int k = 0; k < 4; k++) {  // root flare
            double a = k * PI / 2 + 0.4;
            out.push_back(box(x + std::cos(a) * 0.9 * s, 0.35 * s, z - std::sin(a) * 0.9 * s, 1.6 * s, 0.6 * s, 0.5 * s, bark, 0.05, a, EDGE, 0, 0, -0.35));
        }
        for (int k = 0; k < 4; k++) {  // branches
            double a = k * PI / 2 + r.uniform(0, 1);
            out.push_back(box(x + std::cos(a) * 1.6 * s, h * 0.82, z - std::sin(a) * 1.6 * s, 3.4 * s, 0.5 * s, 0.5 * s, bark, 0.05, a, EDGE, 0, 0, 0.55));
        }
        for (int k = 0; k < 9; k++) {  // lumpy canopy: overlapping leaf clusters at different heights
            double a = r.uniform(0, TAU), rr = r.uniform(0, 3.2) * s;
            double cs = r.uniform(3.2, 5.0) * s;
            double yy = h + r.uniform(0.3, 3.4) * s;
            C3 col = r.choice(leafs);
            double yaw = r.uniform(0, 1.5);
            out.push_back(box(x + std::cos(a) * rr, yy, z - std::sin(a) * rr, cs, cs * 0.75, cs, col, 0.12, yaw, EDGE));
        }
        trees.push_back({x, z, 1.4 * s});
    };
    auto pine = [&](double x, double z, double s) {
        double h = 11 * s;
        out.push_back(box(x, h / 2, z, 0.9 * s, h, 0.9 * s, {0.26, 0.17, 0.1}, 0.06, 0.0, EDGE));
        C3 needles{0.07, 0.25, 0.12};
        for (int k = 0; k < 6; k++) {  // stacked tiers, each two crossed square layers
            double y = 2.2 * s + k * 1.6 * s;
            double w = (5.6 - k * 0.8) * s;
            for (double yaw : {0.0, PI / 4}) out.push_back(box(x, y, z, w, 1.1 * s, w, needles * (0.9 + 0.05 * k), 0.1, yaw + k * 0.3, EDGE));
        }
        out.push_back(box(x, h + 0.6 * s, z, 0.8 * s, 1.6 * s, 0.8 * s, needles, 0.1, 0.0, EDGE));
        trees.push_back({x, z, 1.0 * s});
    };
    auto birch = [&](double x, double z, double s) {
        double h = 9 * s;
        out.push_back(box(x, h / 2, z, 0.8 * s, h, 0.8 * s, {0.9, 0.9, 0.85}, 0.15, 0.0, EDGE));
        for (int k = 0; k < 7; k++) {  // black bark marks
            double y = r.uniform(0.6, h - 0.5);
            int side = r.choice(std::vector<int>{-1, 1});
            double yaw = r.choice(std::vector<double>{0.0, PI / 2});
            out.push_back(box(x + side * 0.41 * s, y, z, 0.04, 0.12 * s, 0.4 * s, {0.08, 0.08, 0.08}, 0.0, yaw, 0.0));
        }
        for (int k = 0; k < 6; k++) {
            double a = r.uniform(0, TAU), rr = r.uniform(0, 2.0) * s;
            double cs = r.uniform(2.2, 3.4) * s;
            double yy = h * 0.8 + r.uniform(0, 3) * s;
            double yaw = r.uniform(0, 1.5);
            out.push_back(box(x + std::cos(a) * rr, yy, z - std::sin(a) * rr, cs, cs * 0.9, cs, {0.45, 0.62, 0.2}, 0.15, yaw, EDGE));
        }
        trees.push_back({x, z, 0.9 * s});
    };

    for (int i = 0; i < 115; i++) {  // trees: a ring of woodland round the clearing, thicker further out
        auto p = spot(r, 40, 210, 4);
        if (p) {
            double kind = r.random();
            double s = r.uniform(0.85, 1.35);
            if (kind < 0.42) oak(p->first, p->second, s);
            else if (kind < 0.8) pine(p->first, p->second, s);
            else birch(p->first, p->second, s);
        }
    }
    for (int i = 0; i < 34; i++) {  // bushes, some with berries
        auto p = spot(r, 32, 160, 1.5);
        if (!p) continue;
        double x = p->first, z = p->second;
        for (int k = 0; k < 5; k++) {
            double a = r.uniform(0, TAU), rr = r.uniform(0, 1.1);
            double cs = r.uniform(1.3, 2.2);
            C3 col = r.choice(leafs);
            double yaw = r.uniform(0, 1.5);
            out.push_back(box(x + std::cos(a) * rr, cs * 0.4, z - std::sin(a) * rr, cs, cs * 0.8, cs, col, 0.1, yaw, EDGE));
        }
        if (i % 3 == 0)
            for (int k = 0; k < 6; k++) {
                double a = r.uniform(0, TAU);
                double y = r.uniform(0.5, 1.4);
                out.push_back(box(x + std::cos(a) * 1.0, y, z - std::sin(a) * 1.0, 0.22, 0.22, 0.22, {0.85, 0.08, 0.15}, 0.9, 0.0, 0.0));
            }
    }
    for (int i = 0; i < 60; i++) {  // ferns: fronds fanning out from a crown
        auto p = spot(r, 28, 170, 0.5);
        if (!p) continue;
        double x = p->first, z = p->second;
        double s = r.uniform(0.8, 1.4);
        for (int k = 0; k < 8; k++) {
            double a = k / 8.0 * TAU + r.uniform(-0.2, 0.2);
            double ln = r.uniform(1.8, 2.6) * s;
            out.push_back(box(x + std::cos(a) * ln * 0.45, 0.55 * s, z - std::sin(a) * ln * 0.45, ln, 0.05, 0.42 * s,
                              k % 2 ? C3(0.2, 0.52, 0.16) : C3(0.25, 0.58, 0.2), 0.12, a, EDGE, 0, 0, 0.45));
        }
    }
    for (int i = 0; i < 55; i++) {  // mushrooms: red toadstools with white spots, brown clusters, a few glowing ones
        auto p = spot(r, 26, 150, 0.3);
        if (!p) continue;
        double x = p->first, z = p->second;
        double kind = r.random();
        if (kind < 0.45) {
            double s = r.uniform(0.8, 1.4);
            out.push_back(box(x, 0.55 * s, z, 0.32 * s, 1.1 * s, 0.32 * s, {0.95, 0.93, 0.85}, 0.2, 0.0, EDGE));
            out.push_back(box(x, 1.15 * s, z, 1.3 * s, 0.42 * s, 1.3 * s, {0.9, 0.12, 0.08}, 0.35, 0.3, EDGE));
            out.push_back(box(x, 1.4 * s, z, 0.9 * s, 0.2 * s, 0.9 * s, {0.9, 0.12, 0.08}, 0.35, 0.3, EDGE));
            for (int k = 0; k < 5; k++) {
                double a = k * 1.3 + r.random();
                out.push_back(box(x + std::cos(a) * 0.4 * s, 1.43 * s + 0.08 * (k % 2), z - std::sin(a) * 0.4 * s, 0.14 * s, 0.06, 0.14 * s, WHITE, 0.4, 0.0, 0.0));
            }
        } else if (kind < 0.85) {
            long long n = r.randint(3, 5);
            for (long long k = 0; k < n; k++) {
                double a = r.uniform(0, TAU), s = r.uniform(0.4, 0.8);
                double mx_ = x + std::cos(a) * 0.5, mz = z - std::sin(a) * 0.5;
                out.push_back(box(mx_, 0.4 * s, mz, 0.16 * s, 0.8 * s, 0.16 * s, {0.85, 0.78, 0.62}, 0.1, 0.0, EDGE));
                out.push_back(box(mx_, 0.85 * s, mz, 0.75 * s, 0.25 * s, 0.75 * s, {0.5, 0.32, 0.16}, 0.15, a, EDGE));
            }
        } else {
            for (int k = 0; k < 4; k++) {
                double a = r.uniform(0, TAU);
                double mx_ = x + std::cos(a) * 0.6, mz = z - std::sin(a) * 0.6;
                out.push_back(box(mx_, 0.25, mz, 0.1, 0.5, 0.1, {0.6, 0.85, 1.0}, 0.6, 0.0, 0.0));
                out.push_back(box(mx_, 0.52, mz, 0.42, 0.14, 0.42, {0.3, 0.8, 1.0}, 1.6, a, 0.0));
            }
        }
    }
    const std::vector<std::pair<C3, C3>> flowers = {{{0.97, 0.97, 0.92}, {1.0, 0.8, 0.1}}, {{0.55, 0.45, 1.0}, {1.0, 0.95, 0.4}},
                                                    {{1.0, 0.5, 0.7}, {1.0, 0.9, 0.3}},    {{1.0, 0.85, 0.15}, {0.6, 0.3, 0.05}},
                                                    {{0.95, 0.25, 0.2}, {0.1, 0.05, 0.05}}};
    for (int i = 0; i < 170; i++) {  // flowers: stem, petals around a centre
        auto p = spot(r, 24, 140, 0.0, 6);
        if (!p) continue;
        double x = p->first, z = p->second;
        auto [petal, middle] = r.choice(flowers);
        double h = r.uniform(0.7, 1.4);
        out.push_back(box(x, h / 2, z, 0.07, h, 0.07, {0.2, 0.5, 0.15}, 0.1, 0.0, 0.0));
        double ly = r.uniform(0, 3);
        out.push_back(box(x + 0.2, h * 0.4, z, 0.4, 0.04, 0.16, {0.25, 0.55, 0.18}, 0.1, ly, 0.0));
        double yaw = r.uniform(0, 1);
        out.push_back(box(x, h, z, 0.62, 0.06, 0.24, petal, 0.45, yaw, 0.0));
        out.push_back(box(x, h, z, 0.24, 0.06, 0.62, petal, 0.45, yaw, 0.0));
        out.push_back(box(x, h, z, 0.5, 0.06, 0.5, petal, 0.45, yaw + PI / 4, 0.0));
        out.push_back(box(x, h + 0.04, z, 0.2, 0.08, 0.2, middle, 0.7, yaw, 0.0));
    }
    const std::vector<C3> grasses = {{0.25, 0.5, 0.14}, {0.3, 0.58, 0.16}, {0.36, 0.55, 0.18}, {0.45, 0.55, 0.2}};
    for (int i = 0; i < 320; i++) {  // grass tufts: three blades leaning outward
        auto p = spot(r, 22, 170, 0.0, 4);
        if (!p) continue;
        double x = p->first, z = p->second;
        C3 g = r.choice(grasses);
        for (int k = 0; k < 3; k++) {
            double a = k * 2.1 + r.uniform(0, 1);
            double h = r.uniform(0.7, 1.5);
            out.push_back(box(x + std::cos(a) * 0.12, h / 2, z - std::sin(a) * 0.12, 0.08, h, 0.03, g, 0.08, a, 0.0, 0, 0, 0.0, 0.25));
        }
    }
    for (int i = 0; i < 26; i++) {  // mossy rocks
        auto p = spot(r, 30, 160, 1.0);
        if (!p) continue;
        double x = p->first, z = p->second;
        double s = r.uniform(1.0, 2.6);
        double yaw = r.uniform(0, 1.5);
        out.push_back(box(x, 0.45 * s, z, 1.8 * s, 0.9 * s, 1.4 * s, {0.42, 0.43, 0.4}, 0.08, yaw, EDGE));
        out.push_back(box(x + 0.2 * s, 0.85 * s, z, 1.2 * s, 0.5 * s, 1.0 * s, {0.45, 0.46, 0.43}, 0.08, yaw + 0.4, EDGE));
        out.push_back(box(x, 0.95 * s, z, 1.5 * s, 0.25 * s, 1.15 * s, {0.25, 0.48, 0.15}, 0.12, yaw, EDGE));  // moss cap
    }
    for (int i = 0; i < 7; i++) {  // fallen logs with moss, a cut end and a few shelf mushrooms
        auto p = spot(r, 40, 150, 4.0);
        if (!p) continue;
        double x = p->first, z = p->second;
        double yaw = r.uniform(0, PI), ln = r.uniform(7, 11), rr = r.uniform(0.7, 1.0);
        for (double a : {0.0, PI / 4}) out.push_back(box(x, rr, z, ln, rr * 2, rr * 2, {0.33, 0.22, 0.13}, 0.06, yaw, EDGE, 0, 0, 0.0, a));
        out.push_back(box(x, rr * 1.9, z, ln * 0.7, 0.3, rr * 1.3, {0.25, 0.5, 0.15}, 0.12, yaw, EDGE));
        double ex = x + std::cos(yaw) * ln / 2, ez = z - std::sin(yaw) * ln / 2;
        out.push_back(box(ex, rr, ez, 0.06, rr * 1.7, rr * 1.7, {0.75, 0.6, 0.4}, 0.2, yaw, 0.0));  // rings on the cut end
        out.push_back(box(ex, rr, ez, 0.08, rr * 1.0, rr * 1.0, {0.55, 0.4, 0.25}, 0.2, yaw, 0.0));
        for (int k = 0; k < 3; k++) {
            double f = (k - 1) * 0.25 * ln;
            out.push_back(box(x + std::cos(yaw) * f + std::sin(yaw) * rr, rr * 1.2, z - std::sin(yaw) * f + std::cos(yaw) * rr, 0.7, 0.12, 0.5, {0.85, 0.6, 0.3}, 0.3, yaw, EDGE));
        }
        trees.push_back({x, z, 2.0});
    }
    // the pond: layered water, a ring of stones, lily pads with a flower, reeds and cattails
    double px = POND[0], pz = POND[1], pr = POND[2];
    for (int k = 0; k < 3; k++) {
        out.push_back(box(px, 0.03, pz, pr * 2, 0.06, pr * 2 * 0.42, {0.1, 0.32, 0.4}, 0.35, k * PI / 3, 0.0));
        out.push_back(box(px, 0.05, pz, pr * 1.6, 0.06, pr * 1.6 * 0.42, {0.12, 0.4, 0.5}, 0.45, k * PI / 3 + 0.5, 0.0));
    }
    for (int k = 0; k < 26; k++) {
        double a = k / 26.0 * TAU + r.uniform(-0.05, 0.05);
        double s = r.uniform(0.7, 1.4);
        out.push_back(box(px + std::cos(a) * (pr + 0.6), 0.3 * s, pz - std::sin(a) * (pr + 0.6), 1.3 * s, 0.6 * s, 1.0 * s, {0.48, 0.47, 0.44}, 0.08, a, EDGE));
    }
    for (int k = 0; k < 14; k++) {
        double a = r.uniform(0, TAU), rr = r.uniform(0, pr * 0.8);
        double s = r.uniform(0.9, 1.6);
        double lx = px + std::cos(a) * rr, lz = pz - std::sin(a) * rr;
        double yaw = r.uniform(0, TAU);
        for (int q = 0; q < 3; q++) out.push_back(box(lx, 0.1, lz, 1.4 * s, 0.05, 0.6 * s, {0.2, 0.55, 0.18}, 0.2, yaw + q * PI / 3, 0.0));  // round pad
        if (k % 4 == 0) {  // a water lily on it
            for (int q = 0; q < 3; q++) out.push_back(box(lx, 0.32, lz, 0.6, 0.18, 0.2, {1.0, 0.7, 0.85}, 0.8, yaw + q * PI / 3, 0.0));
            out.push_back(box(lx, 0.42, lz, 0.18, 0.14, 0.18, {1.0, 0.9, 0.3}, 1.0, 0.0, 0.0));
        }
    }
    for (int k = 0; k < 22; k++) {  // cattails and reeds on the bank
        double a = r.uniform(0, TAU);
        double rr = pr + r.uniform(-0.8, 1.5);
        double cx = px + std::cos(a) * rr, cz = pz - std::sin(a) * rr;
        double h = r.uniform(2.2, 3.6);
        double lean = r.uniform(-0.12, 0.12);
        out.push_back(box(cx, h / 2, cz, 0.08, h, 0.08, {0.35, 0.55, 0.2}, 0.1, a, 0.0, 0, 0, lean));
        if (k % 2 == 0) out.push_back(box(cx - std::sin(lean) * h * 0.4, h * 0.85, cz, 0.24, 0.7, 0.24, {0.4, 0.22, 0.1}, 0.15, a, EDGE, 0, 0, lean));
        out.push_back(box(cx + 0.2, h * 0.35, cz, 0.06, h * 0.7, 0.2, {0.3, 0.5, 0.18}, 0.1, a + 1, 0.0, 0, 0, 0.3));
    }
    trees.push_back({px, pz, pr});  // walkers go round the water (frogs stay on the bank)
    // a low warm sun behind the trees, with a soft halo
    out.push_back(box(-60, 70, -230, 34, 34, 2, {1.0, 0.92, 0.65}, 1.7, 0.0, 0.0, 0.0, 1.0));
    out.push_back(box(-60, 70, -231, 50, 50, 1, {0.65, 0.6, 0.35}, 0.9, 0.0, 0.0, 0.0, 1.0));
    return out;
}

// flowers, grass, ferns and mushrooms scattered through the clearing where the pedestals stand; each box remembers the
// plant it belongs to so whole plants can be hidden under the section platforms
void Forest::clearing_cover() {
    PyRandom r(11);
    const std::vector<std::pair<C3, C3>> flowers = {{{0.97, 0.97, 0.92}, {1.0, 0.8, 0.1}}, {{0.55, 0.45, 1.0}, {1.0, 0.95, 0.4}},
                                                    {{1.0, 0.5, 0.7}, {1.0, 0.9, 0.3}},    {{1.0, 0.85, 0.15}, {0.6, 0.3, 0.05}}};
    const std::vector<C3> grasses = {{0.25, 0.5, 0.14}, {0.3, 0.58, 0.16}, {0.36, 0.55, 0.18}};
    auto add = [&](const std::vector<Box>& items, double x, double z) {
        for (auto& b : items) {
            inner_.push_back(b);
            inner_xz_.push_back({x, z});
        }
    };
    for (int i = 0; i < 520; i++) {
        double x = r.uniform(CLEAR[0] - 6, CLEAR[1] + 6), z = r.uniform(CLEAR[2] - 6, CLEAR[3] + 10);
        double kind = r.random();
        if (kind < 0.5) {  // grass
            C3 g = r.choice(grasses);
            double hs[3] = {r.uniform(0.6, 1.3), 0, 0};
            hs[1] = r.uniform(0.6, 1.3);
            hs[2] = r.uniform(0.6, 1.3);
            std::vector<Box> items;
            for (int k = 0; k < 3; k++)
                items.push_back(box(x + std::cos(k * 2.1) * 0.12, hs[k] / 2, z - std::sin(k * 2.1) * 0.12, 0.08, hs[k], 0.03, g, 0.08, k * 2.1 + i, 0.0, 0, 0, 0.0, 0.25));
            add(items, x, z);
        } else if (kind < 0.85) {  // flower
            auto [petal, middle] = r.choice(flowers);
            double h = r.uniform(0.6, 1.2);
            double yaw = r.uniform(0, 1);
            add({box(x, h / 2, z, 0.07, h, 0.07, {0.2, 0.5, 0.15}, 0.1, 0.0, 0.0), box(x, h, z, 0.6, 0.06, 0.22, petal, 0.45, yaw, 0.0),
                 box(x, h, z, 0.22, 0.06, 0.6, petal, 0.45, yaw, 0.0), box(x, h + 0.04, z, 0.2, 0.08, 0.2, middle, 0.7, yaw, 0.0)},
                x, z);
        } else if (kind < 0.95) {  // small fern
            std::vector<Box> items;
            for (int k = 0; k < 6; k++) {
                double a = k / 6.0 * TAU;
                items.push_back(box(x + std::cos(a) * 0.7, 0.4, z - std::sin(a) * 0.7, 1.6, 0.05, 0.34, {0.22, 0.55, 0.18}, 0.12, a, EDGE, 0, 0, 0.45));
            }
            add(items, x, z);
        } else {  // a toadstool
            double s = r.uniform(0.6, 1.0);
            add({box(x, 0.5 * s, z, 0.28 * s, 1.0 * s, 0.28 * s, {0.95, 0.93, 0.85}, 0.2, 0.0, EDGE),
                 box(x, 1.05 * s, z, 1.1 * s, 0.38 * s, 1.1 * s, {0.9, 0.12, 0.08}, 0.35, 0.3, EDGE),
                 box(x, 1.26 * s, z, 0.3 * s, 0.06, 0.3 * s, WHITE, 0.4, 0.0, 0.0)},
                x, z);
        }
    }
}

// the clearing plants that are not under a platform (recomputed only when the layout changes)
const std::vector<Box>& Forest::visible_cover() {
    std::vector<long long> key;
    for (auto& p : platforms_)
        for (double v : p) key.push_back(std::llround(v * 10));
    if (!plat_init_ || key != plat_key_) {
        plat_init_ = true;
        plat_key_ = key;
        inner_vis_.clear();
        for (size_t i = 0; i < inner_.size(); i++) {
            bool keep = true;
            for (auto& p : platforms_)
                if (std::fabs(inner_xz_[i][0] - p[0]) < p[2] && std::fabs(inner_xz_[i][1] - p[1]) < p[3]) {
                    keep = false;
                    break;
                }
            if (keep) inner_vis_.push_back(inner_[i]);
        }
    }
    return inner_vis_;
}

std::optional<int> Forest::pick(const V3& o, const V3& dir) {
    double best = 1e9;
    std::optional<int> bi;
    for (auto& d : dinos) {
        V3 c = d.flyer ? V3{d.x, d.y, d.z} : V3{d.x, d.y + d.pick_h * d.sc, d.z};
        V3 rv = o - c;
        double b = rv.dot(dir);
        double rr = d.pick_r * d.sc;
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

Beast& Forest::poke(int i, double t) {
    Beast& d = dinos[i];
    d.roar_t0 = t;
    d.look_goal = 0.0;
    d.look_t = t;
    rings.push_back({d.x, d.z, t, 0.0});
    if (d.kind == "frog") {  // a big startled leap away
        double a = d.heading + PI + rnd.uniform(-0.6, 0.6);
        start_hop(d, t, d.x + std::cos(a) * 13, d.z - std::sin(a) * 13, 5.5, 0.9);
    } else if (d.kind == "ladybug") {
        d.fly_until = t + 3.4;
        d.idle = 0.0;
    } else if (d.kind == "butterfly") {
        d.flee_until = t + 2.5;
        double tx = d.x + rnd.uniform(-8, 8);
        double ty = d.y + 9;
        double tz = d.z + rnd.uniform(-8, 8);
        d.target = std::array<double, 3>{tx, ty, tz};
    } else {  // beetles rear up and stand their ground
        d.idle = std::max(d.idle, 1.8);
        d.moving = false;
    }
    for (int k = 0; k < 8; k++) {  // a puff of leaves / pollen
        double vx = rnd.uniform(-3, 3), vy = rnd.uniform(1, 4), vz = rnd.uniform(-3, 3);
        dust.push_back({d.x, d.y + 1, d.z, vx, vy, vz, 0.9});
    }
    for (auto& o : dinos) {  // neighbours scatter
        if (&o != &d && (o.kind == "ladybug" || o.kind == "butterfly") && std::hypot(o.x - d.x, o.z - d.z) < 14) {
            o.flee_until = t + 1.5;
            if (o.kind == "butterfly") {
                double tx = o.x + rnd.uniform(-10, 10);
                double ty = o.y + 5;
                double tz = o.z + rnd.uniform(-10, 10);
                o.target = std::array<double, 3>{tx, ty, tz};
            }
        }
    }
    return d;
}

// drop berries: the nearest bug, frog or butterfly comes over to eat them
Beast* Forest::feed(double x, double z, double t) {
    auto bait = std::make_shared<Bait>(Bait{x, z, "berries", t});
    std::vector<Beast*> cands;
    for (auto& d : dinos)
        if (!d.eats.empty() && !d.bait && std::hypot(d.x - x, d.z - z) < 120) cands.push_back(&d);
    baits.push_back(bait);
    if (cands.empty()) return nullptr;
    Beast* d = *std::min_element(cands.begin(), cands.end(), [&](Beast* a, Beast* b) { return std::hypot(a->x - x, a->z - z) < std::hypot(b->x - x, b->z - z); });
    d->bait = bait;
    d->target = !d->flyer ? std::array<double, 3>{x, z, 0} : std::array<double, 3>{x, 1.4, z};
    d->idle = 0.0;
    d->flee_until = 0.0;
    return d;
}

bool Forest::blocked(double x, double z, const World& w, double r) const {
    if (blocked_basic(x, z, w, r)) return true;
    for (auto& tr : trees)
        if (std::hypot(x - tr[0], z - tr[1]) < tr[2] + r) return true;
    return false;
}

std::array<double, 3> Forest::pick_spot(const Beast& d, const World& w) {
    double rmin = w.bounds.first, rmax = w.bounds.second;
    for (int k = 0; k < 16; k++) {
        double x, z;
        if (d.kind == "frog") {  // frogs stay near the pond
            double a = rnd.uniform(0, TAU), rad = POND[2] + rnd.uniform(3, 20);
            x = POND[0] + std::sin(a) * rad;
            z = POND[1] + std::cos(a) * rad;
        } else {
            double a = rnd.uniform(0, TAU), rad = rnd.uniform(rmin, rmax);
            x = std::sin(a) * rad;
            z = std::cos(a) * rad;
        }
        if (!blocked(x, z, w, d.radius + 1)) return {x, z, 0};
    }
    return {d.x, d.z, 0};
}

void Forest::start_hop(Beast& d, double t, double tx, double tz, double h, double dur) {
    d.heading = std::atan2(-(tz - d.z), tx - d.x);
    d.hop = Beast::Hop{t, d.x, d.z, tx, tz, h, dur};
}

static bool has_bait(const std::vector<BaitP>& baits, const BaitP& b) { return std::find(baits.begin(), baits.end(), b) != baits.end(); }

void Forest::step_frog(Beast& d, double t, double dt, const World& w) {
    if (d.hop) {
        auto& hp = *d.hop;
        double u = (t - hp.t0) / hp.dur;
        if (u >= 1) {
            d.x = hp.x1;
            d.z = hp.z1;
            d.y = 0.0;
            d.hop.reset();
            d.idle = rnd.uniform(0.4, 2.6);
            d.air = 0.0;
            return;
        }
        d.x = hp.x0 + (hp.x1 - hp.x0) * u;
        d.z = hp.z0 + (hp.z1 - hp.z0) * u;
        d.y = 4 * hp.h * u * (1 - u);
        d.air = u < 0.75 ? std::min(1.0, u / 0.15) : std::max(0.0, 1 - (u - 0.75) / 0.25);  // legs extend at take-off, tuck to land
        return;
    }
    d.air = 0.0;
    if (d.bait && !has_bait(baits, d.bait)) d.bait.reset();
    if (d.eat_until > t) return;
    if (d.bait && std::hypot(d.x - d.bait->x, d.z - d.bait->z) < 2.5) {
        d.eat_until = t + 2.4;
        BaitP b = d.bait;
        baits.erase(std::remove(baits.begin(), baits.end(), b), baits.end());
        d.bait.reset();
        d.target.reset();
        return;
    }
    if (d.idle > 0) {
        d.idle -= dt;
        return;
    }
    if (!d.target || (!d.bait && blocked((*d.target)[0], (*d.target)[1], w, d.radius))) d.target = pick_spot(d, w);
    double dx = (*d.target)[0] - d.x, dz = (*d.target)[1] - d.z;
    double dist = std::hypot(dx, dz);
    if (dist < 1.5) {
        d.target.reset();
        d.idle = rnd.uniform(0.5, 3.0);
        return;
    }
    double stepl = std::min(dist, rnd.uniform(5.0, 7.5));
    double tx = d.x + dx / dist * stepl, tz = d.z + dz / dist * stepl;
    if (blocked(tx, tz, w, d.radius) && !d.bait) {
        d.target.reset();
        return;
    }
    double h = rnd.uniform(1.8, 2.6);
    double dur = rnd.uniform(0.5, 0.65);
    start_hop(d, t, tx, tz, h, dur);
}

void Forest::step_walker(Beast& d, double t, double dt, const World& w) {
    if (d.kind == "ladybug" && t < d.fly_until) {  // a short flight in a loop, then land
        double u = 1 - (d.fly_until - t) / 3.4;
        d.y = 3.5 * std::pow(std::sin(PI * std::min(u, 1.0)), 0.7);
        d.heading += dt * 1.6;
        d.x += std::cos(d.heading) * 4.5 * dt;
        d.z -= std::sin(d.heading) * 4.5 * dt;
        d.open = std::min({1.0, u / 0.12, (1 - u) / 0.12});
        d.mv *= 0.9;
        return;
    }
    d.y = 0.0;
    d.open = 0.0;
    d.look += (d.look_goal - d.look) * std::min(1.0, dt * 4);
    if (d.bait && !has_bait(baits, d.bait)) d.bait.reset();
    if (d.eat_until > t) {
        d.mv += (0 - d.mv) * std::min(1.0, dt * 4);
        return;
    }
    if (d.bait && std::hypot(d.x - d.bait->x, d.z - d.bait->z) < 2.0 + d.radius * 0.3) {
        d.eat_until = t + 3.0;
        BaitP b = d.bait;
        baits.erase(std::remove(baits.begin(), baits.end(), b), baits.end());
        d.bait.reset();
        d.target.reset();
        return;
    }
    if (d.idle > 0 && d.flee_until <= t) {
        d.idle -= dt;
        d.mv += (0 - d.mv) * std::min(1.0, dt * 4);
        if (rnd.random() < dt * 0.3) {
            d.look_goal = rnd.uniform(-0.5, 0.5);
            d.look_t = t;
        }
        return;
    }
    bool fleeing = d.flee_until > t;
    if (!d.target || (!d.bait && blocked((*d.target)[0], (*d.target)[1], w, d.radius))) d.target = pick_spot(d, w);
    if (fleeing) d.target = std::array<double, 3>{d.x + std::cos(d.heading) * 20, d.z - std::sin(d.heading) * 20, 0};
    double dx = (*d.target)[0] - d.x, dz = (*d.target)[1] - d.z;
    double dist = std::hypot(dx, dz);
    if (dist < 2.0 && !d.bait) {
        d.target.reset();
        d.idle = rnd.random() < 0.6 ? rnd.uniform(1.0, 4.0) : 0.0;
        return;
    }
    double want = std::atan2(-dz, dx);
    double diff = pmod(want - d.heading + PI, TAU) - PI;
    d.heading += std::max(-1.6 * dt, std::min(1.6 * dt, diff));
    double spd = d.speed * (fleeing ? 1.8 : (d.bait ? 1.3 : 1.0));
    if (std::fabs(diff) < 1.3) {
        d.x += std::cos(d.heading) * spd * dt;
        d.z -= std::sin(d.heading) * spd * dt;
        d.phase += spd * dt * 0.55;
        d.mv += (1 - d.mv) * std::min(1.0, dt * 5);
    } else {  // turning on the spot: the legs still shuffle
        d.phase += dt * 0.6;
        d.mv += (0.6 - d.mv) * std::min(1.0, dt * 5);
    }
    auto push_out = [&](double cx, double cz, double cr) {
        double ox = d.x - cx, oz = d.z - cz;
        double dd = std::hypot(ox, oz);
        if (dd == 0) dd = 1e-6;
        if (dd < cr + d.radius) {
            d.x = cx + ox / dd * (cr + d.radius);
            d.z = cz + oz / dd * (cr + d.radius);
            if (!d.bait) d.target.reset();
        }
    };
    for (auto& c : w.circles) push_out(c[0], c[1], c[2]);
    for (auto& c : trees) push_out(c[0], c[1], c[2]);
    for (auto& q : w.rects) {
        if (std::fabs(d.x - q[0]) < q[2] + d.radius && std::fabs(d.z - q[1]) < q[3] + d.radius) {
            if ((q[2] + d.radius - std::fabs(d.x - q[0])) < (q[3] + d.radius - std::fabs(d.z - q[1]))) d.x = q[0] + std::copysign(q[2] + d.radius, d.x - q[0]);
            else d.z = q[1] + std::copysign(q[3] + d.radius, d.z - q[1]);
            if (!d.bait) d.target.reset();
        }
    }
}

void Forest::step_butterfly(Beast& d, double t, double dt, const World& w) {
    if (d.bait && !has_bait(baits, d.bait)) d.bait.reset();
    if (d.eat_until > t) {  // sipping on the berries: hover low, drifting a little
        d.vx *= 0.9;
        d.vz *= 0.9;
        d.y += (1.3 + 0.15 * std::sin(t * 2) - d.y) * std::min(1.0, dt * 3);
        return;
    }
    if (d.bait) {
        double tx = d.bait->x, ty = 1.4, tz = d.bait->z;
        if (dist3({d.x, d.y, d.z}, {tx, ty, tz}) < 2.4) {
            d.eat_until = t + 4.0;
            BaitP b = d.bait;
            baits.erase(std::remove(baits.begin(), baits.end(), b), baits.end());
            d.bait.reset();
            d.target.reset();
            return;
        }
        d.target = std::array<double, 3>{tx, ty, tz};
    } else if (!d.target || dist3({d.x, d.y, d.z}, {(*d.target)[0], (*d.target)[1], (*d.target)[2]}) < 2.5 || rnd.random() < dt * 0.08) {
        double rmin = w.bounds.first, rmax = w.bounds.second;
        double a = rnd.uniform(0, TAU), rad = rnd.uniform(std::max(18.0, rmin - 20), rmax);
        double ty = rnd.uniform(2.5, 8.5);
        d.target = std::array<double, 3>{std::sin(a) * rad, ty, std::cos(a) * rad};
    }
    double tx = (*d.target)[0], ty = (*d.target)[1], tz = (*d.target)[2];
    double dx = tx - d.x, dy = ty - d.y, dz = tz - d.z;
    double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (dist == 0) dist = 1e-6;
    double spd = d.speed * (d.flee_until > t ? 2.2 : 1.0);
    double k = std::min(1.0, dt * 1.4);
    d.vx += (dx / dist * spd - d.vx) * k;
    d.vy += (dy / dist * spd * 0.6 - d.vy) * k;
    d.vz += (dz / dist * spd - d.vz) * k;
    d.x += d.vx * dt;
    d.z += d.vz * dt;
    d.y = std::max(1.0, d.y + d.vy * dt + 0.9 * std::sin(t * 3.1 + d.seed) * dt * 2);  // the bobbing, erratic butterfly flight
    if (std::fabs(d.vx) + std::fabs(d.vz) > 0.2) {
        double want = std::atan2(-d.vz, d.vx);
        double diff = pmod(want - d.heading + PI, TAU) - PI;
        d.heading += diff * std::min(1.0, dt * 5);
    }
}

void Forest::step(double t, const World& world) {
    if (world.has_platforms) platforms_ = world.platforms;
    else platforms_ = {{0.0, -70.0, 80.0, 95.0}};  // library / games: keep the clearing bare
    double dt = !has_last ? 0.0 : std::min(std::max(t - last, 0.0), 0.1);
    last = t;
    has_last = true;
    shake *= std::exp(-dt * 3.0);
    baits.erase(std::remove_if(baits.begin(), baits.end(), [&](const BaitP& b) { return !(t - b->t0 < 40); }), baits.end());
    rings.erase(std::remove_if(rings.begin(), rings.end(), [&](auto& r_) { return !(t - r_[2] < 1.2); }), rings.end());
    for (auto& r_ : rings) r_[3] = 2 + (t - r_[2]) * 14;
    for (auto& p : dust) {
        p[0] += p[3] * dt;
        p[1] = std::max(0.1, p[1] + p[4] * dt);
        p[2] += p[5] * dt;
        p[4] -= 4 * dt;
        p[6] -= dt;
    }
    dust.erase(std::remove_if(dust.begin(), dust.end(), [](auto& p) { return !(p[6] > 0); }), dust.end());
    for (auto& d : dinos) {
        if (d.kind == "frog") step_frog(d, t, dt, world);
        else if (d.kind == "butterfly") step_butterfly(d, t, dt, world);
        else step_walker(d, t, dt, world);
    }
}

// falling leaves tumbling down and pollen drifting in the light, recycled around the camera
std::vector<Box> Forest::ambient(double t, const V3& cam) {
    std::vector<Box> out;
    const C3 cols[4] = {{0.85, 0.55, 0.12}, {0.7, 0.35, 0.08}, {0.55, 0.65, 0.15}, {0.9, 0.75, 0.2}};
    for (int i = 0; i < 36; i++) {
        double fall = 1.2 + hrand(i, 1) * 1.0;
        double period = 14.0 / fall;
        double u = pmod(t + hrand(i, 2) * period, period) / period;
        double y = 14 * (1 - u);
        double ang = t * (0.6 + hrand(i, 3)) + i;
        double x = cam.x + (hrand(i, 4) - 0.5) * 120 + std::sin(ang) * 2.5;
        double z = cam.z + (hrand(i, 5) - 0.5) * 120 + std::cos(ang * 0.7) * 2.5;
        if (dist3({x, y, z}, cam) < 10) continue;
        out.push_back(box(x, y + 0.1, z, 0.55, 0.04, 0.38, cols[i % 4], 0.3, ang, 0.0, 0.0, 0.0, std::sin(ang * 1.7) * 0.9, std::cos(ang * 1.3) * 0.9));
    }
    for (int i = 0; i < 60; i++) {  // pollen motes catching the sun
        double x = cam.x + (hrand(i, 6) - 0.5) * 80 + std::sin(t * 0.3 + i) * 3;
        double z = cam.z + (hrand(i, 7) - 0.5) * 80 + std::cos(t * 0.25 + i * 1.3) * 3;
        double y = 1 + hrand(i, 8) * 7 + std::sin(t * 0.5 + i * 2) * 0.8;
        double tw = 0.5 + 0.5 * std::sin(t * 1.7 + i * 3);
        out.push_back(box(x, y, z, 0.12, 0.12, 0.12, {1.0, 0.95, 0.6}, 0.4 + 1.2 * tw, 0.0, 0.0));
    }
    return out;
}

std::vector<Box> Forest::boxes(double t, const V3& cam, Renderer* r) {
    if (r) {
        r->add_boxes(static_);  // the forest itself: one pre-built block
        r->add_boxes(visible_cover());
    }
    std::vector<Box> out = ambient(t, cam);
    for (auto& d : dinos) {
        if (std::hypot(d.x - cam.x, d.z - cam.z) > 230) continue;
        double roar_u = (t - d.roar_t0) / 1.6;
        double roar = (0 <= roar_u && roar_u <= 1) ? std::pow(std::sin(PI * roar_u), 0.6) : 0.0;
        double throat = (d.kind == "frog" && !d.hop && ((long long)std::floor(t * 0.4 + d.seed)) % 3 == 0) ? 0.5 + 0.5 * std::sin(t * 5 + d.seed) : 0.0;
        St st{d.phase, d.mv, roar, d.look, d.air, throat, d.open, d.eat_until > t};
        auto parts = rig_of(d.kind)(d, t, st);
        place(out, parts, d.x, d.y, d.z, d.heading, d.sc, EDGE);
        if (r && !d.flyer) {  // soft contact shadow (smaller and fainter while airborne)
            double k = 1.0 / (1.0 + d.y * 0.6);
            double ln = 2.6 * d.sc * (d.kind == "frog" ? 1.2 : 1.0);
            shadow(r, d.x, d.z, d.heading, ln, ln / 1.5, 0.32 * k);
        }
    }
    for (auto& b : baits) {  // berries: a little heap of red and blue
        for (int k = 0; k < 7; k++) {
            double a = k * 0.9 + b->x;
            double rr = k ? 0.35 : 0.0;
            C3 col = k % 2 ? C3(0.85, 0.1, 0.15) : C3(0.25, 0.2, 0.75);
            out.push_back(box(b->x + std::cos(a) * rr, 0.25 + (k == 0 ? 0.25 : 0), b->z + std::sin(a) * rr, 0.38, 0.38, 0.38, col, 0.9, a));
        }
        out.push_back(box(b->x + 0.2, 0.6, b->z, 0.5, 0.05, 0.25, {0.25, 0.55, 0.15}, 0.2, 0.4));
    }
    for (auto& p : dust) out.push_back(box(p[0], p[1], p[2], 0.3, 0.05, 0.22, {0.75, 0.6, 0.2}, 0.6, p[6] * 9, 0.0, 0.0, 0.0, p[6] * 7, p[6] * 5));
    for (auto& rg : rings) {  // ripples on the ground where something was poked
        double fade = 1 - (t - rg[2]) / 1.2;
        for (int k = 0; k < 18; k++) {
            double a = k * TAU / 18;
            out.push_back(box(rg[0] + std::cos(a) * rg[3], 0.1, rg[1] + std::sin(a) * rg[3], 1.0 * fade + 0.2, 0.08, 0.35, {0.8, 1.0, 0.5}, 1.0 * fade, -a, 0.0));
        }
    }
    return out;
}
