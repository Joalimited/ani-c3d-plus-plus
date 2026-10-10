// Anime search/episodes/link across several sites (port of multisrc.py): hianime first, with miruro.to and AnimeHeaven
// merged in, so a show that one site hosts only partly (or in poor quality) still plays.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "scrape.hpp"

namespace multisrc {
struct Stream {
    std::string url, referer, sub;
    std::map<std::string, scrape::Range> skip;
    std::string source;  // label of the site the stream came from
};

extern const int GOOD_HEIGHT;
std::string src_of(const std::string& anime_id);
std::string label(const std::string& anime_id);
std::string akey(const std::string& title);
bool same_show(const std::string& a, const std::string& b);
std::string canon(const std::string& n);

std::vector<scrape::Row> ah_search(const std::string& query);
std::vector<std::string> ah_episodes(const std::string& anime_id);

std::vector<scrape::Row> search(const std::string& query);
std::vector<std::string> episodes(const std::string& anime_id, const std::string& title = "");
std::vector<std::string> alts(const std::string& anime_id, const std::string& title = "", bool lookup = true);
Stream link(const std::string& anime_id, const std::string& ep, const std::string& mode = "sub", const std::string& title = "");
std::string resolve(const std::string& title);
std::string source_label(const std::string& anime_id, const std::string& ep);  // SOURCE
}  // namespace multisrc
