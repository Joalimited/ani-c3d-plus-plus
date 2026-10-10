#include "scrape.hpp"

#include <algorithm>
#include <stdexcept>

#include "../util/http.hpp"
#include "../util/re.hpp"
#include "../util/strutil.hpp"
#include "json.hpp"

using json = nlohmann::json;

namespace scrape {
const std::string BASE = "https://hianime.at";
const std::string UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36";
const std::string MIRURO = "https://www.miruro.to";
const std::string MI_PREFIX = "mi:";
static const std::string KEY = "otaku-embed-v1";   // the embed page ships its config as base64(json XOR this key)
static const std::string MI_KEY = "miruro/catalog";  // the site XORs its catalog responses with this key, then gzips them

static std::mutex g_mu;
static std::map<std::string, MiMeta> g_mi_meta;                                         // anime id -> meta
static std::map<std::string, int> g_quality;                                            // stream url -> pixel height
static std::map<std::pair<std::string, std::string>, std::map<std::string, Range>> g_skip;  // (anime id, ep) -> ranges

std::mutex& mu() { return g_mu; }
std::optional<MiMeta> mi_meta(const std::string& raw) {
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_mi_meta.find(raw);
    if (it == g_mi_meta.end()) return std::nullopt;
    return it->second;
}
int quality_of(const std::string& url) {
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_quality.find(url);
    return it == g_quality.end() ? 0 : it->second;
}
void set_quality(const std::string& url, int h) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_quality[url] = h;
}
std::map<std::string, Range> skip_info(const std::string& anime_id, const std::string& ep) {
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_skip.find({anime_id, ep});
    return it == g_skip.end() ? std::map<std::string, Range>{} : it->second;
}

static http::Resp client_get(const std::string& url, const http::KV& headers = {}, double timeout = 15, size_t max_bytes = 0,
                             const http::KV& params = {}) {
    http::Req r;
    r.url = url;
    r.params = params;
    r.headers = headers;
    r.headers.emplace_back("User-Agent", UA);
    r.timeout = timeout;
    r.max_bytes = max_bytes;
    r.jar = http::Jar::SCRAPE;
    return http::request(r);
}

std::string get(const std::string& url, const std::string& referer) {
    http::KV h;
    if (!referer.empty()) h.emplace_back("Referer", referer);
    auto r = client_get(url, h);
    if (su::contains(r.body.substr(0, 4000), "Just a moment")) throw std::runtime_error("Blocked by Cloudflare - try again in a moment");
    if (r.status >= 400) throw std::runtime_error("HTTP " + std::to_string(r.status) + " from " + url);
    return r.body;
}

static std::vector<Row> cards(std::string page) {
    page = su::split(page, "id=\"main-sidebar\"")[0];  // the sidebar repeats the markup
    page = su::replace(page, "\n", " ");
    static const re::Regex R(R"re(<img src="([^"]*)".*?<h3 class="film-name">\s*<a href="[^"]*/([^"/]*)"\s*title="([^"]*)")re");
    std::vector<Row> out;
    auto chunks = su::split(page, "<div class=\"film-poster\">");
    for (size_t i = 1; i < chunks.size(); i++) {
        if (auto m = R.search(chunks[i])) out.push_back({m->group(2), su::html_unescape(m->group(3)), m->group(1)});
    }
    return out;
}

std::vector<Row> search(const std::string& query) { return cards(get(BASE + "/search?keyword=" + su::url_quote_plus(query))); }
std::vector<Row> listing(const std::string& name) { return cards(get(BASE + "/" + name)); }

static std::string escape_re(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (std::string("\\^$.|?*+()[]{}/-").find(c) != std::string::npos) out += '\\';
        out += c;
    }
    return out;
}

std::vector<std::pair<std::string, std::string>> episodes(const std::string& anime_id) {
    std::string num = anime_id.substr(anime_id.rfind('-') + 1);
    std::string page = su::replace(get(BASE + "/api/theme/episode/list/" + num), "\\", "");
    re::Regex R("data-number=\"([^\"]*)\".*data-id=\"([0-9]+)\".*/watch/" + escape_re(anime_id) + "[?]ep=");
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& part : su::split(su::replace(page, "ep-item", "\nep-item"), "\n")) {
        if (auto m = R.search(part)) out.emplace_back(m->group(2), m->group(1));
    }
    return out;
}

static std::string deobfuscate(const std::string& blob) {
    std::string raw = su::base64_decode(blob);
    for (size_t i = 0; i < raw.size(); i++) raw[i] = (char)(raw[i] ^ KEY[i % KEY.size()]);
    return raw;
}

Link link(const std::string& anime_id, const std::string& ep, const std::string& mode, const std::string& quality) {
    std::map<std::string, std::string> eps;
    for (auto& [id, n] : episodes(anime_id)) eps[n] = id;
    if (!eps.count(ep)) throw std::runtime_error("Episode not released!");
    std::string servers = get(BASE + "/api/theme/episode/servers?episodeId=" + eps[ep]);
    servers = su::replace(su::replace(servers, "\\\"", "\""), "server-item", "\nserver-item");
    std::string h;
    re::Regex R("data-type=\"" + escape_re(mode) + "\".*data-server-name=\"ZokoAnime\".*data-hash=\"([^\"]*)\"");
    for (const auto& line : su::split(servers, "\n")) {  // only the ZokoAnime embed is understood; the other servers use different players
        if (auto m = R.search(line)) {
            h = m->group(1);
            break;
        }
    }
    if (h.empty()) throw std::runtime_error("No sources found for " + mode + "!");
    std::string embed = su::base64_decode(h);
    static const re::Regex HOST(R"(^(https?://[^/]*).*)", re::S);
    std::string refr = HOST.sub("\\1/", embed);
    static const re::Regex P(R"re(window\.__P="([^"]*)")re");
    auto blob = P.search(get(embed));
    if (!blob) throw std::runtime_error("Embed page changed");
    std::string cfg = deobfuscate(blob->group(1));
    try {  // some embeds ship intro/outro timestamps ("skip":{"intro":{"start":..,"end":..},"outro":{..}})
        json j = json::parse(cfg);
        if (j.is_object() && j.contains("skip") && j["skip"].is_object()) {
            std::map<std::string, Range> sk;
            for (const char* k : {"intro", "outro"}) {
                if (j["skip"].contains(k) && j["skip"][k].is_object()) {
                    auto& o = j["skip"][k];
                    auto num = [](const json& v) { return v.is_number() ? v.get<double>() : std::stod(v.get<std::string>()); };
                    sk[k] = {num(o.at("start")), num(o.at("end"))};
                }
            }
            std::lock_guard<std::mutex> lk(g_mu);
            g_skip[{anime_id, ep}] = sk;
        }
    } catch (...) {
    }
    static const re::Regex SRC(R"re("src":"([^"]*\.m3u8[^"]*)")re");
    auto m3u8 = SRC.search(cfg);
    if (!m3u8) throw std::runtime_error("No stream in the embed config");
    std::string master = su::replace(m3u8->group(1), "\\/", "/");
    std::string sub;
    {
        auto parts = su::split(cfg, "\"subtitles\":[", 1);
        if (parts.size() > 1) {
            std::string arr = su::split(parts[1], "]", 1)[0];
            static const re::Regex OBJ(R"(\{[^{}]*\})");
            static const re::Regex S2(R"re("src":"([^"]*)")re");
            for (const auto& o : OBJ.finditer(arr)) {
                if (su::contains(o.group(0), "\"default\":true")) {
                    if (auto s = S2.search(o.group(0))) sub = su::replace(s->group(1), "\\/", "/");
                    break;
                }
            }
        }
    }
    std::string text = get(master, refr);
    std::string base = master.substr(0, master.rfind('/')) + "/";
    std::vector<std::pair<int, std::string>> variants;
    auto lines = su::splitlines(text);
    static const re::Regex RES(R"(RESOLUTION=\d+x(\d+))");
    for (size_t i = 0; i < lines.size(); i++) {
        if (su::starts_with(lines[i], "#EXT-X-STREAM-INF")) {
            auto res = RES.search(lines[i]);
            std::string url;
            for (size_t k = i + 1; k < lines.size(); k++)
                if (!lines[k].empty() && lines[k][0] != '#') {
                    url = lines[k];
                    break;
                }
            if (!url.empty()) variants.emplace_back(res ? std::stoi(res->group(1)) : 0, su::starts_with(url, "http") ? url : base + url);
        }
    }
    if (variants.empty()) {
        set_quality(master, 0);
        return {master, refr, sub};
    }
    std::stable_sort(variants.begin(), variants.end(), [](auto& a, auto& b) { return a.first > b.first; });
    std::pair<int, std::string> chosen = variants[0];
    if (quality == "worst") chosen = variants.back();
    else if (quality != "best" && !quality.empty()) {
        for (auto& v : variants)
            if (std::to_string(v.first) + "p" == quality) {
                chosen = v;
                break;
            }
    }
    set_quality(chosen.second, chosen.first);
    return {chosen.second, refr, sub};
}

// ---------- miruro.to: the fallback source (used when hianime has nothing, or can't stream an episode) ----------
static json mi_get(const std::string& path, const http::KV& query = {}) {
    http::KV h = {{"Referer", MIRURO + "/"}, {"Accept", "*/*"}, {"Sec-Fetch-Dest", "empty"}, {"Sec-Fetch-Mode", "cors"}, {"Sec-Fetch-Site", "same-origin"}};
    auto r = client_get(MIRURO + "/api" + path, h, 15, 0, query);
    if (r.status >= 400) throw std::runtime_error("miruro HTTP " + std::to_string(r.status));
    if (su::starts_with(r.header("content-type"), "application/octet-stream")) {
        std::string raw = r.body;
        for (size_t i = 0; i < raw.size(); i++) raw[i] = (char)(raw[i] ^ MI_KEY[i % MI_KEY.size()]);
        std::string out;
        if (!su::gunzip(raw, out)) throw std::runtime_error("miruro: bad catalog payload");
        return json::parse(out);
    }
    return json::parse(r.body);
}

static std::string mi_title(const json& t) {
    std::string s;
    for (const char* k : {"english", "romaji", "native"}) {
        if (t.contains(k) && t[k].is_string() && !t[k].get<std::string>().empty()) {
            s = t[k].get<std::string>();
            break;
        }
    }
    if (s.empty()) s = "?";
    return su::replace(s, "\xE2\x80\x99", "'");
}

static int mi_count(const json& it) {
    int n = 0;
    if (it.contains("episode_counts") && it["episode_counts"].is_object())
        for (auto& [k, v] : it["episode_counts"].items())
            if (v.is_number()) n = std::max(n, v.get<int>());
    if (it.contains("episode_count") && it["episode_count"].is_number()) n = std::max(n, it["episode_count"].get<int>());
    return n;
}

static std::string jstr(const json& j, const char* k) {
    return (j.is_object() && j.contains(k) && j[k].is_string()) ? j[k].get<std::string>() : "";
}

std::vector<Row> mi_search(const std::string& query) {
    std::vector<Row> out;
    json d = mi_get("/v1/anime", {{"q", query}, {"limit", "15"}, {"sort", "-popularity"}});
    if (!d.contains("data")) return out;
    for (auto& it : d["data"]) {
        std::string id = it["id"].is_string() ? it["id"].get<std::string>() : it["id"].dump();
        std::string title = mi_title(it["title"]);
        {
            std::lock_guard<std::mutex> lk(g_mu);
            g_mi_meta[id] = {title, mi_count(it), jstr(it, "format")};
        }
        out.push_back({MI_PREFIX + id, title, jstr(it, "cover_url")});
    }
    return out;
}

std::vector<std::string> mi_episodes(const std::string& anime_id) {
    std::string raw = anime_id.substr(MI_PREFIX.size());
    auto meta = mi_meta(raw);
    if (!meta) {
        json d = mi_get("/v1/anime/" + raw);
        MiMeta m{mi_title(d["title"]), mi_count(d), jstr(d, "format")};
        std::lock_guard<std::mutex> lk(g_mu);
        g_mi_meta[raw] = m;
        meta = m;
    }
    std::vector<std::string> out;
    for (int i = 1; i <= std::max(meta->episodes, 1); i++) out.push_back(std::to_string(i));
    return out;
}

std::string urljoin(const std::string& base, const std::string& ref) {
    if (su::starts_with(ref, "http://") || su::starts_with(ref, "https://")) return ref;
    size_t scheme = base.find("://");
    if (scheme == std::string::npos) return ref;
    if (su::starts_with(ref, "//")) return base.substr(0, scheme + 1) + ref;
    size_t host_end = base.find('/', scheme + 3);
    std::string origin = host_end == std::string::npos ? base : base.substr(0, host_end);
    if (!ref.empty() && ref[0] == '/') return origin + ref;
    std::string dir = base.substr(0, base.find('?'));
    dir = dir.substr(0, dir.rfind('/') + 1);
    if (dir.size() <= scheme + 3) dir = origin + "/";
    return dir + ref;
}

// True when the stream really plays: the playlist answers, and so does its first media segment. (Several of miruro's
// providers sit behind a Cloudflare challenge, or list segments that never arrive.)
bool alive(const std::string& url, const std::string& referer) {
    try {
        http::KV hdr = {{"Referer", referer}};
        auto top = client_get(url, hdr, 8);
        if (top.status != 200 || !su::starts_with(top.body, "#EXTM3U")) return false;
        std::string target = url, text = top.body, base = url;
        for (int k = 0; k < 2; k++) {  // master playlist -> variant playlist -> first segment
            std::vector<std::string> uris;
            for (auto& ln : su::splitlines(text))
                if (!su::strip(ln).empty() && ln[0] != '#') uris.push_back(su::strip(ln));
            if (uris.empty()) return false;
            target = urljoin(base, uris[0]);
            if (su::contains(text, "#EXT-X-STREAM-INF")) {
                auto r = client_get(target, hdr, 8);
                if (r.status != 200) return false;
                text = r.body;
                base = target;
                continue;
            }
            break;
        }
        http::KV h2 = hdr;
        h2.emplace_back("Range", "bytes=0-2047");
        auto r = client_get(target, h2, 8, 2048);
        return (r.status == 200 || r.status == 206) && !r.body.empty();
    } catch (...) {
        return false;
    }
}

Link mi_link(const std::string& anime_id, const std::string& ep, const std::string& mode) {
    std::string raw = anime_id.substr(MI_PREFIX.size());
    json play = mi_get("/v1/anime/" + raw + "/episodes/" + ep + "/play");
    std::vector<std::string> order;
    if (mode == "sub") order = {"sub", "ssub"};
    else order = {mode, "sub", "ssub"};
    int tried = 0;
    if (!play.contains("tracks")) throw std::runtime_error("No working miruro stream for episode " + ep + " (" + mode + "); tried 0");
    for (const auto& track : order) {
        for (auto& t : play["tracks"]) {
            if (jstr(t, "track") != track) continue;
            if (!t.contains("providers")) continue;
            for (auto& prov : t["providers"]) {
                std::string sub;
                if (prov.contains("subtitles"))
                    for (auto& sb : prov["subtitles"]) {
                        std::string u = jstr(sb, "url");
                        if (u.empty()) u = jstr(sb, "file");
                        std::string lang = sb.contains("language") ? (sb["language"].is_string() ? sb["language"].get<std::string>() : sb["language"].dump()) : "";
                        std::string label = sb.contains("label") ? (sb["label"].is_string() ? sb["label"].get<std::string>() : sb["label"].dump()) : "";
                        if (!u.empty() && (su::contains(su::lower(lang), "en") || su::contains(su::lower(label), "eng"))) {
                            sub = u;
                            break;
                        }
                    }
                if (!prov.contains("servers")) continue;
                for (auto& server : prov["servers"]) {
                    std::vector<json> streams;
                    if (server.contains("streams"))
                        for (auto& s : server["streams"]) {
                            std::string fmt = s.contains("format") && s["format"].is_string() ? s["format"].get<std::string>() : "hls";
                            if (!jstr(s, "url").empty() && fmt == "hls") streams.push_back(s);
                        }
                    auto height = [](const json& s) {
                        if (s.contains("resolution") && s["resolution"].is_object() && s["resolution"].contains("height") && s["resolution"]["height"].is_number())
                            return s["resolution"]["height"].get<int>();
                        return 0;
                    };
                    std::stable_sort(streams.begin(), streams.end(), [&](const json& a, const json& b) { return height(a) > height(b); });
                    std::string ref = MIRURO + "/";
                    if (server.contains("headers") && server["headers"].is_object()) {
                        std::string r2 = jstr(server["headers"], "Referer");
                        if (!r2.empty()) ref = r2;
                    }
                    for (size_t k = 0; k < streams.size() && k < 1; k++) {
                        tried++;
                        std::string u = jstr(streams[k], "url");
                        if (alive(u, ref)) {
                            set_quality(u, height(streams[k]));
                            return {u, ref, sub};
                        }
                    }
                }
            }
        }
    }
    throw std::runtime_error("No working miruro stream for episode " + ep + " (" + mode + "); tried " + std::to_string(tried));
}

static std::string normish(const std::string& s) {
    static const re::Regex R("[^a-z0-9]+");
    return su::strip(R.sub(" ", su::lower(s)));
}

std::string best_miruro(const std::string& title) {
    std::string want = normish(title);
    static const re::Regex FMT(R"(\s*\((tv|movie|ova|ona|special)[^)]*\))", re::I);
    auto rows = mi_search(FMT.sub("", title));
    for (auto& r : rows)
        if (normish(r.title) == want) return r.id;
    return rows.empty() ? "" : rows[0].id;
}
}  // namespace scrape
