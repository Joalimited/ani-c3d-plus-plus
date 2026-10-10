// Tiny 4x4 matrix helpers (port of mathx.py). Matrices are column-vector style (v' = M * v), stored row-major like
// numpy; gpu() hands them to OpenGL column-major. Depth is OpenGL's -1..1 clip range.
#pragma once
#include <array>
#include <cmath>

constexpr double PI = 3.14159265358979323846;
constexpr double TAU = 2 * PI;

// Python's float % (result has the sign of the divisor)
inline double pmod(double a, double b) { return a - b * std::floor(a / b); }
inline double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
// Python's math.copysign
inline double copysign_(double mag, double sgn) { return std::copysign(mag, sgn); }

struct V3 {
    double x = 0, y = 0, z = 0;
    V3() = default;
    V3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
    V3 operator+(const V3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    V3 operator-(const V3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    V3 operator*(double s) const { return {x * s, y * s, z * s}; }
    V3 operator/(double s) const { return {x / s, y / s, z / s}; }
    V3& operator+=(const V3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    double operator[](int i) const { return i == 0 ? x : i == 1 ? y : z; }
    double dot(const V3& o) const { return x * o.x + y * o.y + z * o.z; }
    V3 cross(const V3& o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
    double norm() const { return std::sqrt(x * x + y * y + z * z); }
};
inline double dist3(const V3& a, const V3& b) { return (a - b).norm(); }

struct M4 {
    double m[16];  // row-major: m[r*4+c]
    static M4 identity() {
        M4 r{};
        for (int i = 0; i < 16; i++) r.m[i] = (i % 5 == 0) ? 1.0 : 0.0;
        return r;
    }
    double& at(int r, int c) { return m[r * 4 + c]; }
    double at(int r, int c) const { return m[r * 4 + c]; }
    M4 operator*(const M4& o) const {
        M4 r{};
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++) {
                double s = 0;
                for (int k = 0; k < 4; k++) s += at(i, k) * o.at(k, j);
                r.at(i, j) = s;
            }
        return r;
    }
    std::array<double, 4> mul(double x, double y, double z, double w) const {
        std::array<double, 4> o{};
        for (int i = 0; i < 4; i++) o[i] = at(i, 0) * x + at(i, 1) * y + at(i, 2) * z + at(i, 3) * w;
        return o;
    }
    // column-major float copy for OpenGL
    void gpu(float* out) const {
        for (int c = 0; c < 4; c++)
            for (int r = 0; r < 4; r++) out[c * 4 + r] = (float)at(r, c);
    }
    M4 inverse() const;
};

inline M4 M4::inverse() const {
    const double* a = m;
    double inv[16];
    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
    double det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    M4 r{};
    if (std::fabs(det) < 1e-300) return M4::identity();
    for (int i = 0; i < 16; i++) r.m[i] = inv[i] / det;
    return r;
}

namespace mx {
inline M4 perspective(double fovy, double aspect, double near_, double far_) {
    double f = 1.0 / std::tan(fovy / 2);
    M4 r{};
    r.at(0, 0) = f / aspect;
    r.at(1, 1) = f;
    r.at(2, 2) = (far_ + near_) / (near_ - far_);  // OpenGL depth range -1..1
    r.at(2, 3) = 2 * near_ * far_ / (near_ - far_);
    r.at(3, 2) = -1;
    return r;
}
inline M4 look_at(V3 eye, V3 target, V3 up = {0, 1, 0}) {
    V3 z = eye - target;
    z = z / z.norm();
    V3 x = up.cross(z);
    x = x / x.norm();
    V3 y = z.cross(x);
    M4 m = M4::identity();
    m.at(0, 0) = x.x; m.at(0, 1) = x.y; m.at(0, 2) = x.z;
    m.at(1, 0) = y.x; m.at(1, 1) = y.y; m.at(1, 2) = y.z;
    m.at(2, 0) = z.x; m.at(2, 1) = z.y; m.at(2, 2) = z.z;
    m.at(0, 3) = -x.dot(eye);
    m.at(1, 3) = -y.dot(eye);
    m.at(2, 3) = -z.dot(eye);
    return m;
}
inline M4 translate(double x, double y, double z) {
    M4 m = M4::identity();
    m.at(0, 3) = x; m.at(1, 3) = y; m.at(2, 3) = z;
    return m;
}
inline M4 scale(double x, double y, double z) {
    M4 m{};
    m.at(0, 0) = x; m.at(1, 1) = y; m.at(2, 2) = z; m.at(3, 3) = 1;
    return m;
}
inline M4 rot_x(double a) {
    double c = std::cos(a), s = std::sin(a);
    M4 m = M4::identity();
    m.at(1, 1) = c; m.at(1, 2) = -s; m.at(2, 1) = s; m.at(2, 2) = c;
    return m;
}
inline M4 rot_y(double a) {
    double c = std::cos(a), s = std::sin(a);
    M4 m = M4::identity();
    m.at(0, 0) = c; m.at(0, 2) = s; m.at(2, 0) = -s; m.at(2, 2) = c;
    return m;
}
inline M4 rot_z(double a) {
    double c = std::cos(a), s = std::sin(a);
    M4 m = M4::identity();
    m.at(0, 0) = c; m.at(0, 1) = -s; m.at(1, 0) = s; m.at(1, 1) = c;
    return m;
}
// pixel coordinates, origin top-left, y down
inline M4 ortho_px(double w, double h) {
    M4 m{};
    m.at(0, 0) = 2 / w; m.at(0, 3) = -1;
    m.at(1, 1) = -2 / h; m.at(1, 3) = 1;
    m.at(3, 3) = 1;
    return m;
}
inline V3 unproject(const M4& inv_viewproj, double nx, double ny, double nz) {
    auto p = inv_viewproj.mul(nx, ny, nz, 1.0);
    return {p[0] / p[3], p[1] / p[3], p[2] / p[3]};
}
}  // namespace mx

// ---------------------------------------------------------------- colours and box instances
struct C3 {
    float r = 0, g = 0, b = 0;
    C3() = default;
    C3(double r_, double g_, double b_) : r((float)r_), g((float)g_), b((float)b_) {}
    C3 operator*(double s) const { return {r * s, g * s, b * s}; }
    C3 operator+(const C3& o) const { return {r + o.r, g + o.g, b + o.b}; }
    float operator[](int i) const { return i == 0 ? r : i == 1 ? g : b; }
};
struct C4 {
    float r = 1, g = 1, b = 1, a = 1;
    C4() = default;
    C4(double r_, double g_, double b_, double a_ = 1.0) : r((float)r_), g((float)g_), b((float)b_), a((float)a_) {}
    C4(const C3& c, double a_ = 1.0) : r(c.r), g(c.g), b(c.b), a((float)a_) {}
};
struct UV {
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
};

// One instance: pos(3) glow(1) scale(3) yaw(1) color(3) edge(1) pitch(1) flat(1) rollz(1) rollx(1) - 16 floats.
struct Box {
    float v[16];
};
// `edge` scales the neon outline (0 = plain surface); `pitch` tilts about X; `flat` skips retro posterizing;
// `rollz`/`rollx` rotate the box in its own frame (limbs, jaws, wings).
inline Box box(double x, double y, double z, double sx, double sy, double sz, const C3& c, double glow = 0.0, double yaw = 0.0,
               double edge = 1.0, double pitch = 0.0, double flat = 0.0, double rollz = 0.0, double rollx = 0.0) {
    return Box{{(float)x, (float)y, (float)z, (float)glow, (float)sx, (float)sy, (float)sz, (float)yaw, c.r, c.g, c.b, (float)edge,
                (float)pitch, (float)flat, (float)rollz, (float)rollx}};
}

// A rig part in a creature's local frame: (x, y, z, sx, sy, sz, colour, glow, rollz, rollx, local yaw)
struct Part {
    double x, y, z, sx, sy, sz;
    C3 col;
    double glow = 0.25, rz = 0, rx = 0, yl = 0;
};

inline double ease(double x) {
    x = clampd(x, 0.0, 1.0);
    return 1 - (1 - x) * (1 - x) * (1 - x);
}
inline double smooth01(double x) {
    x = clampd(x, 0.0, 1.0);
    return x * x * (3 - 2 * x);
}
// deterministic pseudo-random in [0, 1)
inline double hrand(double i, double k = 0) { return pmod(std::sin(i * 12.9898 + k * 78.233) * 43758.5453, 1.0); }
