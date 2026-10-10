#include "advance.hpp"

#include <algorithm>
#include <cstdio>
#include <map>

#include "../util/db.hpp"
#include "../util/strutil.hpp"

namespace advance {
struct Tier {
    long long need;
    const char* title;
    const char* desc;
};
static const std::vector<Tier> ANIME_TIERS = {{1, "FIRST CONTACT", "watch your first episode of {a}"}, {3, "HOOKED", "watch 3 episodes of {a}"},
                                              {6, "SETTLING IN", "watch 6 episodes of {a}"},         {12, "FAITHFUL", "watch 12 episodes of {a}"},
                                              {24, "DEVOTED", "watch 24 episodes of {a}"},           {50, "VETERAN", "watch 50 episodes of {a}"},
                                              {100, "LEGEND OF {a}", "watch 100 episodes of {a}"}};
static const std::vector<Tier> TOTAL_TIERS = {{1, "THE JOURNEY BEGINS", "watch an episode"}, {10, "WARMING UP", "watch 10 episodes"},
                                              {25, "SEASON PASS", "watch 25 episodes"},       {50, "DEDICATED VIEWER", "watch 50 episodes"},
                                              {100, "CENTURION", "watch 100 episodes"},       {250, "COUCH POTATO", "watch 250 episodes"},
                                              {500, "TRUE OTAKU", "watch 500 episodes"},      {1000, "TOUCH GRASS?", "watch 1000 episodes"}};
static const std::vector<Tier> SERIES_TIERS = {{2, "VARIETY PACK", "watch 2 different anime"}, {5, "SAMPLER", "watch 5 different anime"},
                                               {10, "CONNOISSEUR", "watch 10 different anime"}, {25, "ARCHIVIST", "watch 25 different anime"}};
static const std::vector<Tier> MANGA_TIERS = {{1, "FIRST PAGE", "read a manga chapter"}, {10, "PAGE TURNER", "read 10 manga chapters"},
                                              {50, "BOOKWORM", "read 50 manga chapters"}, {200, "LIBRARIAN", "read 200 manga chapters"}};
static const std::vector<std::tuple<std::string, std::string, std::string>> GAME_ADV = {
    {"kong3", "KONG CLIMBER", "reach stage 3 of the barrel climb"},
    {"kong_hammer", "HAMMER TIME", "smash 5 barrels with the hammer in one run"},
    {"maze1", "PATHFINDER", "find the item in the data maze"},
    {"maze3", "LOST AND FOUND", "clear 3 maze levels in a row"},
    {"galaga_boss", "BOSS SLAYER", "defeat an anime-character boss in Dino Galaga"},
    {"galaga_w3", "DINO ACE", "reach wave 3 in Dino Galaga"},
    {"quiz", "POSTER EXPERT", "score 600+ in the poster quiz"},
    {"trivia", "TRIVIA MASTER", "finish anime trivia with a 7-answer streak"}};

static std::vector<Item> tiers(const std::string& prefix, long long count, const std::vector<Tier>& ts, const std::string& who = "") {
    std::vector<Item> out;
    for (auto& t : ts)
        out.push_back({prefix + ":" + std::to_string(t.need), su::replace(t.title, "{a}", su::upper(who)), su::replace(t.desc, "{a}", who),
                       std::min(count, t.need), t.need, count >= t.need});
    return out;
}

static long days_of(const std::string& iso) {
    int y, m, d;
    if (std::sscanf(iso.c_str(), "%4d-%2d-%2d", &y, &m, &d) != 3) return -1;
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (unsigned)(m + (m > 2 ? -3 : 9)) + 2) / 5 + (unsigned)d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

std::vector<Group> compute() {
    std::vector<Group> groups;
    std::vector<db::Row> hist;
    std::vector<std::string> days;
    long long per_day = 0, night = 0, chapters = 0;
    std::map<std::string, long long> totals;
    try {
        db::Conn con;
        hist = con.query("SELECT COALESCE(anime_id, anime_name), anime_name, COUNT(DISTINCT episode), MAX(timestamp) "
                         "FROM history GROUP BY COALESCE(anime_id, anime_name) ORDER BY MAX(timestamp) DESC");
        for (auto& r : con.query("SELECT DISTINCT date(timestamp) FROM history ORDER BY 1")) days.push_back(r.str(0));
        auto pd = con.query("SELECT MAX(c) FROM (SELECT COUNT(DISTINCT anime_id || ':' || episode) c FROM history GROUP BY date(timestamp))");
        per_day = pd.empty() ? 0 : pd[0].i64(0);
        night = con.query("SELECT COUNT(*) FROM history WHERE CAST(strftime('%H', timestamp, 'localtime') AS INTEGER) < 5")[0].i64(0);
        chapters = con.query("SELECT COUNT(*) FROM manga_read")[0].i64(0);
        for (auto& r : con.query("SELECT anime_id, total FROM anime_total")) totals[r.str(0)] = r.i64(1);
    } catch (...) {
        return {};
    }
    long long episodes = 0;
    for (auto& r : hist) episodes += r.i64(2);
    long long streak = 0, best = 0;
    long prev = -1;
    for (auto& d : days) {
        long cur = days_of(d);
        if (cur < 0) continue;
        streak = (prev >= 0 && cur - prev == 1) ? streak + 1 : 1;
        best = std::max(best, streak);
        prev = cur;
    }
    std::vector<Item> g = tiers("ep", episodes, TOTAL_TIERS);
    for (auto& x : tiers("series", (long long)hist.size(), SERIES_TIERS)) g.push_back(x);
    for (auto& x : tiers("manga", chapters, MANGA_TIERS)) g.push_back(x);
    for (auto [n, t, d] : std::vector<std::tuple<long long, const char*, const char*>>{
             {2, "BACK AGAIN", "watch on 2 days in a row"}, {3, "ROUTINE", "watch on 3 days in a row"}, {7, "WEEK-LONG ARC", "watch 7 days in a row"}})
        g.push_back({"streak:" + std::to_string(n), t, d, std::min(best, n), n, best >= n});
    g.push_back({"marathon:5", "MARATHON", "watch 5 episodes in one day", std::min(per_day, 5LL), 5, per_day >= 5});
    g.push_back({"marathon:12", "WEEKEND IS GONE", "watch 12 episodes in one day", std::min(per_day, 12LL), 12, per_day >= 12});
    g.push_back({"night:1", "NIGHT OWL", "watch something after midnight", std::min(night, 1LL), 1, night >= 1});
    groups.push_back({"EVERYTHING YOU WATCH", g});
    for (size_t i = 0; i < hist.size() && i < 30; i++) {
        std::string aid = hist[i].str(0), name = hist[i].str(1);
        long long n = hist[i].i64(2);
        auto items = tiers("a:" + aid, n, ANIME_TIERS, name);
        long long tot = totals.count(aid) ? totals[aid] : 0;
        if (tot >= 3)
            items.push_back({"a:" + aid + ":done", "COMPLETIONIST", "finish all " + std::to_string(tot) + " episodes of " + name, std::min(n, tot), tot, n >= tot});
        groups.push_back({name, items});
    }
    return groups;
}

std::set<std::string> unlocked() {
    std::set<std::string> out;
    try {
        db::Conn con;
        for (auto& r : con.query("SELECT key FROM advancement")) out.insert(r.str(0));
    } catch (...) {
    }
    return out;
}

std::vector<Item> game_items() {
    auto have = unlocked();
    std::vector<Item> out;
    for (auto& [k, t, d] : GAME_ADV) {
        bool done = have.count("game:" + k) > 0;
        out.push_back({"game:" + k, t, d, done ? 1 : 0, 1, done});
    }
    return out;
}

static void store(const std::vector<std::string>& keys) {
    try {
        db::Conn con;
        for (auto& k : keys) con.exec("INSERT OR IGNORE INTO advancement (key) VALUES (?)", {k});
    } catch (...) {
    }
}

Tracker::Tracker() : have_(unlocked()), fresh_(have_.empty()) {}

std::vector<Toast> Tracker::refresh() {
    cache_ = compute();
    std::vector<std::tuple<std::string, std::string, std::string, std::string>> fresh;
    auto all = cache_;
    all.push_back({"ARCADE", game_items()});
    for (auto& grp : all)
        for (auto& it : grp.items)
            if (it.done && !have_.count(it.key)) {
                have_.insert(it.key);
                fresh.emplace_back(it.title, it.desc, grp.group, it.key);
            }
    if (!fresh.empty()) {
        std::vector<std::string> keys;
        for (auto& f : fresh) keys.push_back(std::get<3>(f));
        store(keys);
    }
    if (fresh_) {
        fresh_ = false;
        return {};
    }
    std::vector<Toast> out;
    for (auto& f : fresh) out.emplace_back(std::get<0>(f), std::get<1>(f), std::get<2>(f));
    return out;
}

std::optional<Toast> Tracker::award(const std::string& key) {
    std::string full = "game:" + key;
    if (have_.count(full)) return std::nullopt;
    for (auto& [k, t, d] : GAME_ADV)
        if (k == key) {
            have_.insert(full);
            store({full});
            return Toast{t, d, "ARCADE"};
        }
    return std::nullopt;
}

std::vector<Group> Tracker::groups() {
    if (cache_.empty()) cache_ = compute();
    auto out = cache_;
    out.push_back({"ARCADE", game_items()});
    return out;
}

std::pair<int, int> Tracker::counts() {
    int done = 0, total = 0;
    for (auto& g : groups())
        for (auto& it : g.items) {
            total++;
            done += it.done ? 1 : 0;
        }
    return {done, total};
}
}  // namespace advance
