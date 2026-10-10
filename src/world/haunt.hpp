// Halloween theme (port of haunt.py): a haunted graveyard you can poke, feed and follow - the same idea as the herd.
// Ghosts drift and BOO, skeletons fall to pieces when poked, zombies shamble toward candy, black cats arch their backs,
// a witch loops overhead and spiders drop out of the dark on silk threads.
#pragma once
#include "creatures.hpp"

class Haunt : public Creatures {
public:
    explicit Haunt(long long seed = 13);
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
    std::vector<Box> static_;
    std::vector<Box> scenery();
    std::vector<Box> house(double t);
    std::array<double, 3> pick_spot(const Beast& d, const World& w);
};
