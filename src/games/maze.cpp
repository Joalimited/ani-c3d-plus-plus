#include "maze.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <tuple>

#include "../gfx/renderer.hpp"
#include "../gfx/text.hpp"
#include "../util/strutil.hpp"
#include "../world/scene.hpp"
#include "../world/themes.hpp"

namespace {
struct Item {
    std::string anime, item;
    C3 col;
    bool operator==(const Item& o) const { return anime == o.anime && item == o.item; }
};
// keyword in the anime title -> (anime, item, colour)
const std::vector<std::pair<std::string, Item>> ITEMS = {
    {"dragon ball", {"Dragon Ball", "the 4-Star Dragon Ball", {1.0, 0.6, 0.1}}},
    {"one piece", {"One Piece", "the Gomu Gomu no Mi", {0.9, 0.2, 0.7}}},
    {"death note", {"Death Note", "the Death Note", {0.25, 0.25, 0.3}}},
    {"naruto", {"Naruto", "the Leaf Village headband", {0.1, 0.4, 1.0}}},
    {"fullmetal", {"Fullmetal Alchemist", "the Philosopher's Stone", {1.0, 0.1, 0.2}}},
    {"steins", {"Steins;Gate", "the Phone Microwave", {0.8, 0.9, 1.0}}},
    {"demon slayer", {"Demon Slayer", "the Nichirin Blade", {0.2, 0.9, 0.8}}},
    {"kimetsu", {"Demon Slayer", "the Nichirin Blade", {0.2, 0.9, 0.8}}},
    {"jujutsu", {"Jujutsu Kaisen", "Sukuna's Finger", {0.8, 0.1, 0.3}}},
    {"bleach", {"Bleach", "Zangetsu", {0.7, 0.8, 1.0}}},
    {"attack on titan", {"Attack on Titan", "the Basement Key", {0.9, 0.8, 0.3}}},
    {"shingeki", {"Attack on Titan", "the Basement Key", {0.9, 0.8, 0.3}}},
    {"hunter x hunter", {"Hunter x Hunter", "the Hunter License", {0.2, 0.9, 0.3}}},
    {"fairy tail", {"Fairy Tail", "the Fairy Tail guild mark", {1.0, 0.4, 0.2}}},
    {"sword art", {"Sword Art Online", "the Elucidator", {0.3, 0.3, 0.3}}},
    {"pokemon", {"Pokemon", "the Poke Ball", {1.0, 0.1, 0.1}}},
    {"my hero", {"My Hero Academia", "One For All", {0.2, 0.9, 0.2}}},
    {"boku no hero", {"My Hero Academia", "One For All", {0.2, 0.9, 0.2}}},
    {"frieren", {"Frieren", "the Flower-Field Magic grimoire", {0.8, 0.6, 1.0}}},
    {"sousou", {"Frieren", "the Flower-Field Magic grimoire", {0.8, 0.6, 1.0}}},
    {"spirited away", {"Spirited Away", "Haku's golden seal", {1.0, 0.85, 0.2}}},
    {"cowboy bebop", {"Cowboy Bebop", "the Swordfish II key", {0.9, 0.5, 0.1}}},
    {"evangelion", {"Neon Genesis Evangelion", "the Lance of Longinus", {0.9, 0.2, 0.9}}},
    {"code geass", {"Code Geass", "Lelouch's Geass contract", {0.8, 0.1, 0.8}}},
    {"chainsaw", {"Chainsaw Man", "Pochita's pull cord", {1.0, 0.3, 0.1}}},
    {"spy x family", {"Spy x Family", "Anya's peanut", {0.95, 0.75, 0.5}}},
    {"haikyu", {"Haikyu!!", "the match volleyball", {1.0, 0.9, 0.2}}},
    {"tokyo ghoul", {"Tokyo Ghoul", "Kaneki's mask", {0.9, 0.9, 0.9}}},
    {"sailor moon", {"Sailor Moon", "the Silver Crystal", {1.0, 0.8, 0.9}}},
    {"one punch", {"One Punch Man", "Saitama's hero cape", {1.0, 0.9, 0.1}}},
    {"mob psycho", {"Mob Psycho 100", "Mob's 100% meter", {0.9, 0.3, 0.9}}},
    {"gintama", {"Gintama", "the wooden sword Toyako", {0.7, 0.5, 0.2}}},
    {"re:zero", {"Re:Zero", "the Witch's Cult sigil", {0.6, 0.2, 0.9}}},
    {"konosuba", {"KonoSuba", "Megumin's staff", {0.7, 0.1, 0.1}}},
    {"black clover", {"Black Clover", "Asta's five-leaf grimoire", {0.2, 0.2, 0.2}}},
};

std::optional<Item> item_for(const std::string& title) {
    std::string t = su::lower(title);
    for (auto& [kw, it] : ITEMS)
        if (su::contains(t, kw)) return it;
    return std::nullopt;
}

template <class T>
void shuffle(std::vector<T>& v) {
    for (size_t i = v.size(); i > 1; i--) std::swap(v[i - 1], v[(size_t)grand().randbelow((long long)i)]);
}

class Maze : public Game {
public:
    explicit Maze(GameHost& a) : Game(a) {
        name = "DATA MAZE";
        help = "arrows / WASD move   find the item named in the clue   R restart   Esc exit";
    }
    static constexpr double CELL = 2.6;
    int level = 0, cw = 9, ch = 7;
    bool next_level = false, won = false;
    std::map<std::pair<int, int>, bool> wall_e, wall_s;
    struct Thing {
        std::pair<int, int> cell;
        std::string anime, name;
        C3 col;
        bool got;
    };
    Thing target;
    std::vector<Thing> decoys;
    double px = 0, pz = 0, face = 1, run = 0, time_left = 60, clue_t = 3;
    std::pair<std::string, double> flash{"", 0.0};
    CamGoal cam_{{0, 0, 0}, 0.0, 68 * PI / 180, 30};
    CamGoal cam() const override { return cam_; }

    void reset() override {
        level = next_level ? level : 0;
        next_level = false;
        if (level == 0) score = 0;
        build_level();
    }

    std::pair<Item, std::vector<Item>> pick_targets() {
        std::vector<Item> pool;
        std::vector<std::string> titles;
        for (auto& p : app.world_scene().peds) titles.push_back(p.title);
        for (auto& t : app.library_titles()) titles.push_back(t);
        for (auto& t : titles) {
            auto hit = item_for(t);
            if (hit && std::find(pool.begin(), pool.end(), *hit) == pool.end()) pool.push_back(*hit);
        }
        std::vector<Item> known = pool;
        std::vector<Item> rest;
        for (auto& [kw, it] : ITEMS)
            if (std::find(pool.begin(), pool.end(), it) == pool.end()) rest.push_back(it);
        shuffle(rest);
        std::set<std::string> seen;
        std::vector<Item> mixed;
        std::vector<Item> all = known;
        all.insert(all.end(), rest.begin(), rest.end());
        for (auto& x : all)
            if (seen.insert(x.anime).second) mixed.push_back(x);
        Item tgt = !known.empty() ? grand().choice(known) : mixed[0];
        std::vector<Item> dec;
        for (auto& x : mixed)
            if (x.anime != tgt.anime) dec.push_back(x);
        shuffle(dec);
        size_t n = std::min(dec.size(), (size_t)(3 + std::min(level, 3)));
        dec.resize(n);
        return {tgt, dec};
    }

    void build_level() {
        int L = level;
        cw = 9 + 2 * std::min(L, 4);
        ch = 7 + 2 * std::min(L, 3);
        wall_e.clear();
        wall_s.clear();
        for (int x = 0; x < cw; x++)
            for (int y = 0; y < ch; y++) wall_e[{x, y}] = wall_s[{x, y}] = true;
        // recursive backtracker
        std::set<std::pair<int, int>> seen = {{0, ch - 1}};
        std::vector<std::pair<int, int>> stack = {{0, ch - 1}};
        while (!stack.empty()) {
            auto [x, y] = stack.back();
            std::vector<std::pair<int, int>> nbrs;
            for (auto [dx, dy] : {std::pair<int, int>{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
                int nx = x + dx, ny = y + dy;
                if (0 <= nx && nx < cw && 0 <= ny && ny < ch && !seen.count({nx, ny})) nbrs.push_back({nx, ny});
            }
            if (nbrs.empty()) {
                stack.pop_back();
                continue;
            }
            auto [nx, ny] = grand().choice(nbrs);
            if (nx > x) wall_e[{x, y}] = false;
            else if (nx < x) wall_e[{nx, ny}] = false;
            else if (ny > y) wall_s[{x, y}] = false;
            else wall_s[{nx, ny}] = false;
            seen.insert({nx, ny});
            stack.push_back({nx, ny});
        }
        for (int k = 0; k < cw * ch / 12; k++) {  // a few extra openings so it is not a pure tree
            int x = (int)grand().randbelow(cw - 1), y = (int)grand().randbelow(ch - 1);
            if (grand().random() < 0.5) wall_e[{x, y}] = false;
            else wall_s[{x, y}] = false;
        }
        // distances from the start: the target goes far away, decoys at middling distances
        std::pair<int, int> start{0, ch - 1};
        std::map<std::pair<int, int>, int> dist = {{start, 0}};
        std::vector<std::pair<int, int>> queue = {start};
        auto wall = [&](std::map<std::pair<int, int>, bool>& m, int x, int y) {
            auto it = m.find({x, y});
            return it == m.end() ? true : it->second;
        };
        for (size_t qi = 0; qi < queue.size(); qi++) {
            auto [cx, cy] = queue[qi];
            const std::tuple<int, int, bool> moves[4] = {{1, 0, wall(wall_e, cx, cy)}, {-1, 0, wall(wall_e, cx - 1, cy)}, {0, 1, wall(wall_s, cx, cy)}, {0, -1, wall(wall_s, cx, cy - 1)}};
            for (auto& [dx, dy, blocked] : moves) {
                std::pair<int, int> n{cx + dx, cy + dy};
                if (!blocked && !dist.count(n) && 0 <= n.first && n.first < cw && 0 <= n.second && n.second < ch) {
                    dist[n] = dist[{cx, cy}] + 1;
                    queue.push_back(n);
                }
            }
        }
        std::vector<std::pair<int, int>> cells = queue;  // insertion order, like the dict
        std::stable_sort(cells.begin(), cells.end(), [&](auto& a, auto& b) { return dist[a] > dist[b]; });
        auto [tgt, dec] = pick_targets();
        target = {cells[0], tgt.anime, tgt.item, tgt.col, false};
        std::vector<std::pair<int, int>> far(cells.begin() + 1, cells.begin() + std::min(cells.size(), cells.size() / 2 + 1));
        shuffle(far);
        decoys.clear();
        for (size_t i = 0; i < far.size() && i < dec.size(); i++) decoys.push_back({far[i], dec[i].anime, dec[i].item, dec[i].col, false});
        std::tie(px, pz) = center(start.first, start.second);
        face = 1;
        run = 0.0;
        time_left = std::min(150.0, 40 + dist[cells[0]] * 1.6 + 8 * L);
        over = false;
        won = false;
        flash = {"", 0.0};
        clue_t = 3.0;
        cam_ = {{0, 0, 0.6}, 0.0, 68 * PI / 180, std::max(cw * CELL * 0.72, ch * CELL * 1.35) + 6};
        if (app.is_current_game(this)) app.world_scene().cam.goto_(cam_.target, cam_.yaw, cam_.pitch, cam_.dist);
    }

    std::pair<double, double> center(int cx, int cy) const { return {(cx - cw / 2.0 + 0.5) * CELL, (cy - ch / 2.0 + 0.5) * CELL}; }

    // walls as rectangles near the player
    std::vector<std::array<double, 4>> wall_rects(double near_x, double near_z) {
        const double C = CELL, T = 0.34;
        std::vector<std::array<double, 4>> out;
        int gx = (int)(near_x / C + cw / 2.0), gy = (int)(near_z / C + ch / 2.0);
        for (int x = std::max(gx - 2, 0); x < std::min(gx + 3, cw); x++)
            for (int y = std::max(gy - 2, 0); y < std::min(gy + 3, ch); y++) {
                double x0 = (x - cw / 2.0) * C, z0 = (y - ch / 2.0) * C;
                if (wall_e[{x, y}]) out.push_back({x0 + C - T / 2, z0, x0 + C + T / 2, z0 + C});
                if (wall_s[{x, y}]) out.push_back({x0, z0 + C - T / 2, x0 + C, z0 + C + T / 2});
                if (x == 0) out.push_back({x0 - T / 2, z0, x0 + T / 2, z0 + C});
                if (y == 0) out.push_back({x0, z0 - T / 2, x0 + C, z0 + T / 2});
            }
        return out;
    }

    void move(double dx, double dz) {
        const double r = 0.42;
        for (int axis = 0; axis < 2; axis++) {
            double nx = axis == 0 ? px + dx : px, nz = axis == 0 ? pz : pz + dz;
            bool blocked = false;
            for (auto& w : wall_rects(nx, nz))
                if (w[0] - r < nx && nx < w[2] + r && w[1] - r < nz && nz < w[3] + r) {
                    blocked = true;
                    break;
                }
            if (!blocked) {
                px = nx;
                pz = nz;
            }
        }
    }

    std::vector<std::string> achievements() override {
        std::vector<std::string> out;
        if (over && won) out.push_back("maze1");
        if (over && won && level >= 2) out.push_back("maze3");
        return out;
    }

    void key(const std::string& k) override {
        if (k == "r") {
            next_level = false;
            level = 0;
            reset();
        } else if (k == "Enter" && over && won) {
            level++;
            next_level = true;
            reset();
        }
    }

    void update(double dt, const Keys& keys) override {
        clue_t = std::max(0.0, clue_t - dt);
        flash.second = std::max(0.0, flash.second - dt);
        if (over) return;
        time_left -= dt;
        if (time_left <= 0) {
            over = true;
            won = false;
            return;
        }
        int mxv = (held(keys, {"ArrowRight", "d"}) ? 1 : 0) - (held(keys, {"ArrowLeft", "a"}) ? 1 : 0);
        int mzv = (held(keys, {"ArrowDown", "s"}) ? 1 : 0) - (held(keys, {"ArrowUp", "w"}) ? 1 : 0);
        if (mxv || mzv) {
            double n = std::hypot(mxv, mzv);
            move(mxv / n * 6.2 * dt, mzv / n * 6.2 * dt);
            run += dt * 12;
            if (mxv) face = mxv;
        }
        std::vector<Thing*> all = {&target};
        for (auto& d : decoys) all.push_back(&d);
        for (auto* it : all) {
            if (it->got) continue;
            auto [ix, iz] = center(it->cell.first, it->cell.second);
            if (std::hypot(ix - px, iz - pz) < 0.95) {
                it->got = true;
                if (it == &target) {
                    over = true;
                    won = true;
                    score += 500 + 25 * (int)time_left + 100 * level;
                } else {
                    time_left = std::max(0.0, time_left - 8);
                    flash = {"That's " + it->name + " from " + it->anime + " - wrong item!  -8s", 2.5};
                }
            }
        }
    }

    std::vector<Box> build(Renderer& r, TextCache& text, const Theme& th, double t) override {
        const double C = CELL;
        double W = cw * C, Hh = ch * C;
        std::vector<Box> out = {box(0, -0.25, 0, W + 2.2, 0.5, Hh + 2.2, th.slab, 0.08)};
        for (int gx = 0; gx <= cw; gx++) out.push_back(box((gx - cw / 2.0) * C, 0.02, 0, 0.05, 0.05, Hh, th.wire, 0.35, 0.0, 0.0));  // the FSN floor grid
        for (int gz = 0; gz <= ch; gz++) out.push_back(box(0, 0.02, (gz - ch / 2.0) * C, W, 0.05, 0.05, th.wire, 0.35, 0.0, 0.0));
        const double T = 0.34, WH = 1.9;
        for (int x = 0; x < cw; x++)
            for (int y = 0; y < ch; y++) {
                double x0 = (x - cw / 2.0) * C, z0 = (y - ch / 2.0) * C;
                if (wall_e[{x, y}]) out.push_back(box(x0 + C, WH / 2, z0 + C / 2, T, WH, C + T, th.ped, 0.45, 0.0, 1.0));
                if (wall_s[{x, y}]) out.push_back(box(x0 + C / 2, WH / 2, z0 + C, C + T, WH, T, th.ped, 0.45, 0.0, 1.0));
                if (x == 0) out.push_back(box(x0, WH / 2, z0 + C / 2, T, WH, C + T, th.ped, 0.45, 0.0, 1.0));
                if (y == 0) out.push_back(box(x0 + C / 2, WH / 2, z0, C + T, WH, T, th.ped, 0.45, 0.0, 1.0));
            }
        auto [sx, sz] = center(0, ch - 1);  // start pad and items
        out.push_back(box(sx, 0.05, sz, C * 0.8, 0.1, C * 0.8, th.ep_seen, 0.7));
        std::vector<const Thing*> all = {&target};
        for (auto& d : decoys) all.push_back(&d);
        for (auto* it : all) {
            if (it->got) continue;
            auto [ix, iz] = center(it->cell.first, it->cell.second);
            double bob = 0.25 * std::sin(t * 3 + ix);
            out.push_back(box(ix, 0.9 + bob, iz, 0.9, 0.9, 0.9, it->col, 1.7, t * 1.7));
            out.push_back(box(ix, 0.9 + bob, iz, 0.55, 0.55, 0.55, {1, 1, 1}, 1.9, -t * 2.3));
            out.push_back(box(ix, 0.12, iz, C * 0.5, 0.08, C * 0.5, it->col, 0.9));
            flat_label(r, text, su::utf8_substr(su::upper(su::replace(it->name, "the ", "")), 0, 22), ix, 0.15, iz + C * 0.42, 0.55, {1, 1, 1});
        }
        auto person = voxel_person(th, px, 0.0, pz, face, std::sin(run) * 0.35, false, 0.95);
        out.insert(out.end(), person.begin(), person.end());
        return out;
    }

    void hud(double W, double H) override {
        const Theme& th = app.theme();
        const Thing& t = target;
        app.txt("CLUE: find " + t.name + "  from  " + t.anime, W / 2, 92, 20, th.accent, true);
        double frac = std::max(0.0, std::min(1.0, time_left / 150.0));
        app.renderer().rect(W / 2 - 160, 124, 320, 12, C4(0, 0, 0, 0.6));
        app.renderer().rect(W / 2 - 158, 126, 316 * frac, 8, time_left > 15 ? C4(th.good, 1) : C4(1.0, 0.3, 0.2, 1.0));
        app.txt("LEVEL " + std::to_string(level + 1) + "    TIME " + std::to_string((int)time_left) + "s    SCORE " + std::to_string(score) + "    decoys: " +
                    std::to_string(decoys.size()),
                W / 2, 142, 13, th.dim, true);
        if (flash.second > 0) app.txt(flash.first, W / 2, H * 0.28, 22, {1, 0.5, 0.3}, true);
        if (over) {
            if (won) {
                app.txt("FOUND IT!  " + t.name, W / 2, H * 0.3, 34, th.good, true);
                app.txt("+" + std::to_string(500 + 25 * (int)time_left + 100 * level) + " points   -   Enter: next level (bigger maze)   R: restart", W / 2, H * 0.3 + 46, 16,
                        th.text, true);
            } else {
                app.txt("TIME'S UP", W / 2, H * 0.3, 34, {1, 0.3, 0.3}, true);
                app.txt("It was " + t.name + " from " + t.anime + "   -   R to try again", W / 2, H * 0.3 + 46, 16, th.text, true);
            }
        }
    }
};
}  // namespace

std::unique_ptr<Game> make_maze(GameHost& app) { return std::make_unique<Maze>(app); }
