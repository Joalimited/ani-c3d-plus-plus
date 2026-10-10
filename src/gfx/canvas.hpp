// A small anti-aliased 2D canvas standing in for Pillow's ImageDraw: the book's page art (rounded frames, lines,
// circles, polygons, anchored text, pasted images). Thread-safe per instance (the page art is drawn on a worker).
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "image.hpp"

struct RGBA {
    uint8_t r = 0, g = 0, b = 0, a = 255;
};

class Canvas {
public:
    Canvas(int w, int h, RGBA fill = {0, 0, 0, 0});
    Image img;
    int w() const { return img.w; }
    int h() const { return img.h; }
    void rounded_rect(double x0, double y0, double x1, double y1, double radius, const RGBA* fill, const RGBA* outline = nullptr, double width = 1);
    void line(const std::vector<std::pair<double, double>>& pts, RGBA color, double width = 1);
    void ellipse(double x0, double y0, double x1, double y1, const RGBA* fill, const RGBA* outline = nullptr, double width = 1);
    void polygon(const std::vector<std::pair<double, double>>& pts, RGBA fill);
    // Pillow anchors: "la" (left, ascender: the default) or "mm" (middle, middle); "\n" starts a new line
    void text(double x, double y, const std::string& s, double px, RGBA color, const std::string& anchor = "la", bool center_lines = false);
    static double textlength(const std::string& s, double px);
    std::vector<std::string> wrap(const std::string& text, double px, double width, size_t max_lines) const;
    void paste(const Image& src, int x, int y, bool alpha = true);
    void composite(const Image& over);  // Image.alpha_composite
    void blend(int x, int y, RGBA c, float coverage);

private:
    template <class F>
    void shade(double x0, double y0, double x1, double y1, F coverage, RGBA c);
};
