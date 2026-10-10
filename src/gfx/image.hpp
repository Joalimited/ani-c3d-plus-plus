// Image decoding (JPEG / PNG / GIF / BMP via stb_image, WebP via libwebp) and Pillow-style thumbnails. Thread-safe.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    bool empty() const { return w <= 0 || h <= 0; }
};

namespace img {
Image decode_file(const std::string& path);   // throws std::runtime_error when unreadable
Image decode_mem(const std::string& bytes);
Image thumbnail(const Image& src, int max_w, int max_h);  // shrink to fit, keep the aspect, never enlarge
Image resize(const Image& src, int w, int h);
Image open_thumb(const std::string& path, int max_w, int max_h);  // long-strip pages keep their width (see app.py _open_thumb)
bool save_png(const std::string& path, const Image& im);
bool save_jpeg(const std::string& path, const Image& im, int quality, std::string* out_bytes = nullptr);
}  // namespace img
