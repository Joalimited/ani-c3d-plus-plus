#include "tasks.hpp"

Tasks::Tasks(int threads) {
    for (int i = 0; i < threads; i++) {
        workers_.emplace_back([this] {
            while (true) {
                std::function<void()> job;
                {
                    std::unique_lock<std::mutex> lk(mu_);
                    cv_.wait(lk, [&] { return stop_ || !jobs_.empty(); });
                    if (stop_ && jobs_.empty()) return;
                    job = std::move(jobs_.front());
                    jobs_.pop_front();
                }
                try {
                    job();
                } catch (...) {
                }
            }
        });
    }
}

Tasks::~Tasks() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        stop_ = true;
        jobs_.clear();
    }
    cv_.notify_all();
    for (auto& w : workers_) w.detach();  // workers may be stuck in a network call; the process is exiting anyway
}

void Tasks::spawn(std::function<void()> fn) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        jobs_.push_back(std::move(fn));
    }
    cv_.notify_one();
}

void Tasks::run(std::function<json()> fn, Cb cb) {
    spawn([this, fn = std::move(fn), cb = std::move(cb)] {
        json result;
        std::string err;
        try {
            result = fn();
        } catch (const std::exception& e) {
            err = e.what();
            if (err.empty()) err = "error";
        } catch (...) {
            err = "error";
        }
        if (cb) post([cb, result = std::move(result), err] { cb(result, err); });
    });
}

void Tasks::post(std::function<void()> fn) {
    std::lock_guard<std::mutex> lk(post_mu_);
    posted_.push_back(std::move(fn));
}

void Tasks::drain() {
    std::deque<std::function<void()>> items;
    {
        std::lock_guard<std::mutex> lk(post_mu_);
        items.swap(posted_);
    }
    for (auto& f : items) {
        try {
            f();
        } catch (const std::exception& e) {
            if (on_error) on_error(e.what());
        } catch (...) {
            if (on_error) on_error("unknown error");
        }
    }
}
