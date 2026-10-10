// Mini-games played inside the 3D world (port of games.py). Each game owns its state, draws boxes/quads and its HUD bits.
#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "../gfx/mathx.hpp"
#include "../util/pyrand.hpp"
#include "json.hpp"

class Renderer;
class TextCache;
class Scene;
class Tasks;
struct Theme;

// what a game may ask of the app
class GameHost {
public:
    virtual ~GameHost() = default;
    virtual double now() const = 0;  // app.t
    virtual Scene& world_scene() = 0;
    virtual const Theme& theme() const = 0;
    virtual Renderer& renderer() = 0;
    virtual Tasks& tasks() = 0;
    virtual bool is_current_game(const void* g) const = 0;
    virtual std::vector<std::string> library_titles() const = 0;  // titles of the books on the open library shelves
    virtual void txt(const std::string& s, double x, double y, double px, C3 color, bool center = false) = 0;
    virtual void button(const std::string& label, double x, double y, double w, double h, std::function<void()> cb, bool active = false) = 0;
};

PyRandom& grand();  // the unseeded `random` module of the Python version

struct CamGoal {
    V3 target;
    double yaw, pitch, dist;
};

using Keys = std::set<std::string>;
inline bool held(const Keys& k, std::initializer_list<const char*> names) {
    for (auto* n : names)
        if (k.count(n)) return true;
    return false;
}

void flat_label(Renderer& r, TextCache& text, const std::string& s, double x, double y, double z, double height, C3 color, bool center = true);

// ------------------------------------------------------------ shared motion helpers
struct RollPart {
    double ox, oy, oz, sx, sy, sz;
    C3 col;
    double glow, rz;
};
// boxes of something that rolls: parts in the object's frame (axle along z, +x = travel); `roll` turns them about the
// axle, then `yaw` points the axle in the world
std::vector<Box> roller(double cx, double cy, double cz, double yaw, double roll, const std::vector<RollPart>& parts);
std::vector<RollPart> barrel_parts(const std::string& theme_fx, double t = 0.0);

// a book that tips over its front bottom edge, falls, bounces once and fades (Barrel Climb shelves, the library)
struct FallBook {
    double x = 0, y0 = 0, z = 0, w = 1, h = 1, d = 1;
    C3 color;
    std::string phase = "tip";
    double tilt = 0, wt = 0, vx = 0, vy = 0, vz = 0, wz = 0, spin = 1, age = 0;
    double px = 0, py = 0, pz = 0, roll = 0, rest = 0;
    // shelf bookkeeping (Barrel Climb)
    int level = 0, ci = 0;
    bool gone = false;
};
FallBook& tip_book(FallBook& bk, double push_z = 1.0, double push_x = 0.0, double spin = 1.0);
bool step_book(FallBook& bk, double dt, double floor);
std::vector<Box> book_boxes(const FallBook& bk, double glow = 0.15);
std::vector<Box> voxel_person(const Theme& th, double x, double y, double z, double face = 1, double swing = 0.0, bool arms_up = false, double sc = 0.75);

class Game {
public:
    explicit Game(GameHost& app) : app(app) {}
    virtual ~Game() = default;
    std::string name, help;
    int score = 0;
    bool over = false;
    std::string note;
    virtual CamGoal cam() const { return {{0, 0, 0}, 0.0, 60 * PI / 180, 28}; }
    virtual void reset() {}
    virtual void update(double dt, const Keys& keys) {}
    virtual void key(const std::string& k) {}
    virtual std::vector<Box> build(Renderer& r, TextCache& text, const Theme& th, double t) { return {}; }
    virtual void hud(double W, double H) {}
    virtual std::vector<std::string> achievements() { return {}; }  // keys of advance::GAME_ADV earned so far

protected:
    GameHost& app;
};

// games.GAMES: "snake", "breakout", "quiz", "kong", "trivia", "maze", "galaga"
std::unique_ptr<Game> make_game(const std::string& key, GameHost& app);
std::vector<std::string> game_keys();
