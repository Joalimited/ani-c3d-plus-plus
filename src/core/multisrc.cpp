#include "multisrc.hpp"

#include <algorithm>
#include <future>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <tuple>

#include "../util/http.hpp"
#include "../util/re.hpp"
#include "../util/strutil.hpp"

namespace multisrc {
static const std::string AH = "https://animeheaven.me";
static const std::string AH_PREFIX = "ah:";
const int GOOD_HEIGHT = 720;  // below this a stream counts as bad quality and the alternates get a chance
static const std::map<std::string, std::string> LABELS = {{"", "HiAnime"}, {"mi", "Miruro"}, {"ah", "AnimeHeaven"}};
static const std::map<std::string, int> PREFERENCE = {{"", 0}, {"ah", 1}, {"mi", 2}};  // tie-break order

static std::mutex g_mu;
static std::map<std::string, std::vector<std::string>> ALTS;                 // primary id -> alternate ids
static std::map<std::string, int> COUNTS;                                    // id -> episode count
static std::map<std::pair<std::string, std::string>, std::string> SOURCE;    // (anime id, ep) -> site label
static std::map<std::string, std::map<std::string, std::string>> ah_eps;     // ah id -> {episode number: gate key}
static std::map<std::string, std::string> ah_title;                          // ah id -> title
static std::map<std::string, std::string> g_resolved;

std::string src_of(const std::string& anime_id) {
    auto c = anime_id.find(':');
    return c == std::string::npos ? "" : anime_id.substr(0, c);
}
std::string label(const std::string& anime_id) {
    auto it = LABELS.find(src_of(anime_id));
    return it == LABELS.end() ? "?" : it->second;
}
std::string source_label(const std::string& anime_id, const std::string& ep) {
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = SOURCE.find({anime_id, ep});
    return it == SOURCE.end() ? "" : it->second;
}

static http::Resp client_get(const std::string& url, const http::KV& params = {}, const http::KV& headers = {}, double timeout = 15,
                             size_t max_bytes = 0) {
    http::Req r;
    r.url = url;
    r.params = params;
    r.headers = headers;
    r.headers.emplace_back("User-Agent", scrape::UA);
    r.timeout = timeout;
    r.max_bytes = max_bytes;
    r.jar = http::Jar::SCRAPE;
    return http::request(r);
}

// ---------- AnimeHeaven (direct mp4 files, 1080p SubsPlease rips) ----------
std::vector<scrape::Row> ah_search(const std::string& query) {
    auto r = client_get(AH + "/search.php", {{"s", query}});
    if (r.status >= 400) throw std::runtime_error("AnimeHeaven HTTP " + std::to_string(r.status));
    std::vector<scrape::Row> out;
    static const re::Regex R(
        R"re(<img class='coverimg' src='([^']*)' alt='[^']*'[^>]*></a><div class='similarname c'><a href='anime\.php\?([a-z0-9]+)' class='c'>([^<]*)<)re");
    for (const auto& m : R.finditer(r.body)) {
        std::string title = su::strip(su::html_unescape(m.group(3)));
        {
            std::lock_guard<std::mutex> lk(g_mu);
            ah_title[m.group(2)] = title;
        }
        out.push_back({AH_PREFIX + m.group(2), title, scrape::urljoin(AH + "/", m.group(1))});
    }
    return out;
}

static std::map<std::string, std::string> ah_load(const std::string& raw) {
    {
        std::lock_guard<std::mutex> lk(g_mu);
        auto it = ah_eps.find(raw);
        if (it != ah_eps.end()) return it->second;
    }
    auto r = client_get(AH + "/anime.php?" + raw + "=", {}, {{"Referer", AH + "/"}});
    if (r.status >= 400) throw std::runtime_error("AnimeHeaven HTTP " + std::to_string(r.status));
    std::map<std::string, std::string> eps;
    static const re::Regex R(R"re(gatea\(\s*"([0-9a-f]{32})"\s*\).*?watch2 bc '\s*>\s*([0-9.]+)\s*<)re", re::S);
    for (const auto& m : R.finditer(r.body)) eps.emplace(m.group(2), m.group(1));
    std::lock_guard<std::mutex> lk(g_mu);
    ah_eps[raw] = eps;
    return eps;
}

std::vector<std::string> ah_episodes(const std::string& anime_id) {
    auto eps = ah_load(anime_id.substr(AH_PREFIX.size()));
    std::vector<std::string> out;
    for (auto& [k, v] : eps) out.push_back(k);
    std::stable_sort(out.begin(), out.end(), [](auto& a, auto& b) { return std::stod(a) < std::stod(b); });
    return out;
}

struct Got {
    std::string url, referer, sub;
    int height = 0;
};

// The mp4 is probed with a small range request, which also gives its quality: the file name in Content-Disposition carries it.
static Got ah_link(const std::string& anime_id, const std::string& ep) {
    std::string raw = anime_id.substr(AH_PREFIX.size());
    auto eps = ah_load(raw);
    auto it = eps.find(ep);
    if (it == eps.end()) throw std::runtime_error("AnimeHeaven has no episode " + ep);
    auto r = client_get(AH + "/gate.php", {}, {{"Referer", AH + "/anime.php?" + raw}, {"Cookie", "key=" + it->second}});
    static const re::Regex R(R"re(<source src='(https://[^']+?video\.mp4\?[^'&]+&[^'&]+)')re");
    auto srcs = re::findall1(R, r.body);
    if (srcs.empty()) throw std::runtime_error("AnimeHeaven gave no stream");
    static const re::Regex Q(R"(\((\d{3,4})p\))");
    for (const auto& url : srcs) {  // the three mirrors carry the same file
        try {
            auto pr = client_get(url, {}, {{"Referer", AH + "/"}, {"Range", "bytes=0-1023"}}, 10, 1024);
            if (pr.status != 200 && pr.status != 206) continue;
            auto m = Q.search(pr.header("content-disposition"));
            return {url, AH + "/", "", m ? std::stoi(m->group(1)) : 0};
        } catch (...) {
            continue;
        }
    }
    throw std::runtime_error("AnimeHeaven stream is not reachable");
}

// ---------- matching ----------
std::string akey(const std::string& title) {
    std::string t = su::lower(su::html_unescape(title));
    static const re::Regex A(R"(\((tv|dub|sub|subbed|dubbed|english dub|uncensored)[^)]*\))");
    static const re::Regex B(R"(\b(\d+)(st|nd|rd|th)\s+season\b)");
    static const re::Regex C(R"(\bseason\s*(\d+)\b)");
    static const re::Regex D(R"(\bpart\s*(\d+)\b)");
    static const re::Regex E(R"(\bthe\b)");
    static const re::Regex F(R"([^a-z0-9]+)");
    t = A.sub(" ", t);
    t = B.sub(" s\\1 ", t);
    t = C.sub(" s\\1 ", t);
    t = D.sub(" p\\1 ", t);
    t = E.sub(" ", t);
    return F.sub("", t);
}

bool same_show(const std::string& a, const std::string& b) {
    std::string x = akey(a), y = akey(b);
    return !x.empty() && x == y;
}

std::string canon(const std::string& n) {
    double f;
    if (!su::to_double(n, f)) return n;
    if (f == (double)(long long)f) return std::to_string((long long)f);
    return su::py_float(f);
}

static bool is_dub(const std::string& title) {
    static const re::Regex R(R"(\(dub|\bdub(bed)?\)?$)");
    return R.search(su::lower(title)).has_value();
}

// ---------- search ----------
static std::vector<std::string> own_episodes(const std::string& anime_id) {
    std::string s = src_of(anime_id);
    if (s == "mi") return scrape::mi_episodes(anime_id);
    if (s == "ah") return ah_episodes(anime_id);
    std::vector<std::string> out;
    for (auto& [id, n] : scrape::episodes(anime_id)) out.push_back(n);
    return out;
}

static int count_of(const std::string& anime_id) {
    {
        std::lock_guard<std::mutex> lk(g_mu);
        auto it = COUNTS.find(anime_id);
        if (it != COUNTS.end()) return it->second;
    }
    try {
        int n = (int)own_episodes(anime_id).size();
        std::lock_guard<std::mutex> lk(g_mu);
        COUNTS[anime_id] = n;
        return n;
    } catch (...) {
        return 0;
    }
}
static int known_count(const std::string& id) {
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = COUNTS.find(id);
    return it == COUNTS.end() ? 0 : it->second;
}

std::vector<scrape::Row> search(const std::string& query) {
    const std::vector<std::string> keys = {"", "ah", "mi"};
    std::map<std::string, std::future<std::vector<scrape::Row>>> futs;
    futs[""] = std::async(std::launch::async, [query] { return scrape::search(query); });
    futs["ah"] = std::async(std::launch::async, [query] { return ah_search(query); });
    futs["mi"] = std::async(std::launch::async, [query] { return scrape::mi_search(query); });
    std::map<std::string, std::vector<scrape::Row>> got;
    std::vector<std::string> errs;
    for (auto& k : keys) {
        try {
            for (auto& r : futs[k].get())
                if (!is_dub(r.title)) got[k].push_back(r);
        } catch (const std::exception& e) {
            got[k] = {};
            errs.push_back(e.what());
        }
    }
    bool any = false;
    for (auto& [k, v] : got) any = any || !v.empty();
    if (!any) throw std::runtime_error(errs.empty() ? "no results" : errs[0]);
    std::string want = akey(query);
    struct Group {
        std::string key;
        std::map<std::string, scrape::Row> rows;
    };
    std::vector<Group> groups;
    for (auto& src : keys) {
        for (auto& row : got[src]) {
            std::string k = akey(row.title);
            Group* g = nullptr;
            for (auto& gg : groups)
                if (gg.key == k) {
                    g = &gg;
                    break;
                }
            if (!g) {
                // the extra sites list loosely related shows: only the close matches may start a row when hianime answered
                if (!src.empty() && !got[""].empty() && k.find(want) == std::string::npos && want.find(k) == std::string::npos) continue;
                groups.push_back({k, {}});
                g = &groups.back();
            }
            g->rows.emplace(src, row);
        }
    }
    std::vector<std::string> need;
    int merged = 0;
    for (auto& g : groups)
        if (g.rows.size() > 1 && merged < 8) {
            merged++;
            for (auto& [s, r] : g.rows) need.push_back(r.id);
        }
    if (!need.empty()) {  // counts are only needed to pick between sites, so only for shows that more than one site has
        std::vector<std::future<int>> fs;
        for (size_t i = 0; i < need.size(); i += 8) {
            fs.clear();
            for (size_t j = i; j < need.size() && j < i + 8; j++) fs.push_back(std::async(std::launch::async, count_of, need[j]));
            for (auto& f : fs) f.get();
        }
    }
    std::vector<scrape::Row> out;
    for (auto& g : groups) {
        std::vector<std::string> order;
        for (auto& [s, r] : g.rows) order.push_back(s);
        std::stable_sort(order.begin(), order.end(), [](auto& a, auto& b) { return PREFERENCE.at(a) < PREFERENCE.at(b); });
        std::string lead = order[0];
        if (g.rows.size() > 1) {
            std::string best = order[0];
            for (auto& s : order) {
                auto key_s = std::make_pair(known_count(g.rows[s].id), -PREFERENCE.at(s));
                auto key_b = std::make_pair(known_count(g.rows[best].id), -PREFERENCE.at(best));
                if (key_s > key_b) best = s;
            }
            if (known_count(g.rows[best].id) > known_count(g.rows[lead].id) * 1.25 + 2) lead = best;  // clearly fuller: promote it
        }
        scrape::Row row = g.rows[lead];
        std::vector<std::string> alt;
        for (auto& s : order)
            if (s != lead) alt.push_back(g.rows[s].id);
        {
            std::lock_guard<std::mutex> lk(g_mu);
            ALTS[row.id] = alt;
        }
        if (row.poster.empty())
            for (auto& s : order)
                if (!g.rows[s].poster.empty()) {
                    row.poster = g.rows[s].poster;
                    break;
                }
        out.push_back(row);
    }
    return out;
}

// ---------- episodes ----------
static std::pair<int, double> num_key(const std::string& n) {
    double f;
    if (su::to_double(n, f)) return {0, f};
    return {1, 0.0};
}

std::vector<std::string> episodes(const std::string& anime_id, const std::string& title) {
    auto eps = own_episodes(anime_id);
    {
        std::lock_guard<std::mutex> lk(g_mu);
        COUNTS[anime_id] = (int)eps.size();
    }
    std::set<std::string> have;
    for (auto& n : eps) have.insert(canon(n));
    std::map<std::string, std::string> extra;
    std::vector<std::string> extra_order;
    for (auto& alt : alts(anime_id, title, false)) {
        try {
            for (auto& n : own_episodes(alt)) {
                std::string c = canon(n);
                if (!have.count(c) && !extra.count(c)) {
                    extra[c] = n;
                    extra_order.push_back(c);
                }
            }
        } catch (...) {
            continue;
        }
    }
    if (!extra.empty()) {
        for (auto& c : extra_order) eps.push_back(extra[c]);
        std::stable_sort(eps.begin(), eps.end(), [](auto& a, auto& b) { return num_key(a) < num_key(b); });
    }
    return eps;
}

std::vector<std::string> alts(const std::string& anime_id, const std::string& title, bool lookup) {
    {
        std::lock_guard<std::mutex> lk(g_mu);
        auto it = ALTS.find(anime_id);
        if (it != ALTS.end()) return it->second;
        if (!lookup) return {};
    }
    std::vector<std::string> found;
    if (!title.empty()) {
        std::string mine = src_of(anime_id);
        std::vector<std::pair<std::string, std::vector<scrape::Row> (*)(const std::string&)>> fns = {
            {"", scrape::search}, {"ah", ah_search}, {"mi", scrape::mi_search}};
        for (auto& [src, fn] : fns) {
            if (src == mine) continue;
            try {
                for (auto& r : fn(title))
                    if (!is_dub(r.title) && same_show(r.title, title)) {
                        found.push_back(r.id);
                        break;
                    }
            } catch (...) {
            }
        }
    }
    std::lock_guard<std::mutex> lk(g_mu);
    ALTS[anime_id] = found;
    return found;
}

// ---------- link ----------
static std::string own_number(const std::string& anime_id, const std::string& ep) {
    auto mine = own_episodes(anime_id);
    if (std::find(mine.begin(), mine.end(), ep) != mine.end()) return ep;
    for (auto& n : mine)
        if (canon(n) == canon(ep)) return n;
    return ep;
}

static Got try_one(const std::string& anime_id, std::string ep, const std::string& mode) {
    std::string s = src_of(anime_id);
    if (s != "mi") ep = own_number(anime_id, ep);  // miruro numbers are plain 1..N
    if (s == "mi") {
        auto l = scrape::mi_link(anime_id, ep, mode);
        return {l.url, l.referer, l.sub, scrape::quality_of(l.url)};
    }
    if (s == "ah") {
        if (mode == "dub") {
            std::string title;
            {
                std::lock_guard<std::mutex> lk(g_mu);
                auto it = ah_title.find(anime_id.substr(AH_PREFIX.size()));
                if (it != ah_title.end()) title = it->second;
            }
            std::string dub;
            if (!title.empty())
                for (auto& r : ah_search(title + " dub"))
                    if (is_dub(r.title) && same_show(r.title, title)) {
                        dub = r.id;
                        break;
                    }
            if (dub.empty()) throw std::runtime_error("AnimeHeaven has no dub of this show");
            return ah_link(dub, ep);
        }
        return ah_link(anime_id, ep);
    }
    auto l = scrape::link(anime_id, ep, mode);
    if (!scrape::alive(l.url, l.referer)) throw std::runtime_error("hianime stream is not reachable");
    return {l.url, l.referer, l.sub, scrape::quality_of(l.url)};
}

Stream link(const std::string& anime_id, const std::string& ep, const std::string& mode, const std::string& title) {
    std::vector<std::string> errors;
    std::vector<std::string> modes = mode != "sub" ? std::vector<std::string>{mode, "sub"} : std::vector<std::string>{"sub"};
    for (auto& md : modes) {
        std::optional<std::tuple<int, std::string, Got>> best;
        std::vector<std::string> ids = {anime_id};
        size_t i = 0;
        while (i < ids.size()) {
            std::string sid = ids[i++];
            try {
                Got res = try_one(sid, ep, md);
                int h = res.height ? res.height : GOOD_HEIGHT;  // unknown quality counts as good enough
                if (!best || h > std::get<0>(*best)) best = std::make_tuple(h, sid, res);
                if (h >= GOOD_HEIGHT) break;
            } catch (const std::exception& e) {
                errors.push_back(label(sid) + ": " + e.what());
            }
            if (i == ids.size() && ids.size() == 1) {  // the alternates are only looked up once the primary was not enough
                for (auto& a : alts(anime_id, title)) ids.push_back(a);
            }
        }
        if (best) {
            auto& [h, sid, res] = *best;
            Stream st{res.url, res.referer, res.sub, scrape::skip_info(sid, ep), label(sid)};
            std::lock_guard<std::mutex> lk(g_mu);
            SOURCE[{anime_id, ep}] = label(sid);
            return st;
        }
    }
    std::string msg = "No working stream for episode " + ep + ": ";
    size_t from = errors.size() > 4 ? errors.size() - 4 : 0;
    for (size_t k = from; k < errors.size(); k++) msg += (k > from ? "; " : "") + errors[k];
    throw std::runtime_error(msg);
}

std::string resolve(const std::string& title) {
    {
        std::lock_guard<std::mutex> lk(g_mu);
        auto it = g_resolved.find(title);
        if (it != g_resolved.end()) return it->second;
    }
    auto rows = search(title);
    if (rows.empty()) throw std::runtime_error("'" + title + "' was not found on any site");
    std::string id = rows[0].id;
    for (auto& r : rows)
        if (same_show(r.title, title)) {
            id = r.id;
            break;
        }
    std::lock_guard<std::mutex> lk(g_mu);
    g_resolved[title] = id;
    return id;
}
}  // namespace multisrc
