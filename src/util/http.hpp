// HTTP on libcurl, shaped like the httpx calls of the Python version. Safe to call from any thread.
#pragma once
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace http {
using KV = std::vector<std::pair<std::string, std::string>>;

enum class Jar { NONE, SCRAPE, MANGA };  // shared cookie jars: the scraper's client and the manga sites' client

struct Req {
    std::string url;
    std::string method = "GET";
    KV params;        // appended to the url as a query string (repeated keys allowed)
    KV headers;
    std::string body;  // POST body (set content-type in headers)
    double timeout = 15;
    bool follow = true;
    size_t max_bytes = 0;  // stop reading after this many bytes (stream probes)
    Jar jar = Jar::NONE;
};

struct Resp {
    long status = 0;
    std::string body;
    std::map<std::string, std::string> headers;  // lower-case names
    std::string url;                             // final url after redirects
    std::string header(const std::string& name) const {
        auto it = headers.find(name);
        return it == headers.end() ? "" : it->second;
    }
};

struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

void init();  // call once at start-up (curl_global_init)
Resp request(const Req& r);  // throws Error on transport failure (not on HTTP status)
std::string with_params(const std::string& url, const KV& params);
std::string json_post_body(const std::string& json);
}  // namespace http
