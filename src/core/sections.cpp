#include "sections.hpp"

#include <cstdio>
#include <ctime>

#include "../util/re.hpp"
#include "../util/strutil.hpp"
#include "scrape.hpp"

namespace sections {
std::string kind_of(const json& row) {
    static const re::Regex MOVIE(R"(\bmovie\b|\bfilm\b|gekijouban|\(movie\))", re::I);
    static const re::Regex SPECIAL(R"(\b(ova|oad|ona|specials?|recap|mini anime|shorts?|picture drama|extras?|omake)\b)", re::I);
    static const re::Regex SEQUEL(R"(\bseason\s*\d|\b\d+(st|nd|rd|th)\s+season|\bpart\s*\d|\bfinal season\b|\bcour\s*\d|\b(ii|iii|iv)\b$|\bs\d\b$)", re::I);
    std::string id = row.value("id", ""), t = row.value("title", "");
    std::string fmt;
    if (su::starts_with(id, scrape::MI_PREFIX)) {  // miruro rows carry the site's own format
        auto meta = scrape::mi_meta(id.substr(scrape::MI_PREFIX.size()));
        if (meta) fmt = meta->format;
    }
    if (fmt == "MOVIE" || MOVIE.search(t)) return "movie";
    if (fmt == "OVA" || fmt == "ONA" || fmt == "SPECIAL" || fmt == "TV_SHORT" || fmt == "MUSIC" || SPECIAL.search(t)) return "special";
    if (SEQUEL.search(t)) return "sequel";
    return "main";
}

Groups group_search(const std::vector<json>& rows) {
    static const std::vector<std::pair<std::string, std::string>> LABELS = {
        {"main", "MAIN SERIES"}, {"sequel", "SEASONS AND SEQUELS"}, {"movie", "MOVIES"}, {"special", "OVA / ONA / SPECIALS"}};
    Groups out;
    for (auto& [k, label] : LABELS) {
        std::vector<json> b;
        for (auto& r : rows)
            if (kind_of(r) == k) b.push_back(r);
        if (!b.empty()) out.emplace_back(label, b);
    }
    return out;
}

static long days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

Groups group_history(const std::vector<json>& rows) {
    std::time_t now = std::time(nullptr);
    std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &now);
#else
    localtime_r(&now, &lt);
#endif
    long today = days_from_civil(lt.tm_year + 1900, (unsigned)lt.tm_mon + 1, (unsigned)lt.tm_mday);
    Groups buckets = {{"TODAY", {}}, {"THIS WEEK", {}}, {"THIS MONTH", {}}, {"EARLIER", {}}};
    for (auto& r : rows) {
        std::string last = r.contains("last") && r["last"].is_string() ? r["last"].get<std::string>() : "";
        long age = 999;
        int y, m, d;
        if (last.size() >= 10 && std::sscanf(last.c_str(), "%4d-%2d-%2d", &y, &m, &d) == 3) age = today - days_from_civil(y, (unsigned)m, (unsigned)d);
        buckets[age <= 0 ? 0 : age <= 7 ? 1 : age <= 31 ? 2 : 3].second.push_back(r);
    }
    Groups out;
    for (auto& b : buckets)
        if (!b.second.empty()) out.push_back(b);
    return out;
}
}  // namespace sections
