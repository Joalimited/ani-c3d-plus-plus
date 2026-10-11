// Small retro-tech props built from boxes (parts in a local frame, front = +x): a CRT television, a VHS tape, a compact
// cassette, a Sony Walkman, a Macintosh 128K and a Windows-style butterfly. `flying()` places one with a bob and a tumble.
#pragma once
#include <string>
#include <vector>

#include "creatures.hpp"

namespace props {
std::vector<Part> crt_tv(double t, C3 screen, double scale = 1.0);
std::vector<Part> vhs(C3 label, double scale = 1.0);
std::vector<Part> cassette(C3 shell, C3 label, double t, double scale = 1.0);
std::vector<Part> walkman(double t, double scale = 1.0);
std::vector<Part> macintosh(double t, double scale = 1.0);
std::vector<Part> butterfly(double t, double phase, C3 upper, C3 lower, double scale = 1.0);

// the CRT mostly faces the viewer (+z): a little wobble, and every ~28 s a full slow turn
double crt_heading(double t);
// the television only floats in the sparse themes; the busy ones (creatures, forests, haunts) have enough going on
bool crt_theme(const std::string& fx);

// tilt a whole prop about its local z (nose up / down) and x (roll) axes - positions and orientations together
void tilt(std::vector<Part>& parts, double about_z, double about_x);
}  // namespace props
