// Background work for the render loop: a thread pool runs blocking jobs (network, decoding, the engine) and posts their
// results back to a queue the render thread drains every frame (App::pump). Same model as the Python version's asyncio
// thread + queue.
#pragma once
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "json.hpp"

class Tasks {
public:
    using json = nlohmann::json;
    using Cb = std::function<void(const json& result, const std::string& err)>;

    explicit Tasks(int threads = 24);
    ~Tasks();
    // run fn() on a worker; cb(result, err) later runs on the render thread (err is "" on success)
    void run(std::function<json()> fn, Cb cb = nullptr);
    // fire-and-forget on a worker
    void spawn(std::function<void()> fn);
    // queue something for the render thread
    void post(std::function<void()> fn);
    // render thread: run everything that was posted
    void drain();
    std::function<void(const std::string&)> on_error;  // a callback threw (App shows it in the status line)

private:
    std::vector<std::thread> workers_;
    std::deque<std::function<void()>> jobs_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool stop_ = false;
    std::deque<std::function<void()>> posted_;
    std::mutex post_mu_;
};
