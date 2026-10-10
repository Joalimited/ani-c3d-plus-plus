// String helpers that mirror the Python built-ins the original code leans on.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace su {
std::string lower(std::string s);
std::string upper(std::string s);
std::string strip(const std::string& s, const std::string& chars = " \t\r\n\f\v");
std::string rstrip(const std::string& s, const std::string& chars = " \t\r\n\f\v");
std::string lstrip(const std::string& s, const std::string& chars = " \t\r\n\f\v");
std::string replace(std::string s, const std::string& from, const std::string& to);
std::vector<std::string> split(const std::string& s, const std::string& sep, int maxsplit = -1);
std::vector<std::string> split_ws(const std::string& s);  // str.split() with no argument
std::vector<std::string> splitlines(const std::string& s);
std::string join(const std::vector<std::string>& v, const std::string& sep);
bool starts_with(const std::string& s, const std::string& p);
bool ends_with(const std::string& s, const std::string& p);
bool contains(const std::string& s, const std::string& p);
std::string title_case(const std::string& s);  // str.title()
std::string url_quote(const std::string& s, const std::string& safe = "/");
std::string url_quote_plus(const std::string& s);
std::string url_unquote(const std::string& s);
std::string html_unescape(const std::string& s);
bool is_ascii(const std::string& s);
std::string fmt(const char* f, ...);
std::string fmt_time(double sec);  // core.fmt: m:ss / h:mm:ss
std::string py_float(double v);    // str(float) style: 1.0, 12.5
bool to_double(const std::string& s, double& out);  // float(s) with Python's leniency for whitespace
bool is_digit_str(const std::string& s);
// UTF-8 aware helpers
std::vector<uint32_t> utf8_decode(const std::string& s);
std::string utf8_encode(const std::vector<uint32_t>& cps);
size_t utf8_len(const std::string& s);
std::string utf8_substr(const std::string& s, size_t start, size_t count = std::string::npos);
std::string base64_decode(const std::string& in);
std::string sha1_hex(const std::string& data);
std::string md5_hex(const std::string& data);
bool gunzip(const std::string& in, std::string& out);
std::string json_quote(const std::string& s);
}  // namespace su
