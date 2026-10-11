#include "themes.hpp"

#include <mutex>

#include "../util/paths.hpp"

namespace themes {
using C = C3;
static Theme mk(const char* key, const char* label, C fog, float density, C fb, C fs, C fbig, C glow, int levels, C root, C core, C ped, C ped_sel,
                C tower, C tower_off, C wire, C pulse, C slab, C ep, C ep_seen, C ep_play, C link, C t_root, C t_ped, C t_field, C4 panel, C4 edge,
                C text, C dim, C accent, C good, C logo, C wood, std::vector<C> books, const char* fx = "", float floor_style = 0) {
    Theme t;
    t.key = key; t.label = label; t.fog = fog; t.density = density; t.floor_base = fb; t.floor_small = fs; t.floor_big = fbig; t.glow = glow;
    t.levels = levels; t.root = root; t.core = core; t.ped = ped; t.ped_sel = ped_sel; t.tower = tower; t.tower_off = tower_off; t.wire = wire;
    t.pulse = pulse; t.slab = slab; t.ep = ep; t.ep_seen = ep_seen; t.ep_play = ep_play; t.link = link; t.t_root = t_root; t.t_ped = t_ped;
    t.t_field = t_field; t.panel = panel; t.edge = edge; t.text = text; t.dim = dim; t.accent = accent; t.good = good; t.logo = logo; t.wood = wood;
    t.books = books; t.fx = fx; t.floor_style = floor_style;
    return t;
}

static std::vector<Theme> build() {
    return {
        mk("fsn", "CLASSIC FSN", {0.01, 0.02, 0.05}, 0.0075f, {0.012, 0.022, 0.055}, {0.008, 0.114, 0.19}, {0.075, 0.465, 0.71}, {0, 0.05, 0.11}, 0,
           {0.10, 0.18, 0.40}, {1.0, 0.18, 0.80}, {0.12, 0.22, 0.46}, {0.2, 0.3, 0.65}, {0.1, 0.75, 1.0}, {0.3, 0.4, 0.6}, {0.1, 0.75, 1.0}, {0.6, 1, 1},
           {0.08, 0.13, 0.30}, {0.10, 0.50, 1.0}, {0.12, 0.95, 0.50}, {1.0, 0.18, 0.80}, {1.0, 0.75, 0.2}, {0.55, 0.95, 1.0}, {0.65, 0.9, 1.0},
           {1.0, 0.9, 0.6}, {0.01, 0.05, 0.12, 0.82}, {0.10, 0.65, 0.95, 0.9}, {0.55, 0.95, 1.0}, {0.45, 0.62, 0.78}, {1.0, 0.95, 0.7}, {0.4, 1.0, 0.7},
           {0.4, 0.9, 1.0}, {0.10, 0.16, 0.30}, {{0.1, 0.5, 1.0}, {0.1, 0.8, 0.9}, {0.5, 0.3, 1.0}, {0.9, 0.2, 0.7}, {0.2, 0.9, 0.6}}),
        mk("jurassic", "JURASSIC PARK", {0.02, 0.035, 0.02}, 0.009f, {0.015, 0.025, 0.015}, {0.11, 0.09, 0.01}, {0.76, 0.52, 0.04}, {0.05, 0.04, 0.0}, 0,
           {0.07, 0.20, 0.11}, {0.95, 0.10, 0.08}, {0.07, 0.22, 0.12}, {0.12, 0.38, 0.20}, {1.0, 0.72, 0.05}, {0.35, 0.30, 0.10}, {1.0, 0.70, 0.10},
           {1.0, 0.95, 0.6}, {0.05, 0.12, 0.07}, {0.85, 0.55, 0.05}, {0.25, 0.80, 0.20}, {1.0, 0.12, 0.10}, {1.0, 0.2, 0.1}, {1.0, 0.85, 0.35},
           {1.0, 0.85, 0.5}, {1.0, 0.95, 0.6}, {0.03, 0.05, 0.02, 0.86}, {0.95, 0.65, 0.05, 0.9}, {1.0, 0.85, 0.35}, {0.62, 0.52, 0.26}, {1.0, 0.45, 0.2},
           {0.5, 1.0, 0.4}, {1.0, 0.75, 0.1}, {0.18, 0.12, 0.05}, {{0.85, 0.55, 0.05}, {0.2, 0.6, 0.2}, {0.8, 0.15, 0.1}, {0.5, 0.4, 0.1}, {0.15, 0.4, 0.25}},
           "jurassic"),
        mk("forest", "ENCHANTED FOREST", {0.19, 0.28, 0.2}, 0.0047f, {0.07, 0.13, 0.05}, {0.17, 0.3, 0.09}, {0.26, 0.18, 0.09}, {0.12, 0.1, 0.03}, 0,
           {0.30, 0.20, 0.11}, {1.0, 0.85, 0.35}, {0.26, 0.18, 0.10}, {0.40, 0.28, 0.15}, {0.55, 0.85, 0.25}, {0.35, 0.40, 0.25}, {0.45, 0.80, 0.22},
           {1.0, 0.95, 0.6}, {0.12, 0.20, 0.08}, {0.35, 0.65, 0.20}, {0.95, 0.75, 0.25}, {1.0, 0.35, 0.55}, {1.0, 0.8, 0.3}, {0.95, 0.95, 0.7},
           {0.92, 0.96, 0.78}, {1.0, 0.95, 0.7}, {0.04, 0.08, 0.04, 0.86}, {0.55, 0.85, 0.30, 0.9}, {0.90, 0.96, 0.78}, {0.62, 0.74, 0.52},
           {1.0, 0.82, 0.35}, {0.55, 1.0, 0.45}, {0.70, 0.95, 0.35}, {0.32, 0.21, 0.11},
           {{0.35, 0.55, 0.2}, {0.6, 0.35, 0.15}, {0.85, 0.7, 0.3}, {0.25, 0.45, 0.3}, {0.75, 0.3, 0.25}}, "forest", 1.0f),
        mk("sakura", "SAKURA GARDEN", {0.30, 0.22, 0.30}, 0.005f, {0.09, 0.12, 0.07}, {0.18, 0.24, 0.12}, {0.4, 0.34, 0.3}, {0.12, 0.06, 0.08}, 0,
           {0.35, 0.12, 0.10}, {1.0, 0.7, 0.8}, {0.30, 0.12, 0.10}, {0.50, 0.20, 0.16}, {1.0, 0.6, 0.75}, {0.4, 0.3, 0.35}, {1.0, 0.55, 0.7},
           {1.0, 0.9, 0.95}, {0.16, 0.10, 0.12}, {0.95, 0.55, 0.7}, {0.55, 0.85, 0.5}, {1.0, 0.85, 0.3}, {1.0, 0.6, 0.75}, {1.0, 0.9, 0.95},
           {1.0, 0.88, 0.92}, {1.0, 0.95, 0.85}, {0.08, 0.04, 0.07, 0.88}, {1.0, 0.6, 0.75, 0.9}, {1.0, 0.9, 0.94}, {0.78, 0.62, 0.7}, {1.0, 0.8, 0.45},
           {0.6, 1.0, 0.6}, {1.0, 0.62, 0.78}, {0.40, 0.17, 0.11}, {{0.95, 0.6, 0.7}, {0.85, 0.2, 0.15}, {0.2, 0.25, 0.4}, {0.95, 0.85, 0.6}, {0.4, 0.6, 0.35}},
           "sakura", 2.0f),
        mk("halloween", "HALLOWEEN", {0.06, 0.02, 0.09}, 0.0075f, {0.03, 0.01, 0.04}, {0.14, 0.035, 0.18}, {0.80, 0.36, 0.0}, {0.12, 0.04, 0.0}, 0,
           {0.20, 0.08, 0.25}, {1.0, 0.45, 0.0}, {0.22, 0.08, 0.28}, {0.40, 0.15, 0.50}, {1.0, 0.5, 0.05}, {0.40, 0.25, 0.40}, {1.0, 0.5, 0.05},
           {0.7, 1.0, 0.4}, {0.12, 0.05, 0.15}, {0.55, 0.20, 0.85}, {1.0, 0.5, 0.05}, {0.5, 1.0, 0.2}, {0.5, 1.0, 0.3}, {1.0, 0.7, 0.3}, {1.0, 0.75, 0.45},
           {0.8, 1.0, 0.5}, {0.06, 0.02, 0.08, 0.86}, {1.0, 0.5, 0.05, 0.9}, {1.0, 0.75, 0.4}, {0.65, 0.45, 0.60}, {0.6, 1.0, 0.3}, {0.6, 1.0, 0.3},
           {1.0, 0.5, 0.05}, {0.20, 0.09, 0.07}, {{1.0, 0.5, 0.05}, {0.55, 0.2, 0.85}, {0.3, 0.8, 0.2}, {0.8, 0.1, 0.1}, {0.9, 0.9, 0.85}}, "halloween"),
        mk("matrix", "MATRIX // HACKER", {0.0, 0.02, 0.0}, 0.008f, {0.0, 0.01, 0.0}, {0.0, 0.16, 0.035}, {0.075, 0.75, 0.225}, {0.0, 0.06, 0.01}, 0,
           {0.0, 0.15, 0.04}, {0.4, 1.0, 0.5}, {0.0, 0.20, 0.06}, {0.05, 0.40, 0.12}, {0.2, 1.0, 0.4}, {0.1, 0.35, 0.15}, {0.1, 1.0, 0.3}, {0.8, 1.0, 0.8},
           {0.0, 0.10, 0.03}, {0.05, 0.55, 0.18}, {0.6, 1.0, 0.7}, {1.0, 1.0, 1.0}, {0.6, 1.0, 0.6}, {0.4, 1.0, 0.5}, {0.4, 1.0, 0.5}, {0.8, 1.0, 0.8},
           {0.0, 0.04, 0.0, 0.88}, {0.1, 0.9, 0.3, 0.9}, {0.4, 1.0, 0.5}, {0.2, 0.60, 0.30}, {0.8, 1.0, 0.8}, {0.4, 1.0, 0.5}, {0.3, 1.0, 0.4},
           {0.0, 0.14, 0.04}, {{0.1, 0.8, 0.3}, {0.05, 0.5, 0.2}, {0.4, 1.0, 0.5}, {0.0, 0.35, 0.1}, {0.7, 1.0, 0.7}}, "matrix"),
        mk("retro", "RETRO ARCADE", {0.05, 0.0, 0.12}, 0.0026f, {0.03, 0.0, 0.08}, {0.40, 0.04, 0.28}, {0.09, 0.81, 0.90}, {0.15, 0.0, 0.20}, 6,
           {0.5, 0.1, 0.8}, {1.0, 0.9, 0.1}, {0.2, 0.2, 0.9}, {0.4, 0.4, 1.0}, {1.0, 0.2, 0.2}, {0.5, 0.5, 0.5}, {1.0, 0.3, 0.8}, {1.0, 1.0, 0.2},
           {0.1, 0.1, 0.4}, {0.1, 0.6, 1.0}, {0.2, 1.0, 0.2}, {1.0, 1.0, 0.1}, {1.0, 0.5, 0.1}, {1.0, 1.0, 0.3}, {0.4, 1.0, 1.0}, {1.0, 1.0, 0.3},
           {0.05, 0.0, 0.12, 0.88}, {1.0, 0.2, 0.8, 0.95}, {0.4, 1.0, 1.0}, {0.7, 0.5, 0.9}, {1.0, 1.0, 0.3}, {0.3, 1.0, 0.3}, {1.0, 0.3, 0.9},
           {0.35, 0.12, 0.55}, {{1.0, 0.2, 0.2}, {0.2, 0.4, 1.0}, {1.0, 0.9, 0.1}, {0.2, 1.0, 0.3}, {1.0, 0.3, 0.9}}, "retro"),
        mk("vcr", "OLD VCR (VHS)", {0.05, 0.05, 0.09}, 0.0062f, {0.04, 0.045, 0.08}, {0.20, 0.22, 0.33}, {0.55, 0.58, 0.72}, {0.06, 0.06, 0.1}, 9,
           {0.30, 0.32, 0.46}, {0.95, 0.15, 0.15}, {0.22, 0.24, 0.36}, {0.40, 0.42, 0.62}, {0.9, 0.9, 0.95}, {0.35, 0.36, 0.45}, {0.65, 0.68, 0.85},
           {1.0, 0.3, 0.3}, {0.12, 0.13, 0.22}, {0.45, 0.5, 0.72}, {0.35, 0.8, 0.5}, {0.95, 0.15, 0.15}, {0.95, 0.85, 0.3}, {0.92, 0.94, 1.0},
           {0.85, 0.88, 0.98}, {1.0, 0.92, 0.45}, {0.04, 0.04, 0.09, 0.86}, {0.7, 0.72, 0.95, 0.9}, {0.9, 0.93, 1.0}, {0.55, 0.58, 0.72},
           {1.0, 0.88, 0.25}, {0.45, 0.95, 0.55}, {0.95, 0.95, 1.0}, {0.18, 0.15, 0.13},
           {{0.75, 0.2, 0.2}, {0.25, 0.35, 0.7}, {0.8, 0.7, 0.25}, {0.3, 0.6, 0.4}, {0.55, 0.55, 0.6}}, "vcr"),
        // Windows XP Media Center Edition 2005: deep royal blue, glossy glass, the bright green start orb, butterflies
        mk("mce", "MEDIA CENTER 2005", {0.01, 0.05, 0.22}, 0.0058f, {0.0, 0.025, 0.13}, {0.03, 0.22, 0.55}, {0.25, 0.65, 1.0}, {0.0, 0.1, 0.3}, 0,
           {0.05, 0.18, 0.5}, {0.45, 0.9, 0.25}, {0.06, 0.22, 0.55}, {0.15, 0.45, 0.9}, {0.45, 0.9, 0.25}, {0.2, 0.35, 0.55}, {0.3, 0.8, 1.0},
           {0.7, 1.0, 0.5}, {0.03, 0.1, 0.3}, {0.1, 0.4, 0.85}, {0.2, 0.65, 0.35}, {0.55, 0.95, 0.3}, {0.4, 0.85, 1.0}, {0.75, 0.95, 1.0},
           {0.7, 0.9, 1.0}, {0.8, 1.0, 0.7}, {0.01, 0.05, 0.2, 0.85}, {0.3, 0.7, 1.0, 0.9}, {0.92, 0.97, 1.0}, {0.55, 0.72, 0.92}, {0.5, 0.92, 0.3},
           {0.5, 1.0, 0.4}, {0.6, 0.95, 0.4}, {0.08, 0.15, 0.3},
           {{0.1, 0.4, 0.85}, {0.35, 0.8, 0.25}, {0.15, 0.6, 0.9}, {0.95, 0.5, 0.1}, {0.7, 0.9, 1.0}}, "mce"),
    };
}

const std::map<std::string, Theme>& all() {
    static std::map<std::string, Theme> m = [] {
        std::map<std::string, Theme> out;
        for (auto& t : build()) out[t.key] = t;
        return out;
    }();
    return m;
}
const std::vector<std::string>& order() {
    static std::vector<std::string> o = [] {
        std::vector<std::string> out;
        for (auto& t : build()) out.push_back(t.key);
        return out;
    }();
    return o;
}
const Theme& get(const std::string& key) {
    auto it = all().find(key);
    return it == all().end() ? all().at("fsn") : it->second;
}

static std::mutex g_mu;
static std::string settings_path() { return paths::join(paths::config_dir(), "settings.json"); }

// first run: carry over the Python ani-3d's settings (theme, reading direction, skip switches, librarian searches...)
static void import_legacy() {
    static bool done = false;
    if (done) return;
    done = true;
    if (paths::exists(settings_path())) return;
    std::string old_dir = paths::WINDOWS ? paths::join(paths::getenv_or("APPDATA", paths::home()), "ani-3d") : paths::join(paths::home(), ".config/ani-3d");
    std::string raw;
    if (paths::read_file(paths::join(old_dir, "settings.json"), raw)) {
        paths::makedirs(paths::config_dir());
        paths::write_file(settings_path(), raw);
    }
}

nlohmann::json load_settings() {
    std::lock_guard<std::mutex> lk(g_mu);
    import_legacy();
    std::string raw;
    if (!paths::read_file(settings_path(), raw)) return nlohmann::json::object();
    try {
        auto j = nlohmann::json::parse(raw);
        return j.is_object() ? j : nlohmann::json::object();
    } catch (...) {
        return nlohmann::json::object();
    }
}
void save_settings(const nlohmann::json& changes) {
    auto data = load_settings();
    std::lock_guard<std::mutex> lk(g_mu);
    for (auto& [k, v] : changes.items()) data[k] = v;
    paths::makedirs(paths::config_dir());
    paths::write_file(settings_path(), data.dump());
}
#ifdef __ANDROID__
// the TV dashboard looks best in the green-on-black theme (the original Xbox dashboard's palette) until the user picks another
std::string load_theme() { return load_settings().value("theme", "matrix"); }
#else
std::string load_theme() { return load_settings().value("theme", "fsn"); }
#endif
void save_theme(const std::string& key) { save_settings({{"theme", key}}); }
}  // namespace themes
