// hianime scraping (a port of the `ani` shell script's --api mode) plus miruro.to as the fallback source. Port of scrape.py.
#pragma once
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace scrape {
struct Row {
    std::string id, title, poster;
};
struct Link {
    std::string url, referer, sub;
};
struct Range {
    double start = 0, end = 0;
};
struct MiMeta {
    std::string title;
    int episodes = 0;
    std::string format;
};

extern const std::string BASE, UA, MIRURO, MI_PREFIX;

std::string get(const std::string& url, const std::string& referer = "");
std::vector<Row> search(const std::string& query);
std::vector<Row> listing(const std::string& name);
std::vector<std::pair<std::string, std::string>> episodes(const std::string& anime_id);  // (episode_id, number)
Link link(const std::string& anime_id, const std::string& ep, const std::string& mode = "sub", const std::string& quality = "best");

std::vector<Row> mi_search(const std::string& query);
std::vector<std::string> mi_episodes(const std::string& anime_id);
Link mi_link(const std::string& anime_id, const std::string& ep, const std::string& mode = "sub");
std::string best_miruro(const std::string& title);  // "" when nothing matches
bool alive(const std::string& url, const std::string& referer);

// shared state (guarded by mu())
std::mutex& mu();
std::optional<MiMeta> mi_meta(const std::string& raw_id);
int quality_of(const std::string& url);
void set_quality(const std::string& url, int h);
std::map<std::string, Range> skip_info(const std::string& anime_id, const std::string& ep);
std::string urljoin(const std::string& base, const std::string& ref);
}  // namespace scrape
