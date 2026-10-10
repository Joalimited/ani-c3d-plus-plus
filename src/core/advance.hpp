// Advancements: Minecraft-style achievements generated from what you watch and read (port of advance.py). Unlocks live in
// the watch database (`advancement` table), shared with ani-3d / ani-tui. The first evaluation on an existing history is silent.
#pragma once
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace advance {
struct Item {
    std::string key, title, desc;
    long long have = 0, need = 1;
    bool done = false;
};
struct Group {
    std::string group;
    std::vector<Item> items;
};
using Toast = std::tuple<std::string, std::string, std::string>;  // (title, desc, group)

std::vector<Group> compute();
std::vector<Item> game_items();
std::set<std::string> unlocked();

class Tracker {
public:
    Tracker();
    std::vector<Toast> refresh();               // advancements earned since the last call
    std::optional<Toast> award(const std::string& key);  // a game-event advancement
    std::vector<Group> groups();
    std::pair<int, int> counts();

private:
    std::set<std::string> have_;
    bool fresh_;
    std::vector<Group> cache_;
};
}  // namespace advance
