// Grouping of search / history rows into the labelled sections of the 3D world (port of sections.py).
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "json.hpp"

namespace sections {
using json = nlohmann::json;
using Groups = std::vector<std::pair<std::string, std::vector<json>>>;
std::string kind_of(const json& row);
Groups group_search(const std::vector<json>& rows);
Groups group_history(const std::vector<json>& rows);  // rows need a `last` timestamp
}  // namespace sections
