// One book on a library shelf (an anime from your history / watch list, or a manga). Shared: the same book sits in the
// shelf lists, the layout and the open BookView, so changes show up everywhere (the dicts of the Python version).
#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../gfx/image.hpp"
#include "../gfx/mathx.hpp"
#include "../gfx/renderer.hpp"
#include "json.hpp"

struct Book {
    std::string id, title, title_en, title_jp, last, poster_url;
    std::vector<std::string> episodes;  // watched episodes / read chapters
    std::vector<std::string> all_eps;   // every episode / chapter number
    int total = 0;
    // manga
    std::string src = "md", alt_id, source;
    std::optional<nlohmann::json> chapters;  // [{id, chapter, src}] once loaded
    bool loading = false;
    std::vector<std::function<void()>> after;
    std::optional<std::optional<bool>> strip_pref;  // remembered book / scroll choice (outer: looked up yet)
    // shelf layout
    int watched = 0, row = 0;
    double thick = 1.0, x = 0, w = 0.62, h = 3.2;
    std::string section;
    bool face = false, starred = false, white_spine = false;
    C3 color{0.5, 0.5, 0.5};
    // art
    std::shared_ptr<const Image> poster_img;
    bool poster_loading = false;
    TexP card_tex;
};
using BookP = std::shared_ptr<Book>;
