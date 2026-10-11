#include "core.hpp"

#include <algorithm>
#include <condition_variable>
#include <future>
#include <mutex>
#include <stdexcept>

#include "../util/cache.hpp"
#include "../util/db.hpp"
#include "../util/http.hpp"
#include "../util/paths.hpp"
#include "../util/re.hpp"
#include "../util/strutil.hpp"
#include "mangasrc.hpp"
#include "multisrc.hpp"
#include "scrape.hpp"

namespace core {
const std::vector<double> SPEEDS = {0.75, 1.0, 1.25, 1.5, 2.0};
const std::string MANGA_SITE = "https://allmanga.to";
static const std::string MANGA_API = "https://api.allanime.day/api";
static const std::string UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:129.0) Gecko/20100101 Firefox/129.0";
static const std::string MANGA_QUERY =
    "query($search:SearchInput,$limit:Int,$page:Int,$countryOrigin:VaildCountryOriginEnumType)"
    "{mangas(search:$search,limit:$limit,page:$page,countryOrigin:$countryOrigin)"
    "{edges{_id name englishName availableChapters}}}";

// ---------------------------------------------------------------- small json helpers
static std::string S(const json& j, const char* k) {
    if (!j.is_object() || !j.contains(k) || j[k].is_null()) return "";
    if (j[k].is_string()) return j[k].get<std::string>();
    return j[k].dump();
}
static double N(const json& j, const char* k, double dflt = 0) {
    if (!j.is_object() || !j.contains(k) || !j[k].is_number()) return dflt;
    return j[k].get<double>();
}
static const json& O(const json& j, const char* k) {
    static const json empty = json::object();
    if (!j.is_object() || !j.contains(k) || j[k].is_null()) return empty;
    return j[k];
}
static json get_json(const http::Req& r) {
    auto resp = http::request(r);
    if (resp.status >= 400) throw std::runtime_error("HTTP " + std::to_string(resp.status) + " from " + r.url);
    return json::parse(resp.body);
}

std::string norm(const std::string& t) {
    static const re::Regex R("[^a-z0-9]+");
    return su::strip(R.sub(" ", su::lower(t)));
}

std::string base_title(const std::string& title) {
    static const re::Regex A(R"(\((mini anime|tv|movie|ova|ona|special)[^)]*\))", re::I);
    static const re::Regex B(R"(\b(\d+(st|nd|rd|th)|final)\s+season\b)", re::I);
    static const re::Regex C(R"(\bseason\s*\d+\b|\bpart\s*\d+\b|\bcour\s*\d+\b)", re::I);
    static const re::Regex W(R"(\s+)");
    std::string t = A.sub("", title);
    t = B.sub("", t);
    t = C.sub("", t);
    t = su::strip(W.sub(" ", t), " :-");
    return t.empty() ? title : t;
}

// ---------------------------------------------------------------- watch history (same sqlite db the ani script logs to)
json wl_list() {
    json out = json::array();
    try {
        db::Conn con;
        for (auto& r : con.query("SELECT anime_id, anime_name, poster, added FROM watchlist ORDER BY added DESC, rowid DESC"))
            out.push_back({{"id", r.str(0)}, {"title", r.str(1)}, {"poster", r.str(2)}, {"added", r.str(3)}});
    } catch (...) {
    }
    return out;
}
void wl_add(const std::string& id, const std::string& title, const std::string& poster) {
    try {
        db::Conn con;
        con.exec("INSERT OR REPLACE INTO watchlist (anime_id, anime_name, poster) VALUES (?,?,?)", {id, title, poster});
    } catch (...) {
    }
}
void wl_remove(const std::string& id) {
    try {
        db::Conn con;
        con.exec("DELETE FROM watchlist WHERE anime_id=?", {id});
    } catch (...) {
    }
}
std::vector<std::string> known_titles(int limit) {
    std::vector<std::string> out;
    try {
        db::Conn con;
        for (auto& r : con.query("SELECT anime_name FROM history GROUP BY anime_name ORDER BY MAX(timestamp) DESC LIMIT ?", {(long long)limit}))
            out.push_back(r.str(0));
        for (auto& r : con.query("SELECT anime_name FROM watchlist ORDER BY added DESC LIMIT ?", {(long long)limit})) out.push_back(r.str(0));
    } catch (...) {
    }
    std::vector<std::string> uniq;
    std::set<std::string> seen;
    for (auto& t : out)
        if (!t.empty() && seen.insert(t).second) uniq.push_back(t);
    return uniq;
}
void total_set(const std::string& id, int total) {
    if (id.empty() || !total) return;
    try {
        db::Conn con;
        con.exec("INSERT OR REPLACE INTO anime_total (anime_id, total) VALUES (?,?)", {id, (long long)total});
    } catch (...) {
    }
}
std::set<std::string> db_watched(const std::string& id, const std::string& title) {
    std::set<std::string> out;
    try {
        db::Conn con;
        for (auto& r : con.query("SELECT DISTINCT episode FROM history WHERE anime_id=? OR (anime_id IS NULL AND anime_name=?)", {id, title}))
            out.insert(r.str(0));
    } catch (...) {
    }
    return out;
}
void db_log(const std::string& id, const std::string& title, const std::string& ep) {
    try {
        db::Conn con;
        con.exec("INSERT INTO history (anime_name, episode, anime_id) VALUES (?,?,?)", {title, ep, id});
    } catch (...) {
    }
}

// ---------------------------------------------------------------- scraping backend
// Search, episodes and links across hianime, miruro.to and AnimeHeaven (see multisrc): returns the text the old
// `ani --api` shell mode printed.
static std::string scrape_sync(const std::vector<std::string>& args) {
    const std::string& cmd = args[0];
    if (cmd == "search") {
        std::string out;
        for (auto& r : multisrc::search(su::replace(args[1], "+", " "))) {
            if (!out.empty()) out += "\n";
            out += r.id + "\t" + r.title + "\t" + r.poster;
        }
        return out;
    }
    if (cmd == "episodes") return su::join(multisrc::episodes(args[1], args.size() > 2 ? args[2] : ""), "\n");
    if (cmd == "link") {
        std::string mode = args.size() > 3 ? args[3] : "sub";
        std::string title = args.size() > 4 ? args[4] : "";
        auto st = multisrc::link(args[1], args[2], mode, title);
        json skip = json::object();
        for (auto& [k, v] : st.skip) skip[k] = {v.start, v.end};
        return st.url + "\n" + st.referer + "\n" + st.sub + "\n" + skip.dump() + "\n" + st.source + "\n";
    }
    throw std::invalid_argument("unknown backend command: " + cmd);
}

std::string backend(const std::vector<std::string>& args) {
    // search results change rarely, episode lists grow when new episodes air, stream links expire so are never cached
    double ttl = args[0] == "search" ? 3600 : args[0] == "episodes" ? 600 : 0;
    if (ttl > 0) {
        std::string key = "ani:" + su::join(args, std::string(1, '\0'));
        return cache::cached(key, ttl, [&] { return json(scrape_sync(args)); }).get<std::string>();
    }
    return scrape_sync(args);
}

// ---------------------------------------------------------------- posters + manga lookup
std::string fetch_poster(const std::string& url) {
    std::string dir = paths::join(paths::cache_dir(), "posters");
    paths::makedirs(dir);
    std::string path = paths::join(dir, su::sha1_hex(url) + ".jpg");
    if (!paths::exists(path)) {
        http::Req r;
        r.url = url;
        r.headers = {{"User-Agent", UA}};
        auto resp = http::request(r);
        if (resp.status >= 400) throw std::runtime_error("HTTP " + std::to_string(resp.status));
        paths::write_file(path + ".part", resp.body);
        std::rename((path + ".part").c_str(), path.c_str());
    }
    return path;
}

static json find_manga_fetch(const std::string& title) {
    std::string base = base_title(title);
    json body = {{"query", MANGA_QUERY}, {"variables", {{"search", {{"query", base}}}, {"limit", 10}, {"page", 1}, {"countryOrigin", "ALL"}}}};
    http::Req r;
    r.url = MANGA_API;
    r.method = "POST";
    r.body = body.dump();
    r.headers = {{"User-Agent", UA}, {"Referer", MANGA_SITE}, {"Content-Type", "application/json"}};
    json d = get_json(r);
    std::set<std::string> wanted = {norm(base), norm(title)};
    struct E {
        int score;
        long long negn;
        std::string id, name;
        long long n;
    };
    std::vector<E> out;
    for (auto& e : d["data"]["mangas"]["edges"]) {
        const json& ch = O(e, "availableChapters");
        long long n = (long long)std::max(N(ch, "sub"), N(ch, "raw"));
        if (!n) continue;
        std::vector<std::string> names = {norm(S(e, "englishName")), norm(S(e, "name"))};
        int score = 2;
        for (auto& x : names)
            if (wanted.count(x)) score = 0;
        if (score) {
            for (auto& x : names)
                if (!x.empty() && su::starts_with(x, norm(base))) score = 1;
        }
        std::string nm = S(e, "englishName").empty() ? S(e, "name") : S(e, "englishName");
        out.push_back({score, -n, S(e, "_id"), nm, n});
    }
    std::sort(out.begin(), out.end(), [](const E& a, const E& b) { return std::tie(a.score, a.negn, a.id, a.name) < std::tie(b.score, b.negn, b.id, b.name); });
    json res = json::array();
    for (size_t i = 0; i < out.size() && i < 6; i++) res.push_back({out[i].id, out[i].name, out[i].n});
    return res;
}

json find_manga(const std::string& title) {
    return cache::cached("manga:" + title, 86400, [&] { return find_manga_fetch(title); });
}

// ---------------------------------------------------------------- related content: light novels, games, fan sites
static http::Req rq(const std::string& url, const http::KV& params = {}, double timeout = 12) {
    http::Req r;
    r.url = url;
    r.params = params;
    r.timeout = timeout;
    r.headers = {{"User-Agent", UA}};
    return r;
}

static json novels(const std::string& title, const std::string& base) {
    json out = json::array();
    try {
        json d = get_json(rq("https://kitsu.io/api/edge/manga", {{"filter[text]", base}, {"filter[subtype]", "novel"}, {"page[limit]", "8"}}));
        std::vector<std::string> words;
        for (auto& w : su::split_ws(norm(base)))
            if (w.size() > 2 && words.size() < 3) words.push_back(w);
        for (auto& e : O(d, "data")) {
            const json& a = e["attributes"];
            std::string titles = norm(a["titles"].dump());
            bool ok = true;
            for (auto& w : words) ok = ok && su::contains(titles, w);
            if (!words.empty() && !ok) continue;
            std::string name = S(a["titles"], "en");
            if (name.empty()) name = S(a["titles"], "en_jp");
            if (name.empty()) name = S(a, "canonicalTitle");
            std::string vols = a.contains("volumeCount") && a["volumeCount"].is_number() ? std::to_string(a["volumeCount"].get<int>()) : "?";
            out.push_back({{"title", name}, {"url", "https://kitsu.io/manga/" + S(a, "slug")}, {"note", "Light novel \xC2\xB7 " + vols + " vol \xC2\xB7 Kitsu"}});
        }
    } catch (...) {
    }
    try {  // AniList is often rate limited, so it's only a bonus
        std::string q = "query($s:String){Page(perPage:8){media(search:$s,type:MANGA,format:NOVEL){title{romaji english} siteUrl volumes}}}";
        http::Req r = rq("https://graphql.anilist.co");
        r.method = "POST";
        r.body = json({{"query", q}, {"variables", {{"s", base}}}}).dump();
        r.headers.emplace_back("Content-Type", "application/json");
        json d = json::parse(http::request(r).body);
        for (auto& m : d["data"]["Page"]["media"]) {
            std::string name = S(m["title"], "english");
            if (name.empty()) name = S(m["title"], "romaji");
            std::string vols = m.contains("volumes") && m["volumes"].is_number() ? std::to_string(m["volumes"].get<int>()) : "?";
            out.push_back({{"title", name}, {"url", S(m, "siteUrl")}, {"note", "Light novel \xC2\xB7 " + vols + " vol \xC2\xB7 AniList"}});
        }
    } catch (...) {
    }
    std::set<std::string> seen;
    json res = json::array();
    for (auto& n : out) {
        std::string k = norm(n["title"].get<std::string>());
        if (seen.insert(k).second) res.push_back(n);
        if (res.size() >= 8) break;
    }
    return res;
}

static json games(const std::string& title, const std::string& base) {
    static const re::Regex BAD(R"(soundtrack|outfit|costume|dlc|season pass|bonus|wallpaper|artbook|demo\b|\bpack\b|skin|bundle|\bset\b|unlock|upgrade|add-on|bonus)", re::I);
    std::vector<std::string> words;
    for (auto& w : su::split_ws(norm(base)))
        if (w.size() > 2 && words.size() < 3) words.push_back(w);
    json out = json::array();
    try {
        json d = get_json(rq("https://store.steampowered.com/api/storesearch/", {{"term", base}, {"cc", "us"}, {"l", "en"}}));
        for (auto& i : O(d, "items")) {
            std::string name = S(i, "name");
            if (S(i, "type") != "app" || BAD.search(name)) continue;
            bool ok = true;
            for (auto& w : words) ok = ok && su::contains(norm(name), w);
            if (!words.empty() && !ok) continue;
            std::string note = "Free / n.a.";
            if (i.contains("price") && i["price"].is_object() && i["price"].contains("final"))
                note = su::fmt("$%.2f", i["price"]["final"].get<double>() / 100.0);
            out.push_back({{"title", name}, {"url", "https://store.steampowered.com/app/" + S(i, "id")}, {"note", "Steam \xC2\xB7 " + note}});
            if (out.size() >= 8) break;
        }
    } catch (...) {
    }
    return out;
}

static json fandom(const std::string& base) {
    auto n = su::split_ws(norm(base));
    std::set<std::string> slugs;
    slugs.insert(su::join(n, ""));
    slugs.insert(su::join(n, "-"));
    std::vector<std::string> nothe;
    for (auto& w : n)
        if (w != "the") nothe.push_back(w);
    slugs.insert(su::join(nothe, ""));
    json out = json::array();
    for (auto& slug : slugs) {
        if (slug.empty()) continue;
        try {
            auto r = http::request(rq("https://" + slug + ".fandom.com/api.php", {{"action", "query"}, {"meta", "siteinfo"}, {"format", "json"}}));
            if (r.status == 200 && su::contains(r.body, "sitename")) {
                json info = json::parse(r.body)["query"]["general"];
                std::string url = su::split(S(info, "base"), "/wiki/")[0] + "/";
                out.push_back({{"title", S(info, "sitename")}, {"url", url}, {"note", "Fan wiki \xC2\xB7 Fandom"}});
            }
        } catch (...) {
        }
    }
    return out;
}

static json wikipedia(const std::string& base) {
    try {
        json d = get_json(rq("https://en.wikipedia.org/w/api.php",
                             {{"action", "query"}, {"list", "search"}, {"srsearch", base + " anime"}, {"srlimit", "3"}, {"format", "json"}}));
        json out = json::array();
        for (auto& h : d["query"]["search"]) {
            std::string t = S(h, "title");
            out.push_back({{"title", t}, {"url", "https://en.wikipedia.org/wiki/" + su::url_quote(su::replace(t, " ", "_"))}, {"note", "Wikipedia"}});
        }
        return out;
    } catch (...) {
        return json::array();
    }
}

static json web(const std::string& base) {
    static const std::vector<std::string> SKIP = {"amazon.", "ebay.", "netflix.", "crunchyroll.", "youtube.", "youtu.be", "facebook.", "pinterest.",
                                                  "imdb.", "myanimelist.", "anilist.", "kitsu.", "hianime", "aniwatch", "9anime", "gogoanime"};
    static const re::Regex A(R"re(class="result__a" href="([^"]*)"[^>]*>(.*?)</a>)re", re::S);
    static const re::Regex U(R"(uddg=([^&]+))");
    static const re::Regex T(R"(<[^>]+>|&amp;|&#x27;)");
    json out = json::array();
    for (auto [suffix, tag] : {std::pair<std::string, std::string>{"anime fan site", "Fan site"}, {"anime wiki forum community", "Community"}}) {
        try {
            auto r = http::request(rq("https://html.duckduckgo.com/html/", {{"q", base + " " + suffix}}));
            for (auto& m : A.finditer(r.body)) {
                std::string href = m.group(1);
                auto u = U.search(href);
                std::string url = u ? su::url_unquote(u->group(1)) : href;
                bool skip = !su::starts_with(url, "http");
                for (auto& d : SKIP) skip = skip || su::contains(url, d);
                if (skip) continue;
                std::string name = su::strip(T.sub_fn([](const re::Match& x) {
                    const std::string& g = x.group(0);
                    if (g == "&amp;") return std::string("&");
                    if (su::starts_with(g, "&#")) return std::string("'");
                    return std::string();
                }, m.group(2)));
                out.push_back({{"title", name}, {"url", url}, {"note", tag}});
            }
        } catch (...) {
        }
    }
    return out;
}

static std::string netloc(const std::string& url) {
    size_t s = url.find("://");
    if (s == std::string::npos) return "";
    size_t e = url.find_first_of("/?#", s + 3);
    std::string host = url.substr(s + 3, e == std::string::npos ? std::string::npos : e - s - 3);
    if (su::starts_with(host, "www.")) host = host.substr(4);
    return host;
}

json related_content(const std::string& title) {
    std::string base = base_title(title);
    auto f1 = std::async(std::launch::async, novels, title, base);
    auto f2 = std::async(std::launch::async, games, title, base);
    auto f3 = std::async(std::launch::async, fandom, base);
    auto f4 = std::async(std::launch::async, wikipedia, base);
    auto f5 = std::async(std::launch::async, web, base);
    json nov = f1.get(), gam = f2.get(), fan = f3.get(), wik = f4.get(), wb = f5.get();
    json sites = json::array();
    std::set<std::string> seen;
    for (auto* group : {&fan, &wik, &wb})
        for (auto& s : *group) {
            std::string url = s["url"], note = s["note"], host = netloc(url);
            if (!seen.count(url) && !((note == "Fan site" || note == "Community") && seen.count(host))) {
                seen.insert(url);
                seen.insert(host);
                sites.push_back(s);
            }
        }
    json cut = json::array();
    for (size_t i = 0; i < sites.size() && i < 12; i++) cut.push_back(sites[i]);
    return {{"novels", nov}, {"games", gam}, {"sites", cut}};
}

// ---------------------------------------------------------------- manga (MangaDex public API)
static const std::string MD = "https://api.mangadex.org";
static const std::string MD_UA = "ani-3d/1.0 (personal anime library)";

// MangaDex allows ~5 requests a second; keep a few in flight at most
class Gate {
public:
    void acquire() {
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait(lk, [&] { return n_ < 4; });
        n_++;
    }
    void release() {
        std::lock_guard<std::mutex> lk(mu_);
        n_--;
        cv_.notify_one();
    }

private:
    std::mutex mu_;
    std::condition_variable cv_;
    int n_ = 0;
};
static Gate g_gate;
struct GateHold {
    GateHold() { g_gate.acquire(); }
    ~GateHold() { g_gate.release(); }
};

static http::KV rating(bool all = false) {
    http::KV kv = {{"contentRating[]", "safe"}, {"contentRating[]", "suggestive"}};
    if (all) {
        kv.emplace_back("contentRating[]", "erotica");
        kv.emplace_back("contentRating[]", "pornographic");
    }
    return kv;
}

static json md_get(const std::string& path, http::KV params) {
    GateHold hold;
    http::Req r;
    r.url = MD + path;
    r.params = std::move(params);
    r.headers = {{"User-Agent", MD_UA}};
    r.timeout = 25;
    return get_json(r);
}

// (english, japanese) title of a MangaDex attributes block. 'Japanese' is the romaji form, which the bundled font can draw.
static std::pair<std::string, std::string> md_titles(const json& a) {
    const json& t = O(a, "title");
    std::map<std::string, std::string> alts;
    for (auto& d : O(a, "altTitles"))
        for (auto& [lang, name] : d.items())
            if (name.is_string()) alts.emplace(lang, name.get<std::string>());
    auto alt = [&](const std::string& k) { auto it = alts.find(k); return it == alts.end() ? std::string() : it->second; };
    std::string en = S(t, "en");
    if (en.empty()) en = alt("en");
    if (en.empty()) {
        en = "?";
        for (auto& [k, v] : t.items())
            if (v.is_string()) {
                en = v.get<std::string>();
                break;
            }
    }
    std::string jp = S(t, "ja-ro");
    if (jp.empty()) jp = alt("ja-ro");
    if (jp.empty()) jp = alt("ja");
    if (jp.empty()) jp = en;
    if (!jp.empty() && !su::is_ascii(jp)) {  // kanji / kana: the bundled mono font has no glyphs for them
        jp = alt("ja-ro");
        if (jp.empty()) jp = S(t, "ja-ro");
        if (jp.empty()) jp = en;
    }
    return {en, jp};
}

static json md_item(const json& m) {
    const json& a = m["attributes"];
    auto [en, jp] = md_titles(a);
    std::string cover;
    for (auto& x : O(m, "relationships"))
        if (S(x, "type") == "cover_art" && x.contains("attributes")) {
            cover = S(x["attributes"], "fileName");
            break;
        }
    std::string id = S(m, "id");
    return {{"id", id}, {"title", en}, {"title_en", en}, {"title_jp", jp},
            {"cover", cover.empty() ? "" : "https://uploads.mangadex.org/covers/" + id + "/" + cover + ".512.jpg"}};
}

json md_search(const std::string& query, int n) {
    return cache::cached("md:search:" + norm(query), 1800, [&] {
        http::KV p = {{"title", query}, {"limit", std::to_string(n)}, {"hasAvailableChapters", "true"}, {"availableTranslatedLanguage[]", "en"}};
        for (auto& kv : rating()) p.push_back(kv);
        p.emplace_back("includes[]", "cover_art");
        p.emplace_back("order[relevance]", "desc");
        json d = md_get("/manga", p);
        std::vector<json> items;
        for (auto& m : d["data"]) items.push_back(md_item(m));
        std::string want = norm(query);
        auto rank = [&](const json& it) {
            std::string a = norm(it["title_en"].get<std::string>()), b = norm(it["title_jp"].get<std::string>());
            int r0 = (want == a || want == b) ? 0 : (su::starts_with(a, want) || su::starts_with(b, want)) ? 1 : 2;
            int r1 = su::contains(su::lower(it["title_en"].get<std::string>()), "doujinshi") ? 1 : 0;
            return std::make_pair(r0, r1);
        };
        std::stable_sort(items.begin(), items.end(), [&](const json& x, const json& y) { return rank(x) < rank(y); });
        return json(items);
    });
}

json md_alt_titles(const std::vector<std::string>& ids_in) {
    json out = json::object();
    std::vector<std::string> ids;
    std::set<std::string> seen;
    for (auto& i : ids_in)
        if (seen.insert(i).second) ids.push_back(i);
    for (size_t i = 0; i < ids.size(); i += 50) {
        http::KV p;
        for (size_t k = i; k < ids.size() && k < i + 50; k++) p.emplace_back("ids[]", ids[k]);
        p.emplace_back("limit", "50");
        for (auto& kv : rating(true)) p.push_back(kv);
        json d = md_get("/manga", p);
        for (auto& m : d["data"]) {
            auto [en, jp] = md_titles(m["attributes"]);
            out[S(m, "id")] = {en, jp};
        }
    }
    return out;
}

std::string mangafire_url(const std::string& title) { return "https://mangafire.to/filter?keyword=" + su::url_quote_plus(base_title(title)); }

json md_chapters(const std::string& manga_id) {
    return cache::cached("md:chapters:" + manga_id, 1800, [&] {
        std::vector<json> out;
        int offset = 0;
        while (offset < 1500) {
            http::KV p = {{"translatedLanguage[]", "en"}, {"includeExternalUrl", "0"}, {"limit", "500"}, {"offset", std::to_string(offset)}, {"order[chapter]", "asc"}};
            for (auto& kv : rating()) p.push_back(kv);
            json d = md_get("/manga/" + manga_id + "/feed", p);
            for (auto& ch : d["data"]) {
                const json& at = ch["attributes"];
                if (N(at, "pages") > 0 && S(at, "externalUrl").empty() && !S(at, "chapter").empty())
                    out.push_back({{"id", S(ch, "id")}, {"chapter", S(at, "chapter")}, {"pages", (int)N(at, "pages")}});
            }
            offset += 500;
            if (offset >= N(d, "total")) break;
        }
        std::vector<json> uniq;
        std::set<std::string> seen;
        for (auto& c : out)  // several scanlation groups can host the same chapter: keep the first
            if (seen.insert(c["chapter"].get<std::string>()).second) uniq.push_back(c);
        auto num = [](const json& c) {
            double f;
            return su::to_double(c["chapter"].get<std::string>(), f) ? f : 1e9;
        };
        std::stable_sort(uniq.begin(), uniq.end(), [&](const json& a, const json& b) { return num(a) < num(b); });
        return json(uniq);
    });
}

int md_probe(const std::string& manga_id) {
    return cache::cached("md:probe:" + manga_id, 3600, [&] {
               http::KV p = {{"translatedLanguage[]", "en"}, {"includeExternalUrl", "0"}, {"limit", "1"}};
               for (auto& kv : rating()) p.push_back(kv);
               json d = md_get("/manga/" + manga_id + "/feed", p);
               return json((int)N(d, "total"));
           })
        .get<int>();
}

json md_popular(int n) {
    return cache::cached("md:popular:" + std::to_string(n), 3600, [&] {
        http::KV p = {{"limit", std::to_string(n + 12)}, {"order[followedCount]", "desc"}, {"hasAvailableChapters", "true"}, {"availableTranslatedLanguage[]", "en"}};
        for (auto& kv : rating()) p.push_back(kv);
        p.emplace_back("includes[]", "cover_art");
        json d = md_get("/manga", p);
        json out = json::array();
        for (auto& m : d["data"]) out.push_back(md_item(m));
        return out;
    });
}

json md_for_title(const std::string& title) {
    std::string base = base_title(title);
    return cache::cached("md:title:" + base, 86400, [&] {
        http::KV p = {{"title", base}, {"limit", "6"}, {"hasAvailableChapters", "true"}, {"availableTranslatedLanguage[]", "en"}};
        for (auto& kv : rating()) p.push_back(kv);
        p.emplace_back("includes[]", "cover_art");
        p.emplace_back("order[relevance]", "desc");
        json d = md_get("/manga", p);
        std::string want = norm(base);
        for (auto& m : d["data"]) {
            json it = md_item(m);
            std::string t = norm(it["title"].get<std::string>());
            if (t == want || su::starts_with(t, want)) return it;
        }
        return json(nullptr);
    });
}

json md_pages(const std::string& chapter_id) {
    return cache::cached("md:pages:" + chapter_id, 600, [&] {
        json d = md_get("/at-home/server/" + chapter_id, {});
        json out = json::array();
        const json& ch = d["chapter"];
        for (auto& f : ch["dataSaver"]) out.push_back(S(d, "baseUrl") + "/data-saver/" + S(ch, "hash") + "/" + f.get<std::string>());
        return out;
    });
}

std::string fetch_manga_image(const std::string& url) {
    std::string path = paths::join(paths::join(paths::cache_dir(), "manga"), su::sha1_hex(url) + ".img");
    if (!paths::exists(path)) {
        paths::makedirs(paths::dirname(path));
        std::string ref = mangasrc::referer(url);
        http::Req r;
        r.url = url;
        r.timeout = 30;
        r.headers = {{"User-Agent", ref.empty() ? MD_UA : mangasrc::UA}};
        if (!ref.empty()) r.headers.emplace_back("Referer", ref);
        http::Resp resp;
        {
            GateHold hold;
            resp = http::request(r);
        }
        if (resp.status >= 400) throw std::runtime_error("HTTP " + std::to_string(resp.status) + " for a manga page");
        paths::write_file(path + ".part", resp.body);
        std::rename((path + ".part").c_str(), path.c_str());
    }
    return path;
}

// ---------------------------------------------------------------- fuller sources when MangaDex only hosts part of a series
static bool same_title(const std::string& a_, const std::string& b_) {
    std::string a = norm(base_title(a_)), b = norm(base_title(b_));
    if (a.empty()) return false;
    if (a == b) return true;
    size_t mn = std::min(a.size(), b.size()), mxl = std::max(a.size(), b.size());
    return mn >= 6 && (su::contains(b, a) || su::contains(a, b)) && (double)mn / (double)mxl >= 0.75;
}

static json extra_chapters(const std::string& book_id) {
    return cache::cached("xch:" + book_id, 1800, [&] {
        json out = json::array();
        for (auto& c : mangasrc::chapters(book_id)) out.push_back({{"id", c.id}, {"chapter", c.chapter}, {"src", c.src}});
        return out;
    });
}

static json extra_search(const std::string& query) {
    return cache::cached("xs2:" + norm(query), 3600, [&] {
        json out = json::array();
        for (auto& it : mangasrc::search(query)) out.push_back({{"id", it.id}, {"title", it.title}, {"cover", it.cover}, {"src", it.src}});
        return out;
    });
}

json full_chapters(json& book) {
    std::string src = S(book, "src");
    if (src.empty()) src = "md";
    if (src != "md") return {{"chapters", extra_chapters(S(book, "id"))}, {"label", mangasrc::LABELS.at(src)}};
    json md = json::array();
    for (auto c : md_chapters(S(book, "id"))) {
        c["src"] = "md";
        md.push_back(c);
    }
    try {
        std::string title = S(book, "title_en").empty() ? S(book, "title") : S(book, "title_en");
        size_t best_n = 0;
        json best_it, best_chs;
        int taken = 0;
        for (auto& it : extra_search(title)) {
            if (!same_title(title, S(it, "title"))) continue;
            if (taken++ >= 5) break;
            json chs;
            try {
                chs = extra_chapters(S(it, "id"));
            } catch (...) {
                continue;
            }
            if (chs.size() > best_n) {
                best_n = chs.size();
                best_it = it;
                best_chs = chs;
            }
        }
        if (best_n && best_n > md.size() * 1.25 && best_n >= md.size() + 3) {
            book["src"] = best_it["src"];
            book["alt_id"] = best_it["id"];
            return {{"chapters", best_chs}, {"label", mangasrc::LABELS.at(S(best_it, "src"))}};
        }
    } catch (...) {
    }
    return {{"chapters", md}, {"label", "MangaDex"}};
}

int probe_any(const std::string& book_id) {
    if (book_id.find(':') != std::string::npos) return (int)extra_chapters(book_id).size();
    return md_probe(book_id);
}

json chapter_pages(const json& ch) {
    std::string src = S(ch, "src");
    if (src.empty() || src == "md") return md_pages(S(ch, "id"));
    return cache::cached("xpg:" + S(ch, "id"), 600, [&] {
        json out = json::array();
        for (auto& u : mangasrc::pages({S(ch, "id"), S(ch, "chapter"), src})) out.push_back(u);
        return out;
    });
}

json search_all(const std::string& query, int n) {
    json md_items = md_search(query, n);
    std::vector<std::future<int>> probes;
    for (size_t i = 0; i < md_items.size() && i < 20; i++) probes.push_back(std::async(std::launch::async, md_probe, S(md_items[i], "id")));
    std::vector<std::pair<std::string, json>> found;  // normalised title -> entry (insertion order kept)
    auto offer = [&](json it, int total) {
        std::string key = norm(base_title(S(it, "title")));
        auto cur = std::find_if(found.begin(), found.end(), [&](auto& p) { return p.first == key; });
        if (total && (cur == found.end() || total > cur->second["total"].get<int>() * 1.25 + 2)) {
            it["total"] = total;
            if (cur == found.end()) found.emplace_back(key, it);
            else cur->second = it;
        }
    };
    for (size_t i = 0; i < probes.size(); i++) {
        try {
            int t = probes[i].get();
            if (t) {
                json it = md_items[i];
                it["src"] = "md";
                offer(it, t);
            }
        } catch (...) {
        }
    }
    json extra = extra_search(query);
    std::string want = norm(query);
    // the extra sites list loosely related series too: keep the close matches and count their chapters
    std::vector<json> close;
    for (auto& it : extra) {
        if (close.size() >= 14) break;
        if (su::contains(norm(S(it, "title")), want) || same_title(query, S(it, "title"))) close.push_back(it);
    }
    std::vector<std::future<json>> counts;
    for (auto& it : close) counts.push_back(std::async(std::launch::async, extra_chapters, S(it, "id")));
    for (size_t i = 0; i < close.size(); i++) {
        try {
            json chs = counts[i].get();
            if (!chs.empty()) {
                const json& it = close[i];
                offer({{"id", it["id"]}, {"title", it["title"]}, {"title_en", it["title"]}, {"title_jp", it["title"]}, {"cover", it["cover"]}, {"src", it["src"]}},
                      (int)chs.size());
            }
        } catch (...) {
        }
    }
    json out = json::array();
    for (auto& [k, v] : found) out.push_back(v);
    return out;
}

void mr_add(const std::string& id, const std::string& title, const std::string& cover, const std::string& chapter, const std::string& title_jp) {
    try {
        db::Conn con;
        con.exec("INSERT OR REPLACE INTO manga_read (manga_id, chapter, title, cover, title_jp) VALUES (?,?,?,?,?)", {id, chapter, title, cover, title_jp});
        con.exec("DELETE FROM shelf_hidden WHERE kind='manga' AND item_id=? AND COALESCE(purged, 0)=1", {id});
        con.exec("DELETE FROM manga_plan WHERE manga_id=?", {id});  // started it: no longer just "going to read"
    } catch (...) {
    }
}

json mr_list() {
    json out = json::array();
    try {
        db::Conn con;
        for (auto& r : con.query("SELECT manga_id, title, cover, group_concat(chapter, char(31)), max(ts), max(title_jp) FROM manga_read GROUP BY manga_id "
                                 "ORDER BY max(ts) DESC")) {
            json chs = json::array();
            for (auto& c : su::split(r.str(3), "\x1f")) chs.push_back(c);
            out.push_back({{"id", r.str(0)}, {"title", r.str(1)}, {"cover", r.str(2)}, {"chapters", chs}, {"last", r.str(4)}, {"title_jp", r.str(5)}});
        }
    } catch (...) {
    }
    return out;
}

json mp_list() {
    json out = json::array();
    try {
        db::Conn con;
        for (auto& r : con.query("SELECT manga_id, title, cover, title_jp FROM manga_plan ORDER BY ts DESC"))
            out.push_back({{"id", r.str(0)}, {"title", r.str(1)}, {"cover", r.str(2)}, {"title_jp", r.str(3)}});
    } catch (...) {
    }
    return out;
}

void mp_set(const std::string& id, bool on, const std::string& title, const std::string& cover, const std::string& title_jp) {
    try {
        db::Conn con;
        if (on) con.exec("INSERT OR REPLACE INTO manga_plan (manga_id, title, cover, title_jp) VALUES (?,?,?,?)", {id, title, cover, title_jp});
        else con.exec("DELETE FROM manga_plan WHERE manga_id=?", {id});
    } catch (...) {
    }
}

// ---------------------------------------------------------------- library shelf placement
std::map<std::string, std::pair<std::string, long long>> shelf_overrides(const std::string& kind) {
    std::map<std::string, std::pair<std::string, long long>> out;
    try {
        db::Conn con;
        for (auto& r : con.query("SELECT item_id, section, slot FROM shelf_override WHERE kind=?", {kind})) out[r.str(0)] = {r.str(1), r.i64(2)};
    } catch (...) {
    }
    return out;
}
void shelf_set(const std::string& kind, const std::string& id, const std::string& section, long long slot) {
    try {
        db::Conn con;
        con.exec("INSERT OR REPLACE INTO shelf_override (kind, item_id, section, slot) VALUES (?,?,?,?)", {kind, id, section, slot});
    } catch (...) {
    }
}
std::set<std::string> hidden_ids(const std::string& kind) {
    std::set<std::string> out;
    try {
        db::Conn con;
        for (auto& r : con.query("SELECT item_id FROM shelf_hidden WHERE kind=?", {kind})) out.insert(r.str(0));
    } catch (...) {
    }
    return out;
}
std::vector<std::pair<std::string, std::string>> hidden_list(const std::string& kind) {
    std::vector<std::pair<std::string, std::string>> out;
    try {
        db::Conn con;
        for (auto& r : con.query("SELECT item_id, title FROM shelf_hidden WHERE kind=? AND COALESCE(purged, 0)=0 ORDER BY rowid DESC", {kind}))
            out.emplace_back(r.str(0), r.str(1).empty() ? "(removed manga)" : r.str(1));
    } catch (...) {
    }
    return out;
}
void shelf_hide(const std::string& kind, const std::string& id, bool hide, const std::string& title) {
    try {
        db::Conn con;
        if (hide) con.exec("INSERT OR REPLACE INTO shelf_hidden (kind, item_id, title, purged) VALUES (?,?,?,0)", {kind, id, title});
        else con.exec("DELETE FROM shelf_hidden WHERE kind=? AND item_id=?", {kind, id});
    } catch (...) {
    }
}
void shelf_unhide_all(const std::string& kind) {
    try {
        db::Conn con;
        con.exec("DELETE FROM shelf_hidden WHERE kind=? AND COALESCE(purged, 0)=0", {kind});
    } catch (...) {
    }
}
void shelf_unpurge(const std::string& kind, const std::vector<std::string>& ids) {
    try {
        db::Conn con;
        for (auto& i : ids) con.exec("DELETE FROM shelf_hidden WHERE kind=? AND item_id=? AND COALESCE(purged, 0)=1", {kind, i});
    } catch (...) {
    }
}
int shelf_purge_removed(const std::string& kind) {
    try {
        db::Conn con;
        std::vector<std::string> ids;
        for (auto& r : con.query("SELECT item_id FROM shelf_hidden WHERE kind=? AND COALESCE(purged, 0)=0", {kind})) ids.push_back(r.str(0));
        for (auto& iid : ids) {
            if (kind == "manga") con.exec("DELETE FROM manga_read WHERE manga_id=?", {iid});
            con.exec("DELETE FROM shelf_override WHERE kind=? AND item_id=?", {kind, iid});
        }
        con.exec("UPDATE shelf_hidden SET purged=1 WHERE kind=?", {kind});
        return (int)ids.size();
    } catch (...) {
        return 0;
    }
}
void shelf_clear(const std::string& kind, const std::string& id) {
    try {
        db::Conn con;
        con.exec("DELETE FROM shelf_override WHERE kind=? AND item_id=?", {kind, id});
    } catch (...) {
    }
}

// ---------------------------------------------------------------- fan edits (AMVs) from YouTube search results
static void walk_videos(const json& o, std::vector<const json*>& out) {
    if (o.is_object()) {
        if (o.contains("videoRenderer")) out.push_back(&o["videoRenderer"]);
        for (auto& [k, v] : o.items()) walk_videos(v, out);
    } else if (o.is_array()) {
        for (auto& v : o) walk_videos(v, out);
    }
}

json fan_edits(const std::string& title, int n) {
    std::string base = base_title(title);
    return cache::cached("edits:" + norm(base), 21600, [&] {
        json out = json::array();
        std::set<std::string> seen;
        static const re::Regex Y(R"(var ytInitialData\s*=\s*(\{.*?\});</script>)", re::S);
        static const re::Regex BAD(R"([^\x{20}-\x{7e}\x{a0}-\x{24f}]+)");
        static const re::Regex WS(R"(\s+)");
        for (const char* suffix : {"AMV", "fan edit"}) {
            json data;
            try {
                http::Req r = rq("https://www.youtube.com/results", {{"search_query", base + " " + suffix}, {"sp", "EgIQAQ=="}}, 15);
                r.headers.emplace_back("Accept-Language", "en-US,en;q=0.9");
                auto resp = http::request(r);
                auto m = Y.search(resp.body);
                if (!m) continue;
                data = json::parse(m->group(1));
            } catch (...) {
                continue;
            }
            std::vector<const json*> vids;
            walk_videos(data, vids);
            for (auto* vp : vids) {
                const json& v = *vp;
                std::string vid = S(v, "videoId");
                if (vid.empty() || seen.count(vid)) continue;
                seen.insert(vid);
                std::string t;
                for (auto& x : O(O(v, "title"), "runs")) t += S(x, "text");
                t = su::strip(WS.sub(" ", BAD.sub(" ", t)));  // the bundled font has no CJK glyphs
                std::string ch;
                for (auto& x : O(O(v, "ownerText"), "runs")) ch += S(x, "text");
                std::string ln = S(O(v, "lengthText"), "simpleText");
                out.push_back({{"title", t}, {"channel", ch}, {"length", ln}, {"url", "https://www.youtube.com/watch?v=" + vid}});
            }
        }
        json cut = json::array();
        for (size_t i = 0; i < out.size() && (int)i < n; i++) cut.push_back(out[i]);
        return cut;
    });
}

// ---------------------------------------------------------------- home: continue watching, recommendations, popular
void resume_save(const std::string& id, const std::string& title, const std::string& ep, double pos, double dur, const std::string& poster) {
    if (id.empty()) return;
    try {
        db::Conn con;
        auto old = con.query("SELECT poster FROM resume WHERE anime_id=?", {id});
        con.exec("INSERT OR REPLACE INTO resume (anime_id, title, episode, position, duration, poster) VALUES (?,?,?,?,?,?)",
                 {id, title, ep, pos, dur, !poster.empty() ? poster : (old.empty() ? std::string() : old[0].str(0))});
    } catch (...) {
    }
}

void resume_advance(const std::string& id, const std::string& title, const std::string& next_ep) {
    try {
        db::Conn con;
        if (!next_ep.empty()) {
            long long before = con.total_changes();
            con.exec("UPDATE resume SET episode=?, position=0, duration=0, ts=CURRENT_TIMESTAMP WHERE anime_id=?", {next_ep, id});
            if (con.total_changes() == before) con.exec("INSERT INTO resume (anime_id, title, episode) VALUES (?,?,?)", {id, title, next_ep});
        } else {
            con.exec("DELETE FROM resume WHERE anime_id=?", {id});
            con.exec("INSERT OR REPLACE INTO resume (anime_id, title, episode, position, duration) VALUES (?,?,'done',0,0)", {id, title});
        }
    } catch (...) {
    }
}

json continue_list(int limit) {
    json out = json::array();
    std::set<std::string> seen;
    try {
        db::Conn con;
        for (auto& r : con.query("SELECT anime_id, title, episode, position, duration, poster FROM resume ORDER BY ts DESC LIMIT 40")) {
            seen.insert(r.str(0));
            if (r.str(2) != "done")
                out.push_back({{"id", r.str(0)}, {"title", r.str(1)}, {"poster", r.str(5)}, {"ep", r.str(2)}, {"pos", r.f(3)}, {"dur", r.f(4)}});
        }
        std::set<std::string> done;
        for (auto& r : con.query("SELECT anime_id FROM resume WHERE episode='done'")) done.insert(r.str(0));
        std::map<std::string, long long> totals;
        for (auto& r : con.query("SELECT anime_id, total FROM anime_total")) totals[r.str(0)] = r.i64(1);
        for (auto& r : con.query("SELECT h.anime_id, h.anime_name, h.episode, (SELECT COUNT(DISTINCT episode) FROM history WHERE anime_id=h.anime_id) "
                                 "FROM history h WHERE h.anime_id IS NOT NULL AND h.anime_id != '' AND h.id = "
                                 "(SELECT id FROM history WHERE anime_id=h.anime_id ORDER BY timestamp DESC, id DESC LIMIT 1) "
                                 "ORDER BY h.timestamp DESC LIMIT 40")) {
            std::string aid = r.str(0);
            auto tot = totals.find(aid);
            if (seen.count(aid) || done.count(aid) || (tot != totals.end() && tot->second && r.i64(3) >= tot->second)) continue;
            seen.insert(aid);
            out.push_back({{"id", aid}, {"title", r.str(1)}, {"poster", ""}, {"ep", r.str(2)}, {"pos", 0}, {"dur", 0}});
        }
    } catch (...) {
    }
    json cut = json::array();
    for (size_t i = 0; i < out.size() && (int)i < limit; i++) cut.push_back(out[i]);
    return cut;
}

static const std::string ANILIST = "https://graphql.anilist.co";
static const std::string AL_FIELDS = "id title{romaji english} coverImage{large} format averageScore";

static std::string al_title(const json& t) {
    std::string s = S(t, "english");
    if (s.empty()) s = S(t, "romaji");
    return su::strip(s);
}

static json anilist(const std::string& query, const json& variables = json::object()) {
    http::Req r;
    r.url = ANILIST;
    r.method = "POST";
    r.timeout = 20;
    r.headers = {{"User-Agent", "ani-3d"}, {"Content-Type", "application/json"}};
    r.body = json({{"query", query}, {"variables", variables}}).dump();
    return get_json(r)["data"];
}

// Intro / credits timestamps from the crowd-sourced AniSkip database (looked up by MAL id, found through AniList).
// Returns {"intro":[a,b],"outro":[a,b]} (either may be missing), or an empty object.
json aniskip(const std::string& title, const std::string& ep) {
    return cache::cached("aniskip:" + norm(title) + ":" + ep, 12 * 3600, [&]() -> json {
        json out = json::object();
        try {
            if (ep.empty() || ep.find_first_not_of("0123456789") != std::string::npos) return out;
            json d = anilist("query($s:String){Page(perPage:6){media(search:$s,type:ANIME,sort:SEARCH_MATCH){idMal format episodes "
                             "title{romaji english} synonyms}}}", {{"s", title}});
            std::string want = norm(title);
            const json* pick = nullptr;
            for (auto& m : d["Page"]["media"]) {
                if (m["idMal"].is_null()) continue;
                std::set<std::string> names = {norm(S(m["title"], "english")), norm(S(m["title"], "romaji"))};
                for (auto& x : O(m, "synonyms"))
                    if (x.is_string()) names.insert(norm(x.get<std::string>()));
                if (names.count(want)) {
                    pick = &m;
                    break;
                }
                if (!pick && (S(m, "format") == "TV" || S(m, "format") == "ONA")) pick = &m;
            }
            if (!pick) return out;
            http::Req r;
            r.url = "https://api.aniskip.com/v2/skip-times/" + std::to_string((long long)(*pick)["idMal"].get<double>()) + "/" + ep +
                    "?types=op&types=ed&episodeLength=0";
            r.timeout = 15;
            r.headers = {{"User-Agent", "ani-3d"}};
            json j = get_json(r);
            for (auto& it : O(j, "results")) {
                std::string t = S(it, "skipType");
                const json& iv = it["interval"];
                std::string k = t == "op" ? "intro" : t == "ed" ? "outro" : "";
                if (!k.empty() && !out.contains(k)) out[k] = {iv["startTime"].get<double>(), iv["endTime"].get<double>()};
            }
        } catch (...) {
        }
        return out;
    });
}

json anilist_top(int n) {
    return cache::cached("al:top:" + std::to_string(n), 6 * 3600, [&] {
        json d = anilist("query($n:Int){Page(perPage:$n){media(type:ANIME,sort:SCORE_DESC,format_in:[TV,MOVIE],popularity_greater:50000){" + AL_FIELDS + "}}}",
                         {{"n", n}});
        json out = json::array();
        for (auto& m : d["Page"]["media"]) {
            std::string t = al_title(m["title"]);
            if (!t.empty()) out.push_back({{"title", t}, {"poster", S(m["coverImage"], "large")}});
        }
        return out;
    });
}

json recommended(const std::vector<std::string>& seed_titles, const std::vector<std::string>& exclude, int n) {
    std::vector<std::string> seeds;
    std::set<std::string> seen;
    for (auto& t : seed_titles)
        if (seen.insert(t).second && seeds.size() < 6) seeds.push_back(t);
    if (seeds.empty()) return json::array();
    auto one = [](std::string t) {
        return cache::cached("al:rec:" + norm(t), 12 * 3600, [&] {
            json d = anilist("query($s:String){Media(search:$s,type:ANIME){recommendations(perPage:10,sort:RATING_DESC){nodes{rating mediaRecommendation{" +
                                 AL_FIELDS + "}}}}}",
                             {{"s", t}});
            json out = json::array();
            for (auto& x : O(O(O(d, "Media"), "recommendations"), "nodes")) {
                if (!x.contains("mediaRecommendation") || x["mediaRecommendation"].is_null()) continue;
                const json& mr = x["mediaRecommendation"];
                out.push_back({{"title", al_title(mr["title"])}, {"poster", S(O(mr, "coverImage"), "large")}, {"w", N(x, "rating")},
                               {"fmt", mr.contains("format") ? mr["format"] : json(nullptr)}});
            }
            return out;
        });
    };
    std::vector<std::future<json>> got;
    for (auto& t : seeds) got.push_back(std::async(std::launch::async, one, t));
    std::set<std::string> skip;
    std::vector<std::string> all = exclude;
    all.insert(all.end(), seeds.begin(), seeds.end());
    for (auto& t : all) {
        skip.insert(multisrc::akey(t));
        skip.insert(multisrc::akey(base_title(t)));
    }
    std::vector<std::pair<std::string, json>> score;
    for (auto& f : got) {
        json g;
        try {
            g = f.get();
        } catch (...) {
            continue;
        }
        int rank = 0;
        for (auto& it : g) {
            std::string k = multisrc::akey(S(it, "title"));
            std::string fmt = S(it, "fmt");
            bool fmt_ok = it["fmt"].is_null() || fmt == "TV" || fmt == "MOVIE" || fmt == "ONA";
            if (k.empty() || skip.count(k) || !fmt_ok) {
                rank++;
                continue;
            }
            auto cur = std::find_if(score.begin(), score.end(), [&](auto& p) { return p.first == k; });
            if (cur == score.end()) {
                score.emplace_back(k, json{{"title", it["title"]}, {"poster", it["poster"]}, {"s", 0.0}});
                cur = score.end() - 1;
            }
            cur->second["s"] = cur->second["s"].get<double>() + 10 - rank + std::min(N(it, "w"), 50.0) / 10;
            rank++;
        }
    }
    std::stable_sort(score.begin(), score.end(), [](const std::pair<std::string, json>& a, const std::pair<std::string, json>& b) { return a.second["s"].get<double>() > b.second["s"].get<double>(); });
    json out = json::array();
    for (size_t i = 0; i < score.size() && (int)i < n; i++) out.push_back({{"title", score[i].second["title"]}, {"poster", score[i].second["poster"]}});
    return out;
}

// Plain text from an AniList / MAL / MangaDex synopsis: no HTML, markdown, source credits or line breaks.
std::string clean_desc(const std::string& s_in, size_t cap) {
    static const re::Regex BR(R"(<br\s*/?>)", re::I);
    static const re::Regex TAG(R"(<[^>]+>)");
    static const re::Regex MDL(R"(\[([^\]]*)\]\([^)]*\))");
    static const re::Regex EMPH(R"([*_~]{1,3})");
    static const re::Regex SRC(R"(\(\s*source[^)]*\)|\[\s*written by[^\]]*\]|source:.*$)", re::I);
    static const re::Regex NONLATIN(R"([^\x{00}-\x{24F}\x{2010}-\x{2027}])");
    static const re::Regex WS(R"(\s+)");
    std::string s = BR.sub(" ", s_in);
    s = TAG.sub("", s);
    s = MDL.sub("\\1", s);
    s = EMPH.sub("", s);
    s = SRC.sub("", s);
    for (auto [a, b] : std::vector<std::pair<std::string, std::string>>{{"&quot;", "\""}, {"&amp;", "&"}, {"&#039;", "'"}, {"&mdash;", "-"}, {"&ndash;", "-"},
                                                                        {"\xE2\x80\x98", "'"}, {"\xE2\x80\x99", "'"}, {"\xE2\x80\x9C", "\""}, {"\xE2\x80\x9D", "\""},
                                                                        {"\xE2\x80\x94", "-"}, {"\xE2\x80\x93", "-"}, {"\xE2\x80\xA6", "..."}})
        s = su::replace(s, a, b);
    s = NONLATIN.sub("", s);  // the bundled mono font has no CJK / symbol glyphs
    s = su::strip(WS.sub(" ", s));
    if (su::utf8_len(s) > cap) {
        std::string cut = su::utf8_substr(s, 0, cap);
        size_t sp = cut.rfind(' ');
        if (sp != std::string::npos) cut = cut.substr(0, sp);
        s = su::rstrip(cut, ",;:-") + "...";
    }
    return s;
}

static json blurb_anilist(const std::string& t, const std::string& kind) {
    json d = anilist(
        "query($s:String,$k:MediaType){Page(perPage:8){media(search:$s,type:$k,sort:SEARCH_MATCH){title{romaji english} synonyms "
        "description(asHtml:false) genres averageScore popularity seasonYear startDate{year} status episodes chapters format "
        "coverImage{large}}}}",
        {{"s", t}, {"k", su::upper(kind)}});
    std::vector<json> ms;
    for (auto& m : d["Page"]["media"])
        if (!S(m, "description").empty()) ms.push_back(m);
    if (ms.empty()) return nullptr;
    std::string want = norm(t);
    auto names = [](const json& m) {
        std::set<std::string> s = {norm(S(m["title"], "english")), norm(S(m["title"], "romaji"))};
        for (auto& x : O(m, "synonyms"))
            if (x.is_string()) s.insert(norm(x.get<std::string>()));
        return s;
    };
    const json* pick = nullptr;
    for (auto& m : ms)
        if (names(m).count(want)) {
            pick = &m;
            break;
        }
    if (!pick) {
        static const std::set<std::string> MAIN = {"TV", "MOVIE", "ONA", "OVA", "MANGA", "ONE_SHOT", "NOVEL"};
        std::vector<const json*> main;
        for (size_t i = 0; i < ms.size() && i < 5; i++) {
            std::string f = S(ms[i], "format");
            if (f == "MOVIE" || (MAIN.count(f) && N(ms[i], "episodes") != 1)) main.push_back(&ms[i]);
        }
        if (main.empty()) main.push_back(&ms[0]);
        pick = main[0];
        for (auto* m : main)
            if (N(*m, "popularity") > N(*pick, "popularity")) pick = m;
    }
    const json& m = *pick;
    json genres = json::array();
    for (auto& g : O(m, "genres"))
        if (genres.size() < 4) genres.push_back(g);
    json year = m.contains("seasonYear") && !m["seasonYear"].is_null() ? m["seasonYear"] : O(m, "startDate").value("year", json(nullptr));
    std::string status = su::title_case(su::replace(S(m, "status"), "_", " "));
    json count = kind == "anime" ? m.value("episodes", json(nullptr)) : m.value("chapters", json(nullptr));
    return {{"title", al_title(m["title"])}, {"desc", clean_desc(S(m, "description"))}, {"genres", genres}, {"score", m.value("averageScore", json(nullptr))},
            {"year", year}, {"status", status}, {"count", count}, {"format", su::replace(S(m, "format"), "_", " ")}, {"cover", S(O(m, "coverImage"), "large")},
            {"src", "AniList"}};
}

static json blurb_jikan(const std::string& t, const std::string& kind) {
    http::Req r = rq("https://api.jikan.moe/v4/" + kind, {{"q", t}, {"limit", "1"}, {"sfw", "true"}}, 15);
    r.headers = {{"User-Agent", "ani-3d"}};
    json d = get_json(r);
    const json& rows = O(d, "data");
    if (!rows.is_array() || rows.empty() || S(rows[0], "synopsis").empty()) return nullptr;
    const json& m = rows[0];
    json year = m.value("year", json(nullptr));
    if (year.is_null()) {
        const json& aired = m.contains("aired") && !m["aired"].is_null() ? m["aired"] : O(m, "published");
        year = O(O(O(aired, "prop"), "from"), "year").is_null() ? json(nullptr) : O(O(aired, "prop"), "from").value("year", json(nullptr));
    }
    json genres = json::array();
    for (auto& g : O(m, "genres"))
        if (genres.size() < 4) genres.push_back(S(g, "name"));
    json score = m.contains("score") && m["score"].is_number() ? json((int)(m["score"].get<double>() * 10)) : json(nullptr);
    std::string title = S(m, "title_english");
    if (title.empty()) title = S(m, "title");
    if (title.empty()) title = t;
    json count = kind == "anime" ? m.value("episodes", json(nullptr)) : m.value("chapters", json(nullptr));
    return {{"title", title}, {"desc", clean_desc(S(m, "synopsis"))}, {"genres", genres}, {"score", score}, {"year", year}, {"status", S(m, "status")},
            {"count", count}, {"format", S(m, "type")}, {"cover", S(O(O(m, "images"), "jpg"), "large_image_url")}, {"src", "MyAnimeList"}};
}

static json blurb_mangadex(const std::string& t) {
    http::KV p = {{"title", t}, {"limit", "1"}};
    for (auto& kv : rating()) p.push_back(kv);
    p.emplace_back("includes[]", "cover_art");
    p.emplace_back("order[relevance]", "desc");
    json d = md_get("/manga", p);
    if (!d.contains("data") || d["data"].empty()) return nullptr;
    const json& m = d["data"][0];
    const json& a = m["attributes"];
    std::string desc = S(O(a, "description"), "en");
    if (desc.empty()) return nullptr;
    json genres = json::array();
    for (auto& x : O(a, "tags"))
        if (S(O(x, "attributes"), "group") == "genre") {
            std::string g = S(O(O(x, "attributes"), "name"), "en");
            if (!g.empty() && genres.size() < 4) genres.push_back(g);
        }
    return {{"title", md_titles(a).first}, {"desc", clean_desc(desc)}, {"genres", genres}, {"score", nullptr}, {"year", a.value("year", json(nullptr))},
            {"status", su::title_case(S(a, "status"))}, {"count", nullptr}, {"format", "Manga"}, {"cover", md_item(m)["cover"]}, {"src", "MangaDex"}};
}

json media_blurb(const std::string& title, const std::string& kind) {
    static const re::Regex WS(R"(\s+)");
    std::string t = su::strip(WS.sub(" ", title));
    std::string base = base_title(t);
    return cache::cached("blurb2:" + kind + ":" + norm(t), 3 * 86400, [&]() -> json {
        std::vector<std::function<json()>> tries = {[&] { return blurb_anilist(t, kind); }};
        if (base != t) tries.push_back([&] { return blurb_anilist(base, kind); });
        tries.push_back([&] { return blurb_jikan(base, kind); });
        if (kind == "manga") tries.push_back([&] { return blurb_mangadex(base); });
        std::string err;
        for (auto& go : tries) {
            try {
                json got = go();
                if (!got.is_null()) return got;
            } catch (const std::exception& e) {  // one source down is fine, try the next
                err = e.what();
            }
        }
        if (!err.empty()) throw std::runtime_error(err);  // nothing found AND something failed: don't cache, retry next hover
        return nullptr;
    });
}

json home_data() {
    json cont = continue_list();
    auto pop_f = std::async(std::launch::async, [] {
        return cache::cached("home:pop", 1800, [] {
            json out = json::array();
            for (auto& r : scrape::listing("most-popular")) out.push_back({r.id, r.title, r.poster});
            return out;
        });
    });
    auto air_f = std::async(std::launch::async, [] {
        return cache::cached("home:air", 1800, [] {
            json out = json::array();
            for (auto& r : scrape::listing("top-airing")) out.push_back({r.id, r.title, r.poster});
            return out;
        });
    });
    std::vector<std::string> watched;
    for (auto& c : cont) watched.push_back(S(c, "title"));
    std::vector<std::string> recent;
    try {
        db::Conn con;
        for (auto& r : con.query("SELECT anime_name FROM history GROUP BY anime_name ORDER BY max(timestamp) DESC LIMIT 8")) recent.push_back(r.str(0));
    } catch (...) {
    }
    std::vector<std::string> seeds;
    std::set<std::string> seen;
    for (auto* v : {&watched, &recent})
        for (auto& t : *v)
            if (seen.insert(t).second) seeds.push_back(t);
    auto rec_f = std::async(std::launch::async, [seeds] { return seeds.empty() ? anilist_top(18) : recommended(seeds, seeds); });
    auto ok = [](std::future<json>& f) {
        try {
            return f.get();
        } catch (...) {
            return json::array();
        }
    };
    json pop = ok(pop_f), air = ok(air_f), rec = ok(rec_f);
    json recommended_ = json::array(), popular = json::array(), airing = json::array();
    for (size_t i = 0; i < rec.size() && i < 12; i++) recommended_.push_back({{"id", nullptr}, {"title", rec[i]["title"]}, {"poster", rec[i]["poster"]}});
    for (size_t i = 0; i < pop.size() && i < 18; i++) popular.push_back({{"id", pop[i][0]}, {"title", pop[i][1]}, {"poster", pop[i][2]}});
    for (size_t i = 0; i < air.size() && i < 12; i++) airing.push_back({{"id", air[i][0]}, {"title", air[i][1]}, {"poster", air[i][2]}});
    return {{"continue", cont}, {"recommended", recommended_}, {"popular", popular}, {"airing", airing}, {"personal", !seeds.empty()}};
}
}  // namespace core
