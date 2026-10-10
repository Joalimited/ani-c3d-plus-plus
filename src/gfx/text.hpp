// Text -> GPU texture, cached (port of text.py). Also a digit atlas for the many episode numbers.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "renderer.hpp"

struct TextTex {
    TexP tex;
    double w = 0, h = 0;  // logical size
};

// CPU text rasteriser (usable from any thread): RGBA pixels of `text` at `px` pixels, outlined like the Python HUD text
struct TextImage {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
};

class Font {
public:
    static Font& get();  // the bundled font (or a system fallback)
    bool ok() const { return ok_; }
    std::string path;
    TextImage render(const std::string& text, double px, uint8_t r, uint8_t g, uint8_t b, int stroke, bool outline = true) const;
    double text_length(const std::string& text, double px) const;  // advance width in pixels (ImageDraw.textlength)
    const void* info() const { return info_.get(); }  // stbtt_fontinfo*

private:
    Font();
    std::string data_;
    std::shared_ptr<void> info_;
    bool ok_ = false;
};

class TextCache {
public:
    TextCache(Renderer& r, double ratio = 1.0);
    TextTex get(const std::string& text, double px, C3 color = {1, 1, 1});
    TextTex get255(const std::string& text, double px, int r, int g, int b);
    TexP digit_tex;
    double digit_aspect = 64.0 / 110.0;
    UV digit_uv(int d) const { return {(float)(d / 10.0), 0.f, (float)((d + 1) / 10.0), 1.f}; }
    double ratio;

private:
    Renderer& r_;
    std::map<std::tuple<std::string, int, int, int, int>, TextTex> cache_;
};
