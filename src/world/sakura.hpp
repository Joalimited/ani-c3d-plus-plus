// Sakura garden theme (port of sakura.py): cherry trees in bloom, traditional houses with glowing shoji walls, koi
// ponds with a red arched bridge and stone lanterns, bamboo, a raked zen garden, a pagoda, torii gates and Mount Fuji.
// Petals drift down, paper lanterns sway and koi swim. Not interactive.
#pragma once
#include <array>
#include <vector>

#include "../gfx/mathx.hpp"
#include "../util/pyrand.hpp"

class Renderer;

class Garden {
public:
    explicit Garden(long long seed = 5);
    std::vector<Box> boxes(double t, const V3& cam, Renderer* r);

private:
    PyRandom rnd;
    std::vector<std::array<double, 3>> blocked;  // trunks and buildings, so scenery doesn't overlap
    struct Lantern {
        double x, y, z;
        C3 col;
        double ph;
    };
    std::vector<Lantern> lanterns;
    struct Koi {
        int pond;
        double r, w, a, wob;
        C3 base, spot;
        double size;
    };
    std::vector<Koi> koi;
    std::vector<Box> static_;
    bool free_(double x, double z, double margin = 0.0) const;
    bool spot(PyRandom& r, double rmin, double rmax, double margin, int tries, double& x, double& z) const;
    void cherry(std::vector<Box>& out, PyRandom& r, double x, double z, double s);
    void house(std::vector<Box>& out, double x, double z, double yaw);
    void pagoda(std::vector<Box>& out, double x, double z, double s);
    void torii(std::vector<Box>& out, double x, double z, double yaw, double s = 1.0);
    void stone_lantern(std::vector<Box>& out, double x, double z, double s = 1.0);
    void pond(std::vector<Box>& out, PyRandom& r, double px, double pz, double pr);
    void bridge(std::vector<Box>& out, double x, double z, double yaw, double span = 26.0, double rise = 3.2);
    void bamboo(std::vector<Box>& out, PyRandom& r, double x, double z);
    void zen(std::vector<Box>& out, double x, double z, double yaw);
    void fuji(std::vector<Box>& out);
    std::vector<Box> scenery();
};
