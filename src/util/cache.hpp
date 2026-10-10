// Memory + disk cache with a TTL (core.cached). Concurrent callers for the same key share one fetch. Blocking: call it
// from a worker thread.
#pragma once
#include <functional>
#include <string>

#include "json.hpp"

namespace cache {
using json = nlohmann::json;
json cached(const std::string& key, double ttl, const std::function<json()>& fetch);
double now();  // wall clock seconds (time.time())
}  // namespace cache
