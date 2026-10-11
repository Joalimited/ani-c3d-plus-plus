#include "props.hpp"

#include <cmath>

namespace props {
static Part pt(double x, double y, double z, double sx, double sy, double sz, C3 c, double glow = 0.15, double rz = 0, double rx = 0, double yl = 0) {
    return Part{x, y, z, sx, sy, sz, c, glow, rz, rx, yl};
}
static void scale_all(std::vector<Part>& P, double k) {
    for (auto& p : P) {
        p.x *= k;
        p.y *= k;
        p.z *= k;
        p.sx *= k;
        p.sy *= k;
        p.sz *= k;
    }
}

double crt_heading(double t) {
    double u = std::fmod(t, 28.0) / 28.0;
    double spin = u > 0.94 ? (u - 0.94) / 0.06 * 2 * PI : 0.0;
    return -PI / 2 + 0.3 * std::sin(t * 0.35) + spin;  // local +x is the screen: -90 degrees points it at the camera
}

bool crt_theme(const std::string& fx) { return fx.empty() || fx == "matrix" || fx == "retro" || fx == "vcr" || fx == "mce"; }

void tilt(std::vector<Part>& parts, double az, double ax) {
    double cz = std::cos(az), sz = std::sin(az), cx = std::cos(ax), sx = std::sin(ax);
    for (auto& p : parts) {
        double x = p.x * cz - p.y * sz, y = p.x * sz + p.y * cz, z = p.z;  // about z
        double y2 = y * cx - z * sx, z2 = y * sx + z * cx;                  // then about x
        p.x = x;
        p.y = y2;
        p.z = z2;
        p.rz += az;
        p.rx += ax;
    }
}

// a bulky CRT: tapered beige case, dark bezel, glowing screen, two knobs, rabbit-ear antenna, little feet
std::vector<Part> crt_tv(double t, C3 screen, double k) {
    const C3 CASE{0.78, 0.74, 0.62}, DARK{0.06, 0.06, 0.07}, KNOB{0.25, 0.25, 0.28};
    double flick = 0.9 + 0.35 * std::sin(t * 23) * std::sin(t * 7.3) + 0.25 * std::sin(t * 3.1);
    std::vector<Part> P = {
        pt(0.0, 1.45, 0.0, 2.2, 2.2, 2.8, CASE, 0.12),            // main case
        pt(-1.35, 1.45, 0.0, 1.5, 1.6, 2.0, CASE, 0.1),           // the tube's tapered back
        pt(1.12, 1.5, -0.15, 0.1, 1.85, 2.25, DARK, 0.0),         // bezel
        pt(1.19, 1.5, -0.15, 0.06, 1.5, 1.9, screen, flick),      // screen
        pt(1.14, 2.15, 1.18, 0.06, 0.26, 0.26, KNOB, 0.1),        // knobs on the right edge
        pt(1.14, 1.55, 1.18, 0.06, 0.26, 0.26, KNOB, 0.1),
        pt(1.14, 0.85, 1.18, 0.06, 0.08, 0.5, DARK, 0.0),         // speaker grille
        pt(1.14, 0.7, 1.18, 0.06, 0.08, 0.5, DARK, 0.0),
        pt(0.5, 0.18, 1.0, 0.7, 0.36, 0.5, DARK, 0.0),            // feet
        pt(0.5, 0.18, -1.0, 0.7, 0.36, 0.5, DARK, 0.0),
        pt(-0.6, 0.18, 1.0, 0.7, 0.36, 0.5, DARK, 0.0),
        pt(-0.6, 0.18, -1.0, 0.7, 0.36, 0.5, DARK, 0.0),
        pt(-0.3, 3.0, 0.45, 0.07, 1.5, 0.07, KNOB, 0.3, -0.55),   // rabbit ears
        pt(-0.3, 3.0, -0.45, 0.07, 1.5, 0.07, KNOB, 0.3, 0.55),
        pt(-0.05, 2.62, 0.0, 0.5, 0.18, 0.8, DARK, 0.0),          // their base
    };
    scale_all(P, k);
    return P;
}

// a VHS tape: black shell, off-white label with a coloured stripe, two reel windows and the dark flap along the front edge
std::vector<Part> vhs(C3 label, double k) {
    const C3 SHELL{0.07, 0.07, 0.09};
    std::vector<Part> P = {
        pt(0, 0, 0, 3.76, 0.5, 2.04, SHELL, 0.12),
        pt(0.0, 0.0, 0.98, 3.2, 0.44, 0.14, {0.12, 0.12, 0.15}, 0.05),          // the flap
        pt(0.1, 0.27, -0.2, 2.5, 0.04, 1.25, {0.92, 0.9, 0.8}, 0.5),             // label
        pt(0.1, 0.3, -0.62, 2.5, 0.04, 0.3, label, 1.0),                        // its coloured stripe
        pt(-0.95, 0.27, 0.5, 0.62, 0.04, 0.62, {0.02, 0.02, 0.03}, 0.0),        // reel windows
        pt(0.95, 0.27, 0.5, 0.62, 0.04, 0.62, {0.02, 0.02, 0.03}, 0.0),
        pt(-0.95, 0.3, 0.5, 0.3, 0.04, 0.3, {0.8, 0.8, 0.85}, 0.7),            // hubs
        pt(0.95, 0.3, 0.5, 0.3, 0.04, 0.3, {0.8, 0.8, 0.85}, 0.7),
        pt(0.0, 0.0, -1.03, 3.4, 0.4, 0.04, {0.92, 0.9, 0.8}, 0.4),            // spine label
    };
    scale_all(P, k);
    return P;
}

// a compact audio cassette: clear shell, a label, two reels that turn, the tape window and the bottom trapezoid
std::vector<Part> cassette(C3 shell, C3 label, double t, double k) {
    double a = t * 3.0;
    std::vector<Part> P = {
        pt(0, 0, 0, 3.0, 0.34, 1.9, shell, 0.2),
        pt(0.0, 0.19, -0.3, 2.5, 0.03, 0.95, label, 0.7),                       // label
        pt(0.0, 0.2, -0.05, 2.5, 0.03, 0.18, {0.1, 0.1, 0.1}, 0.0),            // a pencil line
        pt(0.0, 0.19, 0.42, 1.75, 0.03, 0.62, {0.02, 0.02, 0.04}, 0.0),        // tape window
        pt(-0.5, 0.2, 0.42, 0.42, 0.04, 0.42, {0.9, 0.9, 0.92}, 0.6, 0, 0, a),  // reels
        pt(0.5, 0.2, 0.42, 0.42, 0.04, 0.42, {0.9, 0.9, 0.92}, 0.6, 0, 0, -a),
        pt(0.0, -0.2, 0.7, 1.7, 0.08, 0.5, {0.25, 0.25, 0.28}, 0.1),           // bottom edge
    };
    scale_all(P, k);
    return P;
}

// a Sony Walkman (TPS-L2 style): blue and silver body, cassette window, row of buttons on top, orange foam headphones
std::vector<Part> walkman(double t, double k) {
    double a = t * 2.4;
    const C3 BLUE{0.12, 0.35, 0.85}, SILVER{0.78, 0.8, 0.84}, ORANGE{1.0, 0.5, 0.05};
    std::vector<Part> P = {
        pt(0, 1.1, 0, 0.9, 2.2, 3.3, BLUE, 0.2),                                // body
        pt(0.48, 1.1, 0, 0.06, 2.0, 3.1, SILVER, 0.2),                          // silver front plate
        pt(0.53, 1.35, 0.0, 0.05, 0.9, 2.1, {0.03, 0.03, 0.06}, 0.0),          // cassette window
        pt(0.56, 1.35, -0.5, 0.05, 0.4, 0.4, {0.9, 0.9, 0.95}, 0.6, 0, 0, a),  // reels
        pt(0.56, 1.35, 0.5, 0.05, 0.4, 0.4, {0.9, 0.9, 0.95}, 0.6, 0, 0, -a),
        pt(0.53, 0.55, 0.0, 0.05, 0.3, 2.2, {0.15, 0.15, 0.2}, 0.0),           // speaker slot
    };
    for (int i = 0; i < 5; i++) P.push_back(pt(0.0, 2.3, -1.2 + i * 0.6, 0.6, 0.18, 0.4, i == 0 ? C3(1.0, 0.2, 0.2) : C3(0.15, 0.15, 0.2), i == 0 ? 0.8 : 0.1));
    // headphones: two foam pads and a band arching over the top
    P.push_back(pt(0.0, 3.1, -1.9, 0.5, 1.0, 1.0, ORANGE, 0.5));
    P.push_back(pt(0.0, 3.1, 1.9, 0.5, 1.0, 1.0, ORANGE, 0.5));
    for (int i = 0; i <= 6; i++) {
        double u = i / 6.0, ang = PI * u;
        P.push_back(pt(0.0, 3.5 + std::sin(ang) * 1.1, -1.9 + 3.8 * u, 0.14, 0.14, 0.7, {0.1, 0.1, 0.12}, 0.1, 0, 0, 0));
    }
    scale_all(P, k);
    return P;
}

// the original Macintosh: tall beige box, recessed 9-inch screen, floppy slot, rainbow-dot badge, carrying handle
std::vector<Part> macintosh(double t, double k) {
    const C3 BEIGE{0.82, 0.78, 0.66};
    double glow = 0.9 + 0.2 * std::sin(t * 2.0);
    std::vector<Part> P = {
        pt(0.0, 1.5, 0.0, 2.3, 3.0, 2.3, BEIGE, 0.12),                          // body
        pt(-0.55, 2.2, 0.0, 1.4, 1.6, 1.8, BEIGE, 0.1),                         // the tube's bulge on the back
        pt(1.16, 2.25, 0.0, 0.06, 1.55, 1.6, {0.15, 0.15, 0.15}, 0.0),          // bezel
        pt(1.2, 2.25, 0.0, 0.05, 1.2, 1.3, {0.82, 0.95, 0.85}, glow),           // the 9" screen
        pt(1.2, 2.3, -0.25, 0.04, 0.2, 0.15, {0.1, 0.1, 0.1}, 0.0),            // a smiling face
        pt(1.2, 2.3, 0.25, 0.04, 0.2, 0.15, {0.1, 0.1, 0.1}, 0.0),
        pt(1.2, 1.95, 0.0, 0.04, 0.1, 0.55, {0.1, 0.1, 0.1}, 0.0),
        pt(1.16, 1.0, 0.3, 0.04, 0.09, 0.8, {0.12, 0.12, 0.12}, 0.0),           // floppy slot
        pt(1.16, 0.55, -0.7, 0.04, 0.18, 0.18, {0.3, 0.8, 0.3}, 0.9),          // rainbow badge dots
        pt(1.16, 0.55, -0.48, 0.04, 0.18, 0.18, {1.0, 0.8, 0.1}, 0.9),
        pt(1.16, 0.55, -0.26, 0.04, 0.18, 0.18, {1.0, 0.4, 0.1}, 0.9),
        pt(0.0, 3.1, 0.0, 0.7, 0.14, 1.0, BEIGE, 0.1),                          // handle recess
        pt(0.0, 0.1, 0.0, 2.5, 0.2, 2.5, {0.7, 0.66, 0.55}, 0.05),              // base plate
    };
    scale_all(P, k);
    return P;
}

// the Windows Live-style butterfly: dark body, blue upper wings, green / orange lower wings, flapping about the body
std::vector<Part> butterfly(double t, double phase, C3 upper, C3 lower, double k) {
    double th = 0.15 + 0.85 * (0.5 + 0.5 * std::sin(t * 9.0 + phase));  // 0 = flat, 1 = raised
    double ang = th * 1.1;
    std::vector<Part> P;
    P.push_back(pt(0, 0, 0, 1.3, 0.22, 0.22, {0.05, 0.05, 0.1}, 0.3));          // body
    P.push_back(pt(0.75, 0.05, 0, 0.3, 0.3, 0.3, {0.05, 0.05, 0.1}, 0.3));      // head
    P.push_back(pt(1.0, 0.35, 0.12, 0.6, 0.04, 0.04, {0.05, 0.05, 0.1}, 0.2, 0.5));  // antennae
    P.push_back(pt(1.0, 0.35, -0.12, 0.6, 0.04, 0.04, {0.05, 0.05, 0.1}, 0.2, 0.5));
    for (int s : {-1, 1}) {
        double phi = -s * ang;  // both wings rise
        double c = std::cos(phi), sn = std::sin(phi);
        auto wing = [&](double px, double wz, double sx, double sz, C3 col, double glow) {
            double zz = s * wz;  // distance of the wing's centre from the body line
            // rotate the centre about the body axis (x) by phi
            P.push_back(pt(px, -sn * zz * 0 + (0 * c - zz * sn), zz * c, sx, 0.05, sz, col, glow, 0, phi));
        };
        wing(0.25, 0.8, 1.3, 1.4, upper, 0.9);   // upper wings
        wing(-0.45, 0.65, 0.9, 1.0, lower, 0.8); // lower wings
    }
    scale_all(P, k);
    return P;
}
}  // namespace props
