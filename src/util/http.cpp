#include "http.hpp"
#include "paths.hpp"
#include "resources.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <mutex>

#include "strutil.hpp"

namespace http {
namespace {
struct Shared {
    CURLSH* sh = nullptr;
    std::mutex locks[CURL_LOCK_DATA_LAST];
};
Shared g_jars[3];
std::once_flag g_once;

void lock_cb(CURL*, curl_lock_data data, curl_lock_access, void* user) { ((Shared*)user)->locks[data].lock(); }
void unlock_cb(CURL*, curl_lock_data data, void* user) { ((Shared*)user)->locks[data].unlock(); }

struct Sink {
    std::string* body;
    size_t max;
};
size_t write_cb(char* ptr, size_t size, size_t n, void* user) {
    Sink* s = (Sink*)user;
    size_t len = size * n;
    if (s->max && s->body->size() + len > s->max) {
        s->body->append(ptr, s->max - s->body->size());
        return 0;  // abort the transfer: we have what we came for
    }
    s->body->append(ptr, len);
    return len;
}
size_t header_cb(char* ptr, size_t size, size_t n, void* user) {
    auto* h = (std::map<std::string, std::string>*)user;
    std::string line(ptr, size * n);
    if (su::starts_with(line, "HTTP/")) h->clear();  // a redirect: keep only the final response's headers
    size_t c = line.find(':');
    if (c != std::string::npos) h->emplace(su::lower(su::strip(line.substr(0, c))), su::strip(line.substr(c + 1)));
    return size * n;
}
}  // namespace

void init() {
    std::call_once(g_once, [] {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        for (auto& j : g_jars) {
            j.sh = curl_share_init();
            curl_share_setopt(j.sh, CURLSHOPT_LOCKFUNC, lock_cb);
            curl_share_setopt(j.sh, CURLSHOPT_UNLOCKFUNC, unlock_cb);
            curl_share_setopt(j.sh, CURLSHOPT_USERDATA, &j);
            curl_share_setopt(j.sh, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE);
            curl_share_setopt(j.sh, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
        }
    });
}

std::string with_params(const std::string& url, const KV& params) {
    if (params.empty()) return url;
    std::string q;
    for (const auto& [k, v] : params) {
        if (!q.empty()) q += '&';
        q += su::url_quote(k, "[]") + "=" + su::url_quote(v, "");
    }
    return url + (url.find('?') == std::string::npos ? "?" : "&") + q;
}

std::string json_post_body(const std::string& json) { return json; }

Resp request(const Req& r) {
    init();
    thread_local CURL* c = nullptr;
    if (!c) c = curl_easy_init();
    else curl_easy_reset(c);
    Resp out;
    std::string url = with_params(r.url, r.params);
    Sink sink{&out.body, r.max_bytes};
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, r.follow ? 1L : 0L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 20L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, (long)(r.timeout * 1000));
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, (long)(std::min(r.timeout, 15.0) * 1000));
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, header_cb);
    curl_easy_setopt(c, CURLOPT_HEADERDATA, &out.headers);
#ifdef __ANDROID__
    {  // Mozilla's CA bundle from the APK (the device's own store is old on a Fire Stick and OpenSSL rejects some chains)
        static std::string ca = [] {
            std::string pem, out = paths::join(paths::cache_dir(), "cacert.pem");
            if (res::get("cacert.pem", pem)) {
                paths::makedirs(paths::cache_dir());
                paths::write_file(out, pem);
                return out;
            }
            return std::string();
        }();
        if (!ca.empty()) curl_easy_setopt(c, CURLOPT_CAINFO, ca.c_str());
        else curl_easy_setopt(c, CURLOPT_CAPATH, "/system/etc/security/cacerts");
    }
#endif
#ifdef _WIN32
    curl_easy_setopt(c, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NATIVE_CA);  // OpenSSL, trusting the Windows certificate store
#endif
    if (r.jar != Jar::NONE) {
        curl_easy_setopt(c, CURLOPT_SHARE, g_jars[(int)r.jar].sh);
        curl_easy_setopt(c, CURLOPT_COOKIEFILE, "");
    } else {
        curl_easy_setopt(c, CURLOPT_SHARE, g_jars[0].sh);  // DNS cache only; cookies stay off without COOKIEFILE
    }
    struct curl_slist* hdrs = nullptr;
    bool has_ua = false;
    for (const auto& [k, v] : r.headers) {
        if (su::lower(k) == "user-agent") has_ua = true;
        hdrs = curl_slist_append(hdrs, (k + ": " + v).c_str());
    }
    if (!has_ua) hdrs = curl_slist_append(hdrs, "User-Agent: ani-c3d");
    hdrs = curl_slist_append(hdrs, "Expect:");
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
    if (r.method == "POST") {
        curl_easy_setopt(c, CURLOPT_POST, 1L);
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, r.body.c_str());
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)r.body.size());
    } else if (r.method != "GET") {
        curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, r.method.c_str());
    }
    CURLcode rc = curl_easy_perform(c);
    curl_slist_free_all(hdrs);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &out.status);
    char* eff = nullptr;
    curl_easy_getinfo(c, CURLINFO_EFFECTIVE_URL, &eff);
    if (eff) out.url = eff;
    if (rc == CURLE_WRITE_ERROR && r.max_bytes && out.body.size() >= r.max_bytes) rc = CURLE_OK;  // stopped on purpose
    if (rc != CURLE_OK) throw Error(std::string(curl_easy_strerror(rc)) + " (" + r.url.substr(0, 80) + ")");
    return out;
}
}  // namespace http
