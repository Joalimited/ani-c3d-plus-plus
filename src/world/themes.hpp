// Visual themes (port of themes.py): palette for the 3D world, floor/fog parameters, HUD colours and an animated effect.
// Also the settings file.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "../gfx/mathx.hpp"
#include "json.hpp"

struct Theme {
    std::string key, label;
    C3 fog;
    float density;
    C3 floor_base, floor_small, floor_big, glow;
    int levels;  // posterize steps per channel, 0 = off
    C3 root, core, ped, ped_sel, tower, tower_off, wire, pulse, slab, ep, ep_seen, ep_play, link, t_root, t_ped, t_field;
    C4 panel, edge;
    C3 text, dim, accent, good, logo, wood;
    std::vector<C3> books;  // spine colours
    std::string fx;
    float floor_style = 0;  // 0 = neon grid, 1 = forest floor, 2 = forest floor with cherry petals
};

namespace themes {
const std::map<std::string, Theme>& all();
const std::vector<std::string>& order();
const Theme& get(const std::string& key);  // falls back to "fsn"

nlohmann::json load_settings();
void save_settings(const nlohmann::json& changes);
std::string load_theme();
void save_theme(const std::string& key);
}  // namespace themes
