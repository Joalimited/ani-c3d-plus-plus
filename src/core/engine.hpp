// The playback/search engine (port of engine.py): scraping, mpv control (JSON IPC) and the watch history.
// handle() blocks: call it from a worker thread. Events (player state, watched lists, errors) go out through `send`.
#pragma once
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "json.hpp"
#include "mpvlink.hpp"

class Engine {
public:
    using json = nlohmann::json;
    Engine();
    ~Engine();
    std::function<void(const json&)> send;  // set by the app: posts the event to the render thread
    json handle(const std::string& cmd, const json& a);
    json snapshot();

private:
    std::recursive_mutex mu_;
    std::pair<std::string, std::string> anime_;  // (id, title); id "" = none
    std::vector<std::string> episodes_;
    std::string mode_ = "sub";
    bool autonext_ = true;
    std::string cur_ep_;
    std::shared_ptr<mpvlink::Proc> proc_;
    std::shared_ptr<mpvlink::Link> link_;
    std::atomic<long long> token_{0};
    bool eof_ = false;
    json state_ = json::object();
    std::string sock_;
    double last_emit_ = 0;
    bool flush_pending_ = false;
    bool autofs_ = false;
    std::map<std::string, bool> autoskip_ = {{"intro", false}, {"outro", false}};
    std::set<std::pair<std::string, std::string>> skipped_;
    std::string outro_armed_ep_;  // same for the credits switch
    std::string last_state_;
    double state_at_ = 0;
    void push_state();
    std::string intro_armed_ep_;  // the episode the intro switch was pressed on (its own intro is skipped by the button, not the switch)
    std::map<std::string, std::pair<double, double>> skip_;
    std::string poster_, source_, late_sub_;
    double resume_at_ = 0;

    json skip_ranges();
    void emit(bool force = false);
    json control(const std::string& action, const json& v);
    void play(const std::string& ep, double start = 0);
    void watch(std::shared_ptr<mpvlink::Proc> proc, long long token);
    void auto_skip();
    void send_ranges(const std::shared_ptr<mpvlink::Link>& link);
    std::string last_ranges_;
    long long aniskip_tok_ = -1;
    double ranges_at_ = 0;
    void save_resume(bool finished = false);
    void kill();
    void mpv(const json& cmd);
    void prefetch(const std::string& aid, const std::string& title);
    int ep_index(const std::string& ep);
    double num(const char* key, double dflt = 0);
};
