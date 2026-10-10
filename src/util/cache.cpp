#include "cache.hpp"

#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>

#include "paths.hpp"
#include "strutil.hpp"

namespace cache {
namespace {
struct Flight {
    bool done = false;
    bool failed = false;
    json value;
    std::string error;
    std::condition_variable cv;
};
std::mutex g_mu;
std::map<std::string, std::pair<double, json>> g_mem;
std::map<std::string, std::shared_ptr<Flight>> g_inflight;
}  // namespace

double now() { return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count(); }

json cached(const std::string& key, double ttl, const std::function<json()>& fetch) {
    double t = now();
    std::string path = paths::join(paths::cache_dir(), su::sha1_hex(key) + ".json");
    std::shared_ptr<Flight> flight;
    bool leader = false;
    {
        std::unique_lock<std::mutex> lk(g_mu);
        auto hit = g_mem.find(key);
        if (hit != g_mem.end() && hit->second.first > t) return hit->second.second;
        std::string raw;
        if (paths::read_file(path, raw)) {
            try {
                json entry = json::parse(raw);
                double exp = entry.at("expires").get<double>();
                if (exp > t) {
                    g_mem[key] = {exp, entry.at("value")};
                    return entry.at("value");
                }
            } catch (...) {
            }
        }
        auto it = g_inflight.find(key);
        if (it != g_inflight.end()) {
            flight = it->second;
        } else {
            flight = std::make_shared<Flight>();
            g_inflight[key] = flight;
            leader = true;
        }
    }
    if (leader) {
        json value;
        std::string err;
        bool ok = true;
        try {
            value = fetch();
        } catch (const std::exception& e) {
            ok = false;
            err = e.what();
        } catch (...) {
            ok = false;
            err = "fetch failed";
        }
        std::unique_lock<std::mutex> lk(g_mu);
        if (ok) {
            double exp = now() + ttl;
            g_mem[key] = {exp, value};
            paths::makedirs(paths::cache_dir());
            json entry = {{"expires", exp}, {"value", value}};
            paths::write_file(path, entry.dump());
        }
        flight->done = true;
        flight->failed = !ok;
        flight->value = value;
        flight->error = err;
        g_inflight.erase(key);
        flight->cv.notify_all();
        if (!ok) throw std::runtime_error(err);
        return value;
    }
    std::unique_lock<std::mutex> lk(g_mu);
    flight->cv.wait(lk, [&] { return flight->done; });
    if (flight->failed) throw std::runtime_error(flight->error);
    return flight->value;
}
}  // namespace cache
