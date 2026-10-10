#include "mangasrc.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>

#include "../util/http.hpp"
#include "../util/re.hpp"
#include "../util/strutil.hpp"

namespace mangasrc {
const std::string UA = "Mozilla/5.0 (X11; Linux x86_64; rv:130.0) Gecko/20100101 Firefox/130.0";
const std::string PILL = "https://mangapill.com", KATANA = "https://mangakatana.com", READ = "https://www.mangaread.org";
const std::map<std::string, std::string> LABELS = {{"mp", "MangaPill"}, {"mk", "MangaKatana"}, {"mr", "MangaRead"}, {"md", "MangaDex"}};

static std::string get(const std::string& url, const http::KV& params = {}) {
    http::Req r;
    r.url = url;
    r.params = params;
    r.headers = {{"User-Agent", UA}};
    r.timeout = 20;
    r.jar = http::Jar::MANGA;
    auto resp = http::request(r);
    if (resp.status >= 400 || su::contains(resp.body.substr(0, 3000), "Just a moment"))
        throw std::runtime_error(url + ": HTTP " + std::to_string(resp.status));
    return resp.body;
}

static std::string escape_re(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (std::string("\\^$.|?*+()[]{}").find(c) != std::string::npos) out += '\\';
        out += c;
    }
    return out;
}

// the chapter number in a label ("Chapter 12.5" -> "12.5", "Ch. 7.0" -> "7")
static std::string num(const std::string& label) {
    static const re::Regex A(R"(chapter\s*([0-9]+(?:\.[0-9]+)?))", re::I);
    static const re::Regex B(R"(([0-9]+(?:\.[0-9]+)?))");
    auto m = A.search(label);
    if (!m) m = B.search(label);
    if (!m) return "";
    std::string g = m->group(1);
    if (g.find('.') != std::string::npos) {
        g = su::rstrip(g, "0");
        g = su::rstrip(g, ".");
    }
    return g;
}

static std::vector<Chapter> sort_chapters(const std::vector<Chapter>& chs) {
    std::set<std::string> seen;
    std::vector<Chapter> out;
    for (const auto& c : chs) {
        if (!c.chapter.empty() && !seen.count(c.chapter)) {
            seen.insert(c.chapter);
            out.push_back(c);
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const Chapter& a, const Chapter& b) { return std::stod(a.chapter) < std::stod(b.chapter); });
    return out;
}

std::vector<Item> search(const std::string& query, int n) {
    std::vector<Item> out;
    try {
        std::string t = get(PILL + "/search", {{"q", query}});
        std::set<std::string> seen;
        static const re::Regex R(R"re(<a href="(/manga/(\d+/[^"]+))" class="relative block">\s*<figure[^>]*>\s*<img data-src="([^"]+)")re");
        for (const auto& m : R.finditer(t)) {
            if (seen.count(m.group(2))) continue;
            seen.insert(m.group(2));
            re::Regex T(escape_re("<a href=\"" + m.group(1) + "\"") + R"([^>]*>\s*<div[^>]*>([^<]+)<)");
            auto title = T.search(t.substr((size_t)m.end()));
            std::string name;
            if (title) name = su::html_unescape(su::strip(title->group(1)));
            else {
                std::string slug = m.group(2).substr(m.group(2).rfind('/') + 1);
                name = su::title_case(su::replace(slug, "-", " "));
            }
            out.push_back({"mp:" + m.group(2), name, m.group(3), "mp"});
            if ((int)seen.size() >= n) break;
        }
    } catch (...) {
    }
    try {
        std::string t = get(KATANA + "/", {{"search", query}, {"search_by", "book_name"}});
        if (su::contains(t, "id=\"book_list\"")) {
            static const re::Regex R(R"re(<img src="([^"]+)" alt="\[Cover\]">.*?<a href="https://mangakatana\.com/manga/([^"]+)" target="_blank">([^<]+)</a>)re", re::S);
            int k = 0;
            for (const auto& m : R.finditer(t)) {
                if (k++ >= n) break;
                out.push_back({"mk:" + m.group(2), su::html_unescape(su::strip(m.group(3))), m.group(1), "mk"});
            }
        } else if (su::contains(t, "/manga/") && su::contains(t, "og:url")) {  // a single hit redirects straight to the series page
            static const re::Regex U(R"re(og:url" content="https://mangakatana\.com/manga/([^"]+)")re");
            static const re::Regex H(R"re(<h1 class="heading">([^<]+)<)re");
            auto m = U.search(t);
            auto ti = H.search(t);
            if (m && ti) out.push_back({"mk:" + m->group(1), su::html_unescape(su::strip(ti->group(1))), "", "mk"});
        }
    } catch (...) {
    }
    try {
        std::string t = get(READ + "/", {{"s", query}, {"post_type", "wp-manga"}});
        re::Regex R(R"re(<img[^>]+(?:data-src|src)="([^"]+)"[^>]*>.{0,600}?<h3 class="h4">\s*<a href=")re" + escape_re(READ) +
                        R"re(/manga/([^/"]+)/">([^<]+)</a>)re",
                    re::S);
        int k = 0;
        for (const auto& m : R.finditer(t)) {
            if (k++ >= n) break;
            out.push_back({"mr:" + m.group(2), su::html_unescape(su::strip(m.group(3))), m.group(1), "mr"});
        }
    } catch (...) {
    }
    return out;
}

std::vector<Chapter> chapters(const std::string& book_id) {
    auto c = book_id.find(':');
    std::string src = book_id.substr(0, c), path = book_id.substr(c + 1);
    std::vector<Chapter> chs;
    if (src == "mp") {
        std::string t = get(PILL + "/manga/" + path);
        static const re::Regex R(R"re(href="(/chapters/[^"]+)"[^>]*>\s*([^<]+)<)re");
        for (const auto& m : R.finditer(t)) chs.push_back({m.group(1), num(m.group(2)), "mp"});
    } else if (src == "mk") {
        std::string t = get(KATANA + "/manga/" + path);
        std::string base = KATANA + "/manga/" + path + "/c";
        static const re::Regex R(R"re(<div class="chapter">\s*<a href="([^"]+)">([^<]+)</a>)re");
        for (const auto& m : R.finditer(t))
            if (su::starts_with(m.group(1), base)) chs.push_back({m.group(1), num(m.group(2)), "mk"});
    } else if (src == "mr") {
        std::string url = READ + "/manga/" + path + "/";
        static const re::Regex R(R"re(<li class="wp-manga-chapter[^"]*">\s*<a href="([^"]+)">\s*([^<]+?)\s*</a>)re");
        std::string t = get(url);
        auto found = R.finditer(t);
        if (found.empty()) {
            http::Req r;
            r.url = url + "ajax/chapters/";
            r.method = "POST";
            r.headers = {{"User-Agent", UA}, {"X-Requested-With", "XMLHttpRequest"}};
            r.timeout = 20;
            r.jar = http::Jar::MANGA;
            found = R.finditer(http::request(r).body);
        }
        for (const auto& m : found) chs.push_back({m.group(1), num(m.group(2)), "mr"});
    } else {
        throw std::invalid_argument(book_id);
    }
    return sort_chapters(chs);
}

std::vector<std::string> pages(const Chapter& ch) {
    std::vector<std::string> out;
    if (ch.src == "mp") {
        std::string t = get(PILL + ch.id);
        static const re::Regex R(R"re(<img[^>]+data-src="([^"]+)")re");
        for (auto& u : re::findall1(R, t))
            if (su::contains(u, "/file/") || su::contains(u, "mangap")) out.push_back(u);
        return out;
    }
    if (ch.src == "mr") {
        std::string t = get(ch.id);
        static const re::Regex R(R"re(class="wp-manga-chapter-img"[^>]*?(?:data-src|src)="\s*([^"\s]+))re");
        for (auto& u : re::findall1(R, t))
            if (su::starts_with(u, "http")) out.push_back(u);
        if (!out.empty()) return out;
        static const re::Regex R2(R"re(<img[^>]+(?:data-src|src)="\s*(https?://[^"\s]+/WP-manga/data/[^"\s]+))re");
        return re::findall1(R2, t);
    }
    std::string t = get(ch.id);
    static const re::Regex ARR(R"re(var \w+=\[((?:'[^']*',?)+)\])re");
    auto arrs = re::findall1(ARR, t);
    if (arrs.empty()) return out;
    auto longest = *std::max_element(arrs.begin(), arrs.end(), [](auto& a, auto& b) { return a.size() < b.size(); });
    static const re::Regex Q(R"re('([^']*)')re");
    return re::findall1(Q, longest);
}

std::string referer(const std::string& url) {
    if (su::contains(url, "mangakatana")) return KATANA + "/";
    if (su::contains(url, "mangaread.org")) return READ + "/";
    if (su::contains(url, "readdetectiveconan") || su::contains(url, "mangap")) return PILL + "/";
    return "";
}
}  // namespace mangasrc
