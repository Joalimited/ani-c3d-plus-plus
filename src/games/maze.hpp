// DATA MAZE (port of maze.py): an FSN-styled neon maze. A clue names an important item from an anime (from your own list
// when it can); find it among the decoys before the clock runs out. Every level the maze grows.
#pragma once
#include <memory>

#include "games.hpp"

std::unique_ptr<Game> make_maze(GameHost& app);
