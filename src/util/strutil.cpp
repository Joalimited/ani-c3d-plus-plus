#include "strutil.hpp"

#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

namespace su {
std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
std::string upper(std::string s) {
    for (auto& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}
std::string strip(const std::string& s, const std::string& chars) { return lstrip(rstrip(s, chars), chars); }
std::string rstrip(const std::string& s, const std::string& chars) {
    size_t e = s.find_last_not_of(chars);
    return e == std::string::npos ? "" : s.substr(0, e + 1);
}
std::string lstrip(const std::string& s, const std::string& chars) {
    size_t b = s.find_first_not_of(chars);
    return b == std::string::npos ? "" : s.substr(b);
}
std::string replace(std::string s, const std::string& from, const std::string& to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}
std::vector<std::string> split(const std::string& s, const std::string& sep, int maxsplit) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        if (maxsplit >= 0 && (int)out.size() >= maxsplit) break;
        size_t p = s.find(sep, start);
        if (p == std::string::npos) break;
        out.push_back(s.substr(start, p - start));
        start = p + sep.size();
    }
    out.push_back(s.substr(start));
    return out;
}
std::vector<std::string> split_ws(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && std::isspace((unsigned char)s[i])) i++;
        size_t j = i;
        while (j < s.size() && !std::isspace((unsigned char)s[j])) j++;
        if (j > i) out.push_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}
std::vector<std::string> splitlines(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\n' || s[i] == '\r') {
            out.push_back(s.substr(start, i - start));
            if (s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n') i++;
            start = i + 1;
        }
    }
    if (start < s.size()) out.push_back(s.substr(start));
    return out;
}
std::string join(const std::vector<std::string>& v, const std::string& sep) {
    std::string out;
    for (size_t i = 0; i < v.size(); i++) {
        if (i) out += sep;
        out += v[i];
    }
    return out;
}
bool starts_with(const std::string& s, const std::string& p) { return s.size() >= p.size() && s.compare(0, p.size(), p) == 0; }
bool ends_with(const std::string& s, const std::string& p) { return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0; }
bool contains(const std::string& s, const std::string& p) { return s.find(p) != std::string::npos; }

std::string title_case(const std::string& s) {
    std::string out = s;
    bool prev_alpha = false;
    for (auto& c : out) {
        bool a = std::isalpha((unsigned char)c);
        if (a) c = prev_alpha ? (char)std::tolower((unsigned char)c) : (char)std::toupper((unsigned char)c);
        prev_alpha = a;
    }
    return out;
}

std::string url_quote(const std::string& s, const std::string& safe) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '_' || c == '.' || c == '-' || c == '~' || safe.find((char)c) != std::string::npos) {
            out += (char)c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}
std::string url_quote_plus(const std::string& s) { return replace(url_quote(s, " "), " ", "+"); }
std::string url_unquote(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size() && std::isxdigit((unsigned char)s[i + 1]) && std::isxdigit((unsigned char)s[i + 2])) {
            out += (char)std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

static void put_utf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) out += (char)cp;
    else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
    else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
}

std::string html_unescape(const std::string& s) {
    static const std::unordered_map<std::string, uint32_t> named = {
        {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", 0xA0}, {"hellip", 0x2026},
        {"mdash", 0x2014}, {"ndash", 0x2013}, {"rsquo", 0x2019}, {"lsquo", 0x2018}, {"rdquo", 0x201D}, {"ldquo", 0x201C},
        {"eacute", 0xE9}, {"egrave", 0xE8}, {"aacute", 0xE1}, {"agrave", 0xE0}, {"oacute", 0xF3}, {"uacute", 0xFA}, {"iacute", 0xED},
        {"ouml", 0xF6}, {"uuml", 0xFC}, {"auml", 0xE4}, {"Eacute", 0xC9}, {"ccedil", 0xE7}, {"ntilde", 0xF1}, {"times", 0xD7},
        {"copy", 0xA9}, {"reg", 0xAE}, {"trade", 0x2122}, {"deg", 0xB0}, {"middot", 0xB7}, {"bull", 0x2022}, {"star", 0x2606},
        {"frac12", 0xBD}, {"sup2", 0xB2}, {"laquo", 0xAB}, {"raquo", 0xBB}, {"ocirc", 0xF4}, {"ecirc", 0xEA}, {"acirc", 0xE2},
        {"ucirc", 0xFB}, {"icirc", 0xEE}, {"euml", 0xEB}, {"iuml", 0xEF}, {"szlig", 0xDF}, {"oslash", 0xF8}, {"aring", 0xE5},
    };
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] != '&') { out += s[i]; continue; }
        size_t semi = s.find(';', i + 1);
        if (semi == std::string::npos || semi - i > 12) { out += s[i]; continue; }
        std::string ent = s.substr(i + 1, semi - i - 1);
        uint32_t cp = 0;
        bool ok = false;
        if (!ent.empty() && ent[0] == '#') {
            char* end = nullptr;
            if (ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X')) cp = (uint32_t)std::strtoul(ent.c_str() + 2, &end, 16);
            else cp = (uint32_t)std::strtoul(ent.c_str() + 1, &end, 10);
            ok = end && *end == 0 && cp > 0;
        } else {
            auto it = named.find(ent);
            if (it != named.end()) { cp = it->second; ok = true; }
        }
        if (ok) {
            put_utf8(out, cp);
            i = semi;
        } else {
            out += s[i];
        }
    }
    return out;
}

bool is_ascii(const std::string& s) {
    for (unsigned char c : s)
        if (c >= 0x80) return false;
    return true;
}

std::string fmt(const char* f, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, f);
    int n = std::vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    if (n < (int)sizeof buf) return std::string(buf, n < 0 ? 0 : n);
    std::string big(n + 1, '\0');
    va_start(ap, f);
    std::vsnprintf(big.data(), big.size(), f, ap);
    va_end(ap);
    big.resize(n);
    return big;
}

std::string fmt_time(double sec) {
    long s = (long)(sec > 0 ? sec : 0);
    long h = s / 3600, rem = s % 3600, m = rem / 60;
    s = rem % 60;
    return h ? fmt("%ld:%02ld:%02ld", h, m, s) : fmt("%02ld:%02ld", m, s);
}

std::string py_float(double v) {
    if (v == std::floor(v) && std::fabs(v) < 1e15) return fmt("%.1f", v);
    std::string s = fmt("%.12g", v);
    return s;
}

bool to_double(const std::string& s, double& out) {
    std::string t = strip(s);
    if (t.empty()) return false;
    char* end = nullptr;
    out = std::strtod(t.c_str(), &end);
    return end && *end == 0 && std::isfinite(out);
}

bool is_digit_str(const std::string& s) {
    if (s.empty()) return false;
    for (unsigned char c : s)
        if (!std::isdigit(c)) return false;
    return true;
}

std::vector<uint32_t> utf8_decode(const std::string& s) {
    std::vector<uint32_t> out;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        uint32_t cp;
        int n;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c >> 5) == 6) { cp = c & 0x1F; n = 2; }
        else if ((c >> 4) == 14) { cp = c & 0x0F; n = 3; }
        else if ((c >> 3) == 30) { cp = c & 0x07; n = 4; }
        else { out.push_back(0xFFFD); i++; continue; }
        if (i + n > s.size()) { out.push_back(0xFFFD); break; }
        bool bad = false;
        for (int k = 1; k < n; k++) {
            unsigned char cc = (unsigned char)s[i + k];
            if ((cc >> 6) != 2) { bad = true; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (bad) { out.push_back(0xFFFD); i++; continue; }
        out.push_back(cp);
        i += n;
    }
    return out;
}
std::string utf8_encode(const std::vector<uint32_t>& cps) {
    std::string out;
    for (auto cp : cps) put_utf8(out, cp);
    return out;
}
size_t utf8_len(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) n++;
    return n;
}
std::string utf8_substr(const std::string& s, size_t start, size_t count) {
    auto cps = utf8_decode(s);
    if (start >= cps.size()) return "";
    size_t end = (count == std::string::npos || start + count > cps.size()) ? cps.size() : start + count;
    return utf8_encode(std::vector<uint32_t>(cps.begin() + (long)start, cps.begin() + (long)end));
}

std::string base64_decode(const std::string& in) {
    static int T[256];
    static bool init = false;
    if (!init) {
        for (int& v : T) v = -1;
        const char* a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; i++) T[(unsigned char)a[i]] = i;
        T[(unsigned char)'-'] = 62;
        T[(unsigned char)'_'] = 63;
        init = true;
    }
    std::string out;
    int val = 0, bits = -8;
    for (unsigned char c : in) {
        if (T[c] == -1) continue;
        val = (val << 6) + T[c];
        bits += 6;
        if (bits >= 0) {
            out += (char)((val >> bits) & 0xFF);
            bits -= 8;
        }
    }
    return out;
}

// ---------------------------------------------------------------- SHA-1 (cache file names, same as hashlib.sha1)
std::string sha1_hex(const std::string& data) {
    uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;
    std::string msg = data;
    uint64_t ml = (uint64_t)data.size() * 8;
    msg += (char)0x80;
    while (msg.size() % 64 != 56) msg += (char)0;
    for (int i = 7; i >= 0; i--) msg += (char)((ml >> (i * 8)) & 0xFF);
    auto rol = [](uint32_t v, int n) { return (v << n) | (v >> (32 - n)); };
    for (size_t off = 0; off < msg.size(); off += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++)
            w[i] = ((uint32_t)(unsigned char)msg[off + i * 4] << 24) | ((uint32_t)(unsigned char)msg[off + i * 4 + 1] << 16) |
                   ((uint32_t)(unsigned char)msg[off + i * 4 + 2] << 8) | (uint32_t)(unsigned char)msg[off + i * 4 + 3];
        for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            uint32_t tmp = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = tmp;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }
    return fmt("%08x%08x%08x%08x%08x", h0, h1, h2, h3, h4);
}

// ---------------------------------------------------------------- MD5
std::string md5_hex(const std::string& data) {
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1,
        0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453,
        0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942,
        0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
        0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d,
        0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    static const int R[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
                              4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    uint32_t a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;
    std::string msg = data;
    uint64_t ml = (uint64_t)data.size() * 8;
    msg += (char)0x80;
    while (msg.size() % 64 != 56) msg += (char)0;
    for (int i = 0; i < 8; i++) msg += (char)((ml >> (i * 8)) & 0xFF);
    for (size_t off = 0; off < msg.size(); off += 64) {
        uint32_t M[16];
        for (int i = 0; i < 16; i++)
            M[i] = (uint32_t)(unsigned char)msg[off + i * 4] | ((uint32_t)(unsigned char)msg[off + i * 4 + 1] << 8) |
                   ((uint32_t)(unsigned char)msg[off + i * 4 + 2] << 16) | ((uint32_t)(unsigned char)msg[off + i * 4 + 3] << 24);
        uint32_t A = a0, B = b0, C = c0, D = d0;
        for (int i = 0; i < 64; i++) {
            uint32_t F;
            int g;
            if (i < 16) { F = (B & C) | (~B & D); g = i; }
            else if (i < 32) { F = (D & B) | (~D & C); g = (5 * i + 1) % 16; }
            else if (i < 48) { F = B ^ C ^ D; g = (3 * i + 5) % 16; }
            else { F = C ^ (B | ~D); g = (7 * i) % 16; }
            F = F + A + K[i] + M[g];
            A = D; D = C; C = B;
            B = B + ((F << R[i]) | (F >> (32 - R[i])));
        }
        a0 += A; b0 += B; c0 += C; d0 += D;
    }
    std::string out;
    for (uint32_t v : {a0, b0, c0, d0})
        for (int i = 0; i < 4; i++) out += fmt("%02x", (v >> (i * 8)) & 0xFF);
    return out;
}

bool gunzip(const std::string& in, std::string& out) {
    z_stream zs{};
    if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK) return false;
    zs.next_in = (Bytef*)in.data();
    zs.avail_in = (uInt)in.size();
    char buf[65536];
    int ret;
    out.clear();
    do {
        zs.next_out = (Bytef*)buf;
        zs.avail_out = sizeof buf;
        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            inflateEnd(&zs);
            return false;
        }
        out.append(buf, sizeof buf - zs.avail_out);
    } while (ret != Z_STREAM_END);
    inflateEnd(&zs);
    return true;
}

std::string json_quote(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) out += fmt("\\u%04x", c);
                else out += (char)c;
        }
    }
    return out + "\"";
}
}  // namespace su
