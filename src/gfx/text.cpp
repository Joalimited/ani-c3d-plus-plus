#include "text.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "../util/paths.hpp"
#include "../util/resources.hpp"
#include "../util/strutil.hpp"
#include "stb_truetype.h"

Font& Font::get() {
    static Font f;
    return f;
}

Font::Font() {
    bool have = res::get("font.ttf", data_);
    path = have ? (paths::WINDOWS ? std::string("bundled inside the exe") : paths::resource("fonts/NotoSansMono-Bold.ttf")) : "";
    if (!have) {
        for (const char* p : {"C:/Windows/Fonts/consolab.ttf", "/usr/share/fonts/google-noto/NotoSansMono-Bold.ttf",
                              "/usr/share/fonts/dejavu-sans-mono-fonts/DejaVuSansMono-Bold.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"}) {
            if (paths::read_file(p, data_)) {
                path = p;
                have = true;
                break;
            }
        }
    }
    if (!have) return;
    auto info = std::make_shared<stbtt_fontinfo>();
    if (stbtt_InitFont(info.get(), (const unsigned char*)data_.data(), stbtt_GetFontOffsetForIndex((const unsigned char*)data_.data(), 0))) {
        info_ = info;
        ok_ = true;
    }
}

double Font::text_length(const std::string& text, double px) const {
    if (!ok_) return text.size() * px * 0.6;
    auto* info = (stbtt_fontinfo*)info_.get();
    float scale = stbtt_ScaleForMappingEmToPixels(info, (float)px);
    double x = 0;
    for (uint32_t cp : su::utf8_decode(text)) {
        int adv, lsb;
        stbtt_GetCodepointHMetrics(info, (int)cp, &adv, &lsb);
        x += adv * scale;
    }
    return x;
}

// Like Pillow: the box is the ink's bounding box (anchored at the ascender line), padded by 3 px, with the text drawn in
// `colour` over a dark stroke `stroke` pixels wide.
TextImage Font::render(const std::string& text_in, double px, uint8_t r, uint8_t g, uint8_t b, int stroke, bool outline) const {
    TextImage img;
    std::string text = text_in.empty() ? " " : text_in;
    if (!ok_) {
        img.w = img.h = 2;
        img.rgba.assign(16, 0);
        return img;
    }
    auto* info = (stbtt_fontinfo*)info_.get();
    float scale = stbtt_ScaleForMappingEmToPixels(info, (float)px);
    int asc, desc, gap;
    stbtt_GetFontVMetrics(info, &asc, &desc, &gap);
    double ascent = asc * scale;
    auto cps = su::utf8_decode(text);
    struct G {
        int cp;
        double x;
        int x0, y0, x1, y1;
    };
    std::vector<G> gl;
    double pen = 0;
    double l = 1e9, t = 1e9, rr = -1e9, bb = -1e9;
    for (size_t i = 0; i < cps.size(); i++) {
        int cp = (int)cps[i];
        if (!stbtt_FindGlyphIndex(info, cp)) cp = '?';
        int adv, lsb;
        stbtt_GetCodepointHMetrics(info, cp, &adv, &lsb);
        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBoxSubpixel(info, cp, scale, scale, (float)(pen - std::floor(pen)), 0, &x0, &y0, &x1, &y1);
        gl.push_back({cp, pen, x0, y0, x1, y1});
        if (x1 > x0 && y1 > y0) {
            l = std::min(l, std::floor(pen) + x0);
            rr = std::max(rr, std::floor(pen) + x1);
            t = std::min(t, ascent + y0);
            bb = std::max(bb, ascent + y1);
        }
        pen += adv * scale;
        if (i + 1 < cps.size()) pen += stbtt_GetCodepointKernAdvance(info, cp, (int)cps[i + 1]) * scale;
    }
    if (l > rr) {  // no ink (spaces): the advance box
        l = 0;
        rr = pen;
        t = 0;
        bb = ascent - desc * scale;
    }
    const int pad = 3;
    double lm = std::min(l, 0.0), tm = std::min(t, 0.0);
    img.w = std::max((int)(rr - lm) + pad * 2, 2);
    img.h = std::max((int)(bb - tm) + pad * 2, 2);
    double ox = pad - lm, oy = pad - tm;
    std::vector<float> a((size_t)img.w * img.h, 0.f);
    for (auto& g0 : gl) {
        int gw = g0.x1 - g0.x0, gh = g0.y1 - g0.y0;
        if (gw <= 0 || gh <= 0) continue;
        std::vector<unsigned char> bmp((size_t)gw * gh);
        double fx = g0.x - std::floor(g0.x);
        stbtt_MakeCodepointBitmapSubpixel(info, bmp.data(), gw, gh, gw, scale, scale, (float)fx, 0, g0.cp);
        int bx = (int)std::floor(g0.x) + g0.x0 + (int)ox;
        int by = (int)std::lround(ascent) + g0.y0 + (int)oy;
        for (int y = 0; y < gh; y++)
            for (int x = 0; x < gw; x++) {
                int X = bx + x, Y = by + y;
                if (X < 0 || Y < 0 || X >= img.w || Y >= img.h) continue;
                float& d = a[(size_t)Y * img.w + X];
                d = std::min(1.f, d + bmp[(size_t)y * gw + x] / 255.f);
            }
    }
    std::vector<float> s;
    if (outline && stroke > 0) {  // dilate the coverage by `stroke` pixels (a round brush) for the outline
        s.assign(a.size(), 0.f);
        for (int y = 0; y < img.h; y++)
            for (int x = 0; x < img.w; x++) {
                float m = 0;
                for (int dy = -stroke; dy <= stroke; dy++)
                    for (int dx = -stroke; dx <= stroke; dx++) {
                        if (dx * dx + dy * dy > stroke * stroke + stroke) continue;
                        int X = x + dx, Y = y + dy;
                        if (X < 0 || Y < 0 || X >= img.w || Y >= img.h) continue;
                        m = std::max(m, a[(size_t)Y * img.w + X]);
                    }
                s[(size_t)y * img.w + x] = m;
            }
    }
    img.rgba.assign((size_t)img.w * img.h * 4, 0);
    const float sr = 0, sg = 6, sb = 16, sa = 235 / 255.f;  // stroke fill (0, 6, 16, 235)
    for (size_t i = 0; i < a.size(); i++) {
        float A = a[i];
        float S = s.empty() ? 0.f : s[i] * sa;
        float alpha = A + (1 - A) * S;
        if (alpha <= 0) continue;
        float cr = (r * A + sr * (1 - A) * S) / alpha, cg = (g * A + sg * (1 - A) * S) / alpha, cb = (b * A + sb * (1 - A) * S) / alpha;
        img.rgba[i * 4 + 0] = (uint8_t)std::min(255.f, cr);
        img.rgba[i * 4 + 1] = (uint8_t)std::min(255.f, cg);
        img.rgba[i * 4 + 2] = (uint8_t)std::min(255.f, cb);
        img.rgba[i * 4 + 3] = (uint8_t)std::min(255.f, alpha * 255.f + 0.5f);
    }
    return img;
}

TextCache::TextCache(Renderer& r, double ratio_) : ratio(ratio_), r_(r) {
    // the digit atlas: ten 64x110 cells, digits centred, white, no outline
    Font& f = Font::get();
    const int cw = 64, ch = 110;
    std::vector<uint8_t> atlas((size_t)cw * 10 * ch * 4, 0);
    for (int i = 0; i < 10; i++) {
        std::string d(1, (char)('0' + i));
        TextImage g = f.render(d, 96, 255, 255, 255, 0, false);
        double w = f.text_length(d, 96);
        int x0 = (int)(i * cw + (cw - w) / 2) - 3, y0 = 2 - 3;
        for (int y = 0; y < g.h; y++)
            for (int x = 0; x < g.w; x++) {
                int X = x0 + x, Y = y0 + y;
                if (X < i * cw || X >= (i + 1) * cw || Y < 0 || Y >= ch) continue;
                std::memcpy(&atlas[((size_t)Y * cw * 10 + X) * 4], &g.rgba[((size_t)y * g.w + x) * 4], 4);
            }
    }
    digit_tex = r_.make_texture(atlas.data(), cw * 10, ch);
    digit_aspect = (double)cw / ch;
}

TextTex TextCache::get255(const std::string& text, double px, int r, int g, int b) {
    auto key = std::make_tuple(text, (int)std::lround(px * 100), r, g, b);
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second;
    double size = std::max(6.0, std::round(px * ratio));
    TextImage img = Font::get().render(text, size, (uint8_t)r, (uint8_t)g, (uint8_t)b, std::max(1, (int)ratio));
    TextTex out{r_.make_texture(img.rgba.data(), img.w, img.h), img.w / ratio, img.h / ratio};
    if (cache_.size() > 600) cache_.clear();
    cache_[key] = out;
    return out;
}

TextTex TextCache::get(const std::string& text, double px, C3 color) {
    return get255(text, px, (int)(color.r * 255), (int)(color.g * 255), (int)(color.b * 255));
}
