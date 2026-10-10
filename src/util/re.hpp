// Python-`re`-like regular expressions on PCRE2 (UTF-8, Unicode classes like Python 3 str patterns). Patterns compile
// once (keep them `static`) and are safe to use from several threads.
#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace re {
enum Flags : unsigned { NONE = 0, I = 1, S = 2, M = 4 };

struct Match {
    std::vector<std::string> groups;               // [0] = whole match
    std::vector<std::pair<long, long>> spans;      // byte offsets, -1 when the group did not take part
    const std::string& group(size_t i = 0) const { static const std::string empty; return i < groups.size() ? groups[i] : empty; }
    bool has(size_t i) const { return i < spans.size() && spans[i].first >= 0; }
    long start(size_t i = 0) const { return spans[i].first; }
    long end(size_t i = 0) const { return spans[i].second; }
};

class Regex {
public:
    explicit Regex(const std::string& pattern, unsigned flags = NONE);
    ~Regex();
    Regex(const Regex&) = delete;
    Regex& operator=(const Regex&) = delete;
    std::optional<Match> search(const std::string& s, size_t pos = 0) const;
    std::optional<Match> match(const std::string& s) const;  // anchored at the start
    std::vector<Match> finditer(const std::string& s) const;
    // re.sub: replacement may use \1..\99 and \g<n>; count=0 means all
    std::string sub(const std::string& repl, const std::string& s, int count = 0) const;
    std::string sub_fn(const std::function<std::string(const Match&)>& fn, const std::string& s, int count = 0) const;
    bool ok() const { return code_ != nullptr; }

private:
    void* code_ = nullptr;  // pcre2_code_8*
    std::optional<Match> exec(const std::string& s, size_t pos, bool anchored) const;
};

// convenience for one-off use
inline std::optional<Match> search(const Regex& r, const std::string& s) { return r.search(s); }
// findall-style: group 1 of each match (or the whole match when the pattern has no groups)
std::vector<std::string> findall1(const Regex& r, const std::string& s);
}  // namespace re
