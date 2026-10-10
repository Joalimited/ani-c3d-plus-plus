// Bundled data (font, Windows mpv.zip). On Windows they are RCDATA resources inside the exe, so the program is a single
// file; elsewhere they are plain files found through paths::resource().
#pragma once
#include <string>

namespace res {
bool get(const std::string& name, std::string& out);  // name: "font.ttf", "mpv.zip"
bool has(const std::string& name);
}  // namespace res
