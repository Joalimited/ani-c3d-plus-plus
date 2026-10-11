// Shared shape of the interactive theme creatures (dinosaurs, the haunted graveyard, the forest bugs): the app drives
// them all the same way - pick / poke / feed / step / boxes / shake.
#pragma once
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "../gfx/mathx.hpp"
#include "../util/pyrand.hpp"

class Renderer;

// obstacles and roaming area handed to the creatures every frame (app.roam_world)
struct World {
    std::vector<std::array<double, 3>> circles;  // x, z, r
    std::vector<std::array<double, 4>> rects;    // cx, cz, half w, half d
    std::pair<double, double> bounds{30, 90};
    bool has_platforms = false;
    double z_clip = 1e30;  // ambience props / particles are only built behind this z (the TV dashboard keeps them behind the menus)
    std::vector<std::array<double, 4>> platforms;  // forest: where section platforms stand (cx, cz, hw, hd)
};

struct Bait {
    double x, z;
    std::string kind;
    double t0;
};
using BaitP = std::shared_ptr<Bait>;

// one animal; a fat struct so every species can use the fields it needs
struct Beast {
    std::string kind, name, diet, length, era, fact, verb, eats;
    int i = 0;
    double speed = 0, radius = 0, pick_h = 0, pick_r = 0, sc = 1;
    bool flyer = false;
    double x = 0, y = 0, z = 0;
    double heading = 0, phase = 0, mv = 0, turn = 0, graze = 0;
    std::optional<std::array<double, 3>> target;  // (x, z) or (x, y, z) for flyers
    double idle = 0;
    bool moving = false;
    double roar_t0 = -9, look = 0, look_goal = 0, look_t = 0;
    double eat_until = 0, flee_until = 0;
    BaitP bait;
    double seed = 0;
    // species extras
    std::array<C3, 3> pal{};
    std::string pal_name;  // butterfly palette / ghost colour key
    C3 tint{1, 1, 1};
    double pal_k = 0;      // jewel beetle colour phase
    std::pair<double, double> foot{5, 2};
    double cx = 0, cz = 0, rad = 0, w = 0, a = 0, alt = 0, flap = 6;
    int last_step = 0;
    // forest
    double fly_until = -1, air = 0, open = 0, vx = 0, vy = 0, vz = 0, flap_w = 17;
    struct Hop {
        double t0, x0, z0, x1, z1, h, dur;
    };
    std::optional<Hop> hop;
};

class Creatures {
public:
    virtual ~Creatures() = default;
    std::vector<Beast> dinos;
    double shake = 0;
    virtual std::optional<int> pick(const V3& origin, const V3& dir) = 0;
    virtual Beast& poke(int i, double t) = 0;
    virtual Beast* feed(double x, double z, double t) = 0;
    virtual void step(double t, const World& world) = 0;
    virtual std::vector<Box> boxes(double t, const V3& cam, Renderer* r) = 0;
};

// helpers shared by the rigs
inline Part P_(double x, double y, double z, double sx, double sy, double sz, C3 col, double glow = 0.25, double rz = 0, double rx = 0, double yl = 0) {
    return Part{x, y, z, sx, sy, sz, col, glow, rz, rx, yl};
}
// a box joining two points of the local frame (a leg segment), rotated so its long (y) axis runs p0 -> p1
Part limb(V3 p0, V3 p1, double th, C3 col, double glow = 0.15);
// a soft contact shadow quad on the ground
void shadow(Renderer* r, double x, double z, double heading, double ln, double wd, double alpha, double off_l = 0.5, double off_w = 0.5);
// parts in a creature's local frame -> world boxes
void place(std::vector<Box>& out, const std::vector<Part>& parts, double x, double y0, double z, double heading, double sc, double edge);
bool blocked_basic(double x, double z, const World& w, double r);
