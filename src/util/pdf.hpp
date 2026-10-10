// A minimal PDF writer: one page per image, each embedded as a JPEG (what Pillow's save("PDF", save_all=True) produced).
#pragma once
#include <string>
#include <vector>

#include "../gfx/image.hpp"

namespace pdf {
// pages are sized from the pixel size at `dpi`; returns false when the file could not be written
bool write_images(const std::string& path, const std::vector<Image>& pages, double dpi = 110.0, int quality = 88);
}
