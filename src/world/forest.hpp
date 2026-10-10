// Enchanted forest theme (port of forest.py): frogs, ladybugs, beetles and butterflies you can poke, feed and follow.
// The dressing (trees, ferns, mushrooms, flowers, grass, rocks, logs, the pond) is built once; leaves and pollen drift.
#pragma once
#include "creatures.hpp"

class Forest : public Creatures {
public:
    explicit Forest(long long seed = 21);
    std::optional<int> pick(const V3& origin, const V3& dir) override;
    Beast& poke(int i, double t) override;
    Beast* feed(double x, double z, double t) override;
    void step(double t, const World& world) override;
    std::vector<Box> boxes(double t, const V3& cam, Renderer* r) override;

private:
    PyRandom rnd;
    double last = 0;
    bool has_last = false;
    std::vector<BaitP> baits;
    std::vector<std::array<double, 7>> dust;
    std::vector<std::array<double, 4>> rings;
    std::vector<std::array<double, 3>> trees;  // trunks the walkers steer around
    std::vector<Box> static_, inner_, inner_vis_;
    std::vector<std::array<double, 2>> inner_xz_;
    std::vector<std::array<double, 4>> platforms_;
    std::vector<long long> plat_key_;
    bool plat_init_ = false;

    bool free_(double x, double z, double margin) const;
    std::optional<std::pair<double, double>> spot(PyRandom& r, double rmin, double rmax, double margin = 0.0, int tries = 30) const;
    std::vector<Box> scenery();
    void clearing_cover();
    const std::vector<Box>& visible_cover();
    bool blocked(double x, double z, const World& w, double r) const;
    std::array<double, 3> pick_spot(const Beast& d, const World& w);
    void start_hop(Beast& d, double t, double tx, double tz, double h = 1.6, double dur = 0.55);
    void step_frog(Beast& d, double t, double dt, const World& w);
    void step_walker(Beast& d, double t, double dt, const World& w);
    void step_butterfly(Beast& d, double t, double dt, const World& w);
    std::vector<Box> ambient(double t, const V3& cam);
};
