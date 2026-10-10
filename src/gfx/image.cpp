#include "image.hpp"

#include <webp/decode.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "../util/paths.hpp"
#include "stb_image.h"
#include "stb_image_resize2.h"
#include "stb_image_write.h"

namespace img {
Image decode_mem(const std::string& bytes) {
    Image im;
    int w = 0, h = 0, n = 0;
    unsigned char* p = stbi_load_from_memory((const unsigned char*)bytes.data(), (int)bytes.size(), &w, &h, &n, 4);
    if (p) {
        im.w = w;
        im.h = h;
        im.rgba.assign(p, p + (size_t)w * h * 4);
        stbi_image_free(p);
        return im;
    }
    uint8_t* q = WebPDecodeRGBA((const uint8_t*)bytes.data(), bytes.size(), &w, &h);
    if (q) {
        im.w = w;
        im.h = h;
        im.rgba.assign(q, q + (size_t)w * h * 4);
        WebPFree(q);
        return im;
    }
    throw std::runtime_error("cannot identify image file");
}

Image decode_file(const std::string& path) {
    std::string bytes;
    if (!paths::read_file(path, bytes)) throw std::runtime_error("cannot read " + path);
    return decode_mem(bytes);
}

Image resize(const Image& src, int w, int h) {
    Image out;
    out.w = std::max(1, w);
    out.h = std::max(1, h);
    out.rgba.resize((size_t)out.w * out.h * 4);
    stbir_resize_uint8_srgb(src.rgba.data(), src.w, src.h, 0, out.rgba.data(), out.w, out.h, 0, STBIR_RGBA);
    return out;
}

Image thumbnail(const Image& src, int max_w, int max_h) {
    if (src.w <= max_w && src.h <= max_h) return src;
    double k = std::min((double)max_w / src.w, (double)max_h / src.h);
    int w = std::max(1, (int)std::lround(src.w * k)), h = std::max(1, (int)std::lround(src.h * k));
    return resize(src, w, h);
}

Image open_thumb(const std::string& path, int max_w, int max_h) {
    Image im = decode_file(path);
    if (im.h > 1.6 * im.w) return thumbnail(im, std::min(max_w, 900), 1 << 16);  // long-strip (webtoon) page: limit only the width
    return thumbnail(im, max_w, max_h);
}

bool save_png(const std::string& path, const Image& im) { return stbi_write_png(path.c_str(), im.w, im.h, 4, im.rgba.data(), im.w * 4) != 0; }

static void append_cb(void* ctx, void* data, int size) { ((std::string*)ctx)->append((const char*)data, (size_t)size); }

bool save_jpeg(const std::string& path, const Image& im, int quality, std::string* out_bytes) {
    std::vector<uint8_t> rgb((size_t)im.w * im.h * 3);
    for (size_t i = 0; i < (size_t)im.w * im.h; i++) {  // flatten onto white (PDF pages)
        float a = im.rgba[i * 4 + 3] / 255.f;
        for (int c = 0; c < 3; c++) rgb[i * 3 + c] = (uint8_t)(im.rgba[i * 4 + c] * a + 255 * (1 - a));
    }
    std::string buf;
    if (!stbi_write_jpg_to_func(append_cb, &buf, im.w, im.h, 3, rgb.data(), quality)) return false;
    if (out_bytes) *out_bytes = buf;
    if (!path.empty()) return paths::write_file(path, buf);
    return true;
}
}  // namespace img
