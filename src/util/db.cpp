#include "db.hpp"

#include <sqlite3.h>

#include <algorithm>

#include "paths.hpp"

namespace db {
std::string WATCH_DB = paths::watch_db();

std::string Row::str(size_t i) const {
    if (i >= v.size()) return "";
    if (auto p = std::get_if<std::string>(&v[i])) return *p;
    if (auto p = std::get_if<long long>(&v[i])) return std::to_string(*p);
    if (auto p = std::get_if<double>(&v[i])) {
        std::string s = std::to_string(*p);
        return s;
    }
    return "";
}
long long Row::i64(size_t i) const {
    if (i >= v.size()) return 0;
    if (auto p = std::get_if<long long>(&v[i])) return *p;
    if (auto p = std::get_if<double>(&v[i])) return (long long)*p;
    if (auto p = std::get_if<std::string>(&v[i])) return std::atoll(p->c_str());
    return 0;
}
double Row::f(size_t i) const {
    if (i >= v.size()) return 0;
    if (auto p = std::get_if<double>(&v[i])) return *p;
    if (auto p = std::get_if<long long>(&v[i])) return (double)*p;
    if (auto p = std::get_if<std::string>(&v[i])) return std::atof(p->c_str());
    return 0;
}

static void bind_all(sqlite3_stmt* st, const std::vector<Val>& args) {
    for (size_t i = 0; i < args.size(); i++) {
        int k = (int)i + 1;
        const Val& a = args[i];
        if (std::holds_alternative<std::nullptr_t>(a)) sqlite3_bind_null(st, k);
        else if (auto p = std::get_if<long long>(&a)) sqlite3_bind_int64(st, k, *p);
        else if (auto p = std::get_if<double>(&a)) sqlite3_bind_double(st, k, *p);
        else {
            const auto& s = std::get<std::string>(a);
            sqlite3_bind_text(st, k, s.c_str(), (int)s.size(), SQLITE_TRANSIENT);
        }
    }
}

Conn::Conn() {
    sqlite3* d = nullptr;
    if (sqlite3_open_v2(WATCH_DB.c_str(), &d, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
        std::string msg = d ? sqlite3_errmsg(d) : "cannot open database";
        if (d) sqlite3_close(d);
        throw Error(msg);
    }
    db_ = d;
    sqlite3_busy_timeout(d, 5000);
    exec("CREATE TABLE IF NOT EXISTS history( id INTEGER PRIMARY KEY AUTOINCREMENT, anime_name TEXT, "
         "episode TEXT, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP )");
    auto has = [&](const std::string& t, const std::string& c) {
        auto cols = columns(t);
        return std::find(cols.begin(), cols.end(), c) != cols.end();
    };
    if (!has("history", "anime_id")) exec("ALTER TABLE history ADD COLUMN anime_id TEXT");
    exec("CREATE TABLE IF NOT EXISTS manga_read( manga_id TEXT, chapter TEXT, title TEXT, cover TEXT, "
         "ts DATETIME DEFAULT CURRENT_TIMESTAMP, PRIMARY KEY (manga_id, chapter) )");
    if (!has("manga_read", "title_jp")) exec("ALTER TABLE manga_read ADD COLUMN title_jp TEXT");
    exec("CREATE TABLE IF NOT EXISTS shelf_override( kind TEXT, item_id TEXT, section TEXT, slot INTEGER, PRIMARY KEY (kind, item_id) )");
    exec("CREATE TABLE IF NOT EXISTS manga_plan( manga_id TEXT PRIMARY KEY, title TEXT, cover TEXT, title_jp TEXT, "
         "ts DATETIME DEFAULT CURRENT_TIMESTAMP )");
    exec("CREATE TABLE IF NOT EXISTS shelf_hidden( kind TEXT, item_id TEXT, title TEXT, PRIMARY KEY (kind, item_id) )");
    if (!has("shelf_hidden", "title")) exec("ALTER TABLE shelf_hidden ADD COLUMN title TEXT");
    if (!has("shelf_hidden", "purged")) exec("ALTER TABLE shelf_hidden ADD COLUMN purged INTEGER DEFAULT 0");
    exec("CREATE TABLE IF NOT EXISTS anime_total( anime_id TEXT PRIMARY KEY, total INTEGER )");
    exec("CREATE TABLE IF NOT EXISTS resume( anime_id TEXT PRIMARY KEY, title TEXT, episode TEXT, position REAL DEFAULT 0, "
         "duration REAL DEFAULT 0, poster TEXT DEFAULT '', ts DATETIME DEFAULT CURRENT_TIMESTAMP )");
    exec("CREATE TABLE IF NOT EXISTS advancement( key TEXT PRIMARY KEY, unlocked DATETIME DEFAULT CURRENT_TIMESTAMP )");
    exec("CREATE TABLE IF NOT EXISTS watchlist( anime_id TEXT PRIMARY KEY, anime_name TEXT, poster TEXT, "
         "added DATETIME DEFAULT CURRENT_TIMESTAMP )");
    exec("BEGIN");
}

Conn::~Conn() {
    if (db_) {
        if (!done_) sqlite3_exec((sqlite3*)db_, "COMMIT", nullptr, nullptr, nullptr);
        sqlite3_close_v2((sqlite3*)db_);
    }
}

void Conn::rollback() {
    if (db_ && !done_) {
        sqlite3_exec((sqlite3*)db_, "ROLLBACK", nullptr, nullptr, nullptr);
        done_ = true;
    }
}

std::vector<std::string> Conn::columns(const std::string& table) {
    std::vector<std::string> out;
    for (auto& r : query("PRAGMA table_info(" + table + ")")) out.push_back(r.str(1));
    return out;
}

std::vector<Row> Conn::query(const std::string& sql, const std::vector<Val>& args) {
    sqlite3_stmt* st = nullptr;
    sqlite3* d = (sqlite3*)db_;
    if (sqlite3_prepare_v2(d, sql.c_str(), (int)sql.size(), &st, nullptr) != SQLITE_OK) throw Error(sqlite3_errmsg(d));
    bind_all(st, args);
    std::vector<Row> rows;
    int rc;
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        Row r;
        int n = sqlite3_column_count(st);
        for (int i = 0; i < n; i++) {
            switch (sqlite3_column_type(st, i)) {
                case SQLITE_INTEGER: r.v.emplace_back((long long)sqlite3_column_int64(st, i)); break;
                case SQLITE_FLOAT: r.v.emplace_back(sqlite3_column_double(st, i)); break;
                case SQLITE_NULL: r.v.emplace_back(nullptr); break;
                default: {
                    const unsigned char* t = sqlite3_column_text(st, i);
                    r.v.emplace_back(std::string(t ? (const char*)t : "", (size_t)sqlite3_column_bytes(st, i)));
                }
            }
        }
        rows.push_back(std::move(r));
    }
    if (rc != SQLITE_DONE) {
        std::string msg = sqlite3_errmsg(d);
        sqlite3_finalize(st);
        throw Error(msg);
    }
    sqlite3_finalize(st);
    return rows;
}

void Conn::exec(const std::string& sql, const std::vector<Val>& args) { query(sql, args); }

long long Conn::total_changes() { return sqlite3_total_changes((sqlite3*)db_); }
}  // namespace db
