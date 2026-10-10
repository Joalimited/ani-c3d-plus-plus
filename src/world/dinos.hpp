// Jurassic Park theme (port of dinos.py): a herd of articulated voxel dinosaurs you can poke, feed and follow.
#pragma once
#include "creatures.hpp"

class Herd : public Creatures {
public:
    explicit Herd(long long seed = 7);
    std::optional<int> pick(const V3& origin, const V3& dir) override;
    Beast& poke(int i, double t) override;
    Beast* feed(double x, double z, double t) override;
    void step(double t, const World& world) override;
    std::vector<Box> boxes(double t, const V3& cam, Renderer* r) override;

private:
    PyRandom rnd;
    double last = -1;
    bool has_last = false;
    std::vector<BaitP> baits;
    std::vector<std::array<double, 7>> dust;  // x, y, z, vx, vy, vz, life
    std::vector<std::array<double, 4>> rings;  // x, z, t0, radius
    V3 centre(const Beast& d) const;
    std::array<double, 3> pick_spot(const Beast& d, const World& w);
};

// the herd's rigs for other uses (Dino Galaga's ships): one pose of a species, in its local frame
std::vector<Part> dino_pose(const std::string& kind, double t, double ph, double mv, double roar, double flap);
std::array<C3, 3> dino_base_palette(const std::string& kind);
