// The watch-history database (the same sqlite file ani-cli and ani-3d use). Each Conn is one connection inside one
// transaction, committed when it goes out of scope (like Python's `with db_connect() as con:`).
#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace db {
using Val = std::variant<std::nullptr_t, long long, double, std::string>;

struct Row {
    std::vector<Val> v;
    bool null(size_t i) const { return i >= v.size() || std::holds_alternative<std::nullptr_t>(v[i]); }
    std::string str(size_t i) const;  // "" for NULL
    long long i64(size_t i) const;
    double f(size_t i) const;
};

struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

extern std::string WATCH_DB;  // path; tests point it at a copy

class Conn {
public:
    Conn();  // opens WATCH_DB, creates / migrates every table, begins a transaction (throws db::Error)
    ~Conn();
    Conn(const Conn&) = delete;
    Conn& operator=(const Conn&) = delete;
    std::vector<Row> query(const std::string& sql, const std::vector<Val>& args = {});
    void exec(const std::string& sql, const std::vector<Val>& args = {});
    long long total_changes();
    void rollback();

private:
    void* db_ = nullptr;
    bool done_ = false;
    std::vector<std::string> columns(const std::string& table);
};
}  // namespace db
