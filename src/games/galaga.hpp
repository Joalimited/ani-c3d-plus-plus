// DINO GALAGA (port of galaga.py): a Galaga-style shooter where you fly a dinosaur-ship and every wave ends with an
// anime-character boss.
#pragma once
#include <memory>

#include "games.hpp"

std::unique_ptr<Game> make_galaga(GameHost& app);
