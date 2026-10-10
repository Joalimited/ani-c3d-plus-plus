#include "canvas.hpp"

#include <algorithm>
#include <cmath>

#include "../util/strutil.hpp"
#include "stb_truetype.h"
#include "text.hpp"

Canvas::Canvas(int w, int h, RGBA fill) {
    img.w = w;
    img.h = h;
    img.rgba.resize((size_t)w * h * 4);
    for (size_t i = 0; i < (size_t)w * h; i++) {
        img.rgba[i * 4 + 0] = fill.r;
        img.rgba[i * 4 + 1] = fill.g;
        img.rgba[i * 4 + 2] = fill.b;
        img.rgba[i * 4 + 3] = fill.a;
    }
}

// source-over blending of colour c with the given coverage
void Canvas::blend(int x, int y, RGBA c, float coverage) {
    if (x < 0 || y < 0 || x >= img.w || y >= img.h || coverage <= 0) return;
    coverage = std::min(coverage, 1.f);
    uint8_t* p = &img.rgba[((size_t)y * img.w + x) * 4];
    float sa = c.a / 255.f * coverage;
    float da = p[3] / 255.f;
    float oa = sa + da * (1 - sa);
    if (oa <= 0) return;
    for (int k = 0; k < 3; k++) {
        float sc = (k == 0 ? c.r : k == 1 ? c.g : c.b), dc = p[k];
        p[k] = (uint8_t)std::lround((sc * sa + dc * da * (1 - sa)) / oa);
    }
    p[3] = (uint8_t)std::lround(oa * 255);
}

template <class F>
void Canvas::shade(double x0, double y0, double x1, double y1, F coverage, RGBA c) {
    int ix0 = std::max(0, (int)std::floor(x0) - 1), iy0 = std::max(0, (int)std::floor(y0) - 1);
    int ix1 = std::min(img.w - 1, (int)std::ceil(x1) + 1), iy1 = std::min(img.h - 1, (int)std::ceil(y1) + 1);
    for (int y = iy0; y <= iy1; y++)
        for (int x = ix0; x <= ix1; x++) {
            float cov = (float)coverage(x + 0.5, y + 0.5);
            if (cov > 0) blend(x, y, c, cov);
        }
}

static double sd_round_box(double px, double py, double cx, double cy, double hx, double hy, double r) {
    double qx = std::fabs(px - cx) - (hx - r), qy = std::fabs(py - cy) - (hy - r);
    double ox = std::max(qx, 0.0), oy = std::max(qy, 0.0);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0) - r;
}

void Canvas::rounded_rect(double x0, double y0, double x1, double y1, double radius, const RGBA* fill, const RGBA* outline, double width) {
    double cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, hx = (x1 - x0) / 2, hy = (y1 - y0) / 2;
    radius = std::min(radius, std::min(hx, hy));
    if (fill) shade(x0, y0, x1, y1, [&](double px, double py) { return std::clamp(0.5 - sd_round_box(px, py, cx, cy, hx, hy, radius), 0.0, 1.0); }, *fill);
    if (outline && width > 0) {  // like Pillow, the outline lies inside the rectangle
        shade(x0, y0, x1, y1,
              [&](double px, double py) {
                  double d = sd_round_box(px, py, cx, cy, hx, hy, radius);
                  return std::clamp(width / 2 + 0.5 - std::fabs(d + width / 2), 0.0, 1.0);
              },
              *outline);
    }
}

void Canvas::line(const std::vector<std::pair<double, double>>& pts, RGBA color, double width) {
    double hw = std::max(width, 1.0) / 2;
    for (size_t i = 0; i + 1 < pts.size(); i++) {
        auto [ax, ay] = pts[i];
        auto [bx, by] = pts[i + 1];
        double dx = bx - ax, dy = by - ay, L2 = dx * dx + dy * dy;
        shade(std::min(ax, bx) - hw, std::min(ay, by) - hw, std::max(ax, bx) + hw, std::max(ay, by) + hw,
              [&](double px, double py) {
                  double t = L2 > 0 ? std::clamp(((px - ax) * dx + (py - ay) * dy) / L2, 0.0, 1.0) : 0.0;
                  double qx = ax + dx * t - px, qy = ay + dy * t - py;
                  return std::clamp(hw + 0.5 - std::sqrt(qx * qx + qy * qy), 0.0, 1.0);
              },
              color);
    }
}

void Canvas::ellipse(double x0, double y0, double x1, double y1, const RGBA* fill, const RGBA* outline, double width) {
    double cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, rx = (x1 - x0) / 2, ry = (y1 - y0) / 2;
    auto sd = [&](double px, double py) {  // approximate signed distance to an ellipse (exact for circles)
        double nx = (px - cx) / rx, ny = (py - cy) / ry;
        double k = std::sqrt(nx * nx + ny * ny);
        return (k - 1) * std::min(rx, ry);
    };
    if (fill) shade(x0, y0, x1, y1, [&](double px, double py) { return std::clamp(0.5 - sd(px, py), 0.0, 1.0); }, *fill);
    if (outline && width > 0)
        shade(x0, y0, x1, y1, [&](double px, double py) { return std::clamp(width / 2 + 0.5 - std::fabs(sd(px, py) + width / 2), 0.0, 1.0); }, *outline);
}

void Canvas::polygon(const std::vector<std::pair<double, double>>& pts, RGBA fill) {
    if (pts.size() < 3) return;
    double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
    for (auto& [x, y] : pts) {
        x0 = std::min(x0, x);
        y0 = std::min(y0, y);
        x1 = std::max(x1, x);
        y1 = std::max(y1, y);
    }
    auto inside = [&](double px, double py) {
        bool in = false;
        for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
            auto [xi, yi] = pts[i];
            auto [xj, yj] = pts[j];
            if (((yi > py) != (yj > py)) && (px < (xj - xi) * (py - yi) / (yj - yi) + xi)) in = !in;
        }
        return in;
    };
    shade(x0, y0, x1, y1,
          [&](double px, double py) {  // 4x4 supersampling
              int n = 0;
              for (int sy = 0; sy < 4; sy++)
                  for (int sx = 0; sx < 4; sx++) n += inside(px - 0.5 + (sx + 0.5) / 4, py - 0.5 + (sy + 0.5) / 4);
              return n / 16.0;
          },
          fill);
}

double Canvas::textlength(const std::string& s, double px) { return Font::get().text_length(s, px); }

std::vector<std::string> Canvas::wrap(const std::string& text, double px, double width, size_t max_lines) const {
    std::vector<std::string> lines;
    std::string cur;
    for (auto& w : su::split_ws(text)) {
        std::string t = su::strip(cur + " " + w);
        if (textlength(t, px) > width && !cur.empty()) {
            lines.push_back(cur);
            cur = w;
        } else {
            cur = t;
        }
    }
    lines.push_back(cur);
    if (lines.size() > max_lines) lines.resize(max_lines);
    return lines;
}

void Canvas::text(double x, double y, const std::string& s, double px, RGBA color, const std::string& anchor, bool center_lines) {
    Font& f = Font::get();
    auto* info = (const stbtt_fontinfo*)f.info();
    if (!info) return;
    float scale = stbtt_ScaleForMappingEmToPixels(info, (float)px);
    int asc, desc, gap;
    stbtt_GetFontVMetrics(info, &asc, &desc, &gap);
    double ascent = asc * scale, descent = desc * scale;  // descent is negative
    auto lines = su::split(s, "\n");
    double line_h = ascent + 4;  // Pillow's multiline spacing (bbox of "A" + 4)
    double block_h = line_h * (lines.size() - 1);
    char ha = anchor.size() > 0 ? anchor[0] : 'l', va = anchor.size() > 1 ? anchor[1] : 'a';
    double block_w = 0;
    for (auto& ln : lines) block_w = std::max(block_w, f.text_length(ln, px));
    for (size_t li = 0; li < lines.size(); li++) {
        const std::string& ln = lines[li];
        double lw = f.text_length(ln, px);
        double pen = ha == 'm' ? x - lw / 2 : x;  // "mm" centres every line on x (Pillow's align=center)
        (void)center_lines;
        (void)block_w;
        double base;
        if (va == 'm') base = y - block_h / 2 + (ascent + descent) / 2 + li * line_h;
        else base = y + ascent + li * line_h;
        auto cps = su::utf8_decode(ln);
        for (size_t i = 0; i < cps.size(); i++) {
            int cp = (int)cps[i];
            if (!stbtt_FindGlyphIndex(info, cp)) cp = '?';
            int adv, lsb;
            stbtt_GetCodepointHMetrics(info, cp, &adv, &lsb);
            int x0, y0, x1, y1;
            float fx = (float)(pen - std::floor(pen));
            stbtt_GetCodepointBitmapBoxSubpixel(info, cp, scale, scale, fx, 0, &x0, &y0, &x1, &y1);
            int gw = x1 - x0, gh = y1 - y0;
            if (gw > 0 && gh > 0) {
                std::vector<unsigned char> bmp((size_t)gw * gh);
                stbtt_MakeCodepointBitmapSubpixel(info, bmp.data(), gw, gh, gw, scale, scale, fx, 0, cp);
                int bx = (int)std::floor(pen) + x0, by = (int)std::lround(base) + y0;
                for (int yy = 0; yy < gh; yy++)
                    for (int xx = 0; xx < gw; xx++) {
                        unsigned char v = bmp[(size_t)yy * gw + xx];
                        if (v) blend(bx + xx, by + yy, color, v / 255.f);
                    }
            }
            pen += adv * scale;
            if (i + 1 < cps.size()) pen += stbtt_GetCodepointKernAdvance(info, cp, (int)cps[i + 1]) * scale;
        }
    }
}

void Canvas::paste(const Image& src, int x, int y, bool alpha) {
    for (int yy = 0; yy < src.h; yy++)
        for (int xx = 0; xx < src.w; xx++) {
            int X = x + xx, Y = y + yy;
            if (X < 0 || Y < 0 || X >= img.w || Y >= img.h) continue;
            const uint8_t* s = &src.rgba[((size_t)yy * src.w + xx) * 4];
            uint8_t* d = &img.rgba[((size_t)Y * img.w + X) * 4];
            if (!alpha) {  // Image.paste without a mask copies the pixels outright
                d[0] = s[0];
                d[1] = s[1];
                d[2] = s[2];
                d[3] = s[3];
            } else {  // with the image as its own mask: blend by its alpha
                float a = s[3] / 255.f;
                for (int k = 0; k < 4; k++) d[k] = (uint8_t)std::lround(s[k] * a + d[k] * (1 - a));
            }
        }
}

void Canvas::composite(const Image& over) {
    for (int y = 0; y < img.h && y < over.h; y++)
        for (int x = 0; x < img.w && x < over.w; x++) {
            const uint8_t* s = &over.rgba[((size_t)y * over.w + x) * 4];
            if (s[3]) blend(x, y, {s[0], s[1], s[2], s[3]}, 1.f);
        }
}
