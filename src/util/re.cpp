#define PCRE2_CODE_UNIT_WIDTH 8
#define PCRE2_STATIC
#include "re.hpp"

#include <pcre2.h>

#include <cstdio>
#include <stdexcept>

namespace re {
Regex::Regex(const std::string& pattern, unsigned flags) {
    uint32_t opts = PCRE2_UTF | PCRE2_UCP | PCRE2_MATCH_INVALID_UTF;
    if (flags & I) opts |= PCRE2_CASELESS;
    if (flags & S) opts |= PCRE2_DOTALL;
    if (flags & M) opts |= PCRE2_MULTILINE;
    int err = 0;
    PCRE2_SIZE off = 0;
    code_ = pcre2_compile((PCRE2_SPTR)pattern.c_str(), pattern.size(), opts, &err, &off, nullptr);
    if (!code_) {
        PCRE2_UCHAR msg[256];
        pcre2_get_error_message(err, msg, sizeof msg);
        std::fprintf(stderr, "regex compile error at %zu in /%s/: %s\n", (size_t)off, pattern.c_str(), (const char*)msg);
        return;
    }
    pcre2_jit_compile((pcre2_code*)code_, PCRE2_JIT_COMPLETE);  // a no-op where JIT is unavailable
}

Regex::~Regex() {
    if (code_) pcre2_code_free((pcre2_code*)code_);
}

std::optional<Match> Regex::exec(const std::string& s, size_t pos, bool anchored) const {
    if (!code_ || pos > s.size()) return std::nullopt;
    pcre2_match_data* md = pcre2_match_data_create_from_pattern((pcre2_code*)code_, nullptr);
    int rc = pcre2_match((pcre2_code*)code_, (PCRE2_SPTR)s.data(), s.size(), pos, anchored ? PCRE2_ANCHORED : 0, md, nullptr);
    if (rc < 0) {
        pcre2_match_data_free(md);
        return std::nullopt;
    }
    uint32_t n = 0;
    pcre2_pattern_info((pcre2_code*)code_, PCRE2_INFO_CAPTURECOUNT, &n);
    PCRE2_SIZE* ov = pcre2_get_ovector_pointer(md);
    Match m;
    for (uint32_t i = 0; i <= n; i++) {
        if ((int)i < rc && ov[2 * i] != PCRE2_UNSET) {
            m.spans.emplace_back((long)ov[2 * i], (long)ov[2 * i + 1]);
            m.groups.push_back(s.substr(ov[2 * i], ov[2 * i + 1] - ov[2 * i]));
        } else {
            m.spans.emplace_back(-1, -1);
            m.groups.emplace_back();
        }
    }
    pcre2_match_data_free(md);
    return m;
}

std::optional<Match> Regex::search(const std::string& s, size_t pos) const { return exec(s, pos, false); }
std::optional<Match> Regex::match(const std::string& s) const { return exec(s, 0, true); }

std::vector<Match> Regex::finditer(const std::string& s) const {
    std::vector<Match> out;
    size_t pos = 0;
    while (pos <= s.size()) {
        auto m = exec(s, pos, false);
        if (!m) break;
        size_t st = (size_t)m->start(), en = (size_t)m->end();
        out.push_back(std::move(*m));
        if (en == st) {
            // empty match: step one UTF-8 character forward
            pos = en + 1;
            while (pos < s.size() && ((unsigned char)s[pos] & 0xC0) == 0x80) pos++;
        } else {
            pos = en;
        }
    }
    return out;
}

static std::string expand(const std::string& repl, const Match& m) {
    std::string out;
    for (size_t i = 0; i < repl.size(); i++) {
        char c = repl[i];
        if (c == '\\' && i + 1 < repl.size()) {
            char d = repl[i + 1];
            if (d >= '0' && d <= '9') {
                size_t j = i + 1;
                int g = 0;
                while (j < repl.size() && j < i + 3 && repl[j] >= '0' && repl[j] <= '9') g = g * 10 + (repl[j++] - '0');
                out += m.group((size_t)g);
                i = j - 1;
                continue;
            }
            if (d == 'g' && i + 2 < repl.size() && repl[i + 2] == '<') {
                size_t close = repl.find('>', i + 3);
                if (close != std::string::npos) {
                    out += m.group((size_t)std::stoi(repl.substr(i + 3, close - i - 3)));
                    i = close;
                    continue;
                }
            }
            if (d == 'n') { out += '\n'; i++; continue; }
            if (d == '\\') { out += '\\'; i++; continue; }
        }
        out += c;
    }
    return out;
}

std::string Regex::sub_fn(const std::function<std::string(const Match&)>& fn, const std::string& s, int count) const {
    std::string out;
    size_t last = 0;
    int n = 0;
    for (const auto& m : finditer(s)) {
        if (count && n >= count) break;
        out.append(s, last, (size_t)m.start() - last);
        out += fn(m);
        last = (size_t)m.end();
        n++;
    }
    out.append(s, last, std::string::npos);
    return out;
}

std::string Regex::sub(const std::string& repl, const std::string& s, int count) const {
    return sub_fn([&](const Match& m) { return expand(repl, m); }, s, count);
}

std::vector<std::string> findall1(const Regex& r, const std::string& s) {
    std::vector<std::string> out;
    for (const auto& m : r.finditer(s)) out.push_back(m.groups.size() > 1 ? m.group(1) : m.group(0));
    return out;
}
}  // namespace re
