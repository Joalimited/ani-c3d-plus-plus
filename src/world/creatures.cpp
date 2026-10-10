#include "creatures.hpp"

#include <cmath>

#include "../gfx/renderer.hpp"

Part limb(V3 p0, V3 p1, double th, C3 col, double glow) {
    // the shader applies rollz, then rollx, so dir = (-sin rz, cos rz cos rx, cos rz sin rx)
    double dx = p1.x - p0.x, dy = p1.y - p0.y, dz = p1.z - p0.z;
    double ln = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (ln == 0) ln = 1e-6;
    double rz = -std::asin(clampd(dx / ln, -1.0, 1.0));
    double rx = std::atan2(dz / ln, dy / ln);
    return Part{(p0.x + p1.x) / 2, (p0.y + p1.y) / 2, (p0.z + p1.z) / 2, th, ln + th * 0.6, th, col, glow, rz, rx, 0.0};
}

void shadow(Renderer* r, double x, double z, double heading, double ln, double wd, double alpha, double off_l, double off_w) {
    if (!r) return;
    M4 m = mx::translate(x, 0.03, z) * mx::rot_y(heading) * mx::rot_x(PI / 2) * mx::translate(-ln * off_l, -wd * off_w, 0) * mx::scale(ln, wd, 1);
    r->quad3d(r->white, m, C4(0, 0, 0, alpha));
}

void place(std::vector<Box>& out, const std::vector<Part>& parts, double x, double y0, double z, double heading, double sc, double edge) {
    double c = std::cos(heading), s = std::sin(heading);
    for (auto& p : parts)
        out.push_back(box(x + (p.x * c + p.z * s) * sc, y0 + p.y * sc, z + (-p.x * s + p.z * c) * sc, p.sx * sc, p.sy * sc, p.sz * sc, p.col, p.glow,
                          heading + p.yl, edge, 0.0, 0.0, p.rz, p.rx));
}

bool blocked_basic(double x, double z, const World& w, double r) {
    for (auto& c : w.circles)
        if (std::hypot(x - c[0], z - c[1]) < c[2] + r) return true;
    for (auto& q : w.rects)
        if (std::fabs(x - q[0]) < q[2] + r && std::fabs(z - q[1]) < q[3] + r) return true;
    return false;
}
