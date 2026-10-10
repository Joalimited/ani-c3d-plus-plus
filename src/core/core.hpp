// Shared logic (port of core.py): scraping backend, watch db, posters, manga and related-content lookups.
// Every network function blocks: call them from worker threads (see app/tasks.hpp).
#pragma once
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "json.hpp"

namespace core {
using json = nlohmann::json;

extern const std::vector<double> SPEEDS;
extern const std::string MANGA_SITE;

std::string norm(const std::string& t);
std::string base_title(const std::string& title);

// ---------- watch history / lists (sqlite) ----------
json wl_list();  // [{id, title, poster, added}]
void wl_add(const std::string& id, const std::string& title, const std::string& poster = "");
void wl_remove(const std::string& id);
std::vector<std::string> known_titles(int limit = 120);
void total_set(const std::string& id, int total);
std::set<std::string> db_watched(const std::string& id, const std::string& title);
void db_log(const std::string& id, const std::string& title, const std::string& ep);

// ---------- scraping backend (text in the old `ani --api` shape, cached like the Python version) ----------
std::string backend(const std::vector<std::string>& args);

// ---------- posters / manga ----------
std::string fetch_poster(const std::string& url);  // local file path
json find_manga(const std::string& title);          // [[id, name, chapters], ...]
json related_content(const std::string& title);     // {novels, games, sites}

json md_search(const std::string& query, int n = 30);
json md_alt_titles(const std::vector<std::string>& ids);  // {id: [en, jp]}
json md_chapters(const std::string& manga_id);           // [{id, chapter, pages}]
int md_probe(const std::string& manga_id);
json md_popular(int n = 18);
json md_for_title(const std::string& title);  // item or null
json md_pages(const std::string& chapter_id);
std::string mangafire_url(const std::string& title);
std::string fetch_manga_image(const std::string& url);  // local file path

json full_chapters(json& book);  // {chapters: [...], label}; may switch book.src / book.alt_id
int probe_any(const std::string& book_id);
json chapter_pages(const json& ch);
json search_all(const std::string& query, int n = 24);

void mr_add(const std::string& id, const std::string& title, const std::string& cover, const std::string& chapter, const std::string& title_jp = "");
json mr_list();
json mp_list();
void mp_set(const std::string& id, bool on, const std::string& title = "", const std::string& cover = "", const std::string& title_jp = "");

std::map<std::string, std::pair<std::string, long long>> shelf_overrides(const std::string& kind);
void shelf_set(const std::string& kind, const std::string& id, const std::string& section, long long slot);
std::set<std::string> hidden_ids(const std::string& kind);
std::vector<std::pair<std::string, std::string>> hidden_list(const std::string& kind);
void shelf_hide(const std::string& kind, const std::string& id, bool hide = true, const std::string& title = "");
void shelf_unhide_all(const std::string& kind);
void shelf_unpurge(const std::string& kind, const std::vector<std::string>& ids);
int shelf_purge_removed(const std::string& kind);
void shelf_clear(const std::string& kind, const std::string& id);

json fan_edits(const std::string& title, int n = 10);

// ---------- home ----------
void resume_save(const std::string& id, const std::string& title, const std::string& ep, double pos, double dur, const std::string& poster = "");
void resume_advance(const std::string& id, const std::string& title, const std::string& next_ep);
json continue_list(int limit = 12);
json anilist_top(int n = 24);
json recommended(const std::vector<std::string>& seeds, const std::vector<std::string>& exclude, int n = 18);
std::string clean_desc(const std::string& s, size_t cap = 420);
json media_blurb(const std::string& title, const std::string& kind = "anime");
json home_data();
}  // namespace core
