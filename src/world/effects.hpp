// Animated theme effects (port of effects.py). Each returns boxes plus a lightning level; quads go straight to the renderer.
#pragma once
#include <vector>

#include "creatures.hpp"
#include "themes.hpp"

class Renderer;
class TextCache;

namespace effects {
double flash(const Theme& th, double t);
Creatures* creatures(const Theme& th);  // the theme's interactive animals, or nullptr
Creatures& herd();                      // the dinosaurs (fallback when a theme has none)
std::vector<Box> build(const Theme& th, Renderer& r, TextCache& text, double t, const V3& cam, const World* world, double& flash_out);
}  // namespace effects
