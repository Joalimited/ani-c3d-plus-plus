// One interface to mpv's JSON IPC: a Unix socket on Linux/macOS, an overlapped named pipe on Windows (a plain blocking
// handle would let a pending read block every write). Also starting / stopping the mpv process.
#pragma once
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace mpvlink {
class Proc {
public:
    static std::unique_ptr<Proc> spawn(const std::vector<std::string>& args);  // nullptr on failure
    ~Proc();
    bool running();     // still alive (non-blocking)
    void wait();        // block until it exits
    void terminate();   // ask it to quit; kills it after 3 s
private:
    Proc() = default;
#ifdef _WIN32
    void* handle_ = nullptr;
#else
    int pid_ = -1;
    bool reaped_ = false;
    std::mutex mu_;
#endif
};

class Link {
public:
    // wait for mpv to create its IPC endpoint and connect; nullptr if mpv exited first
    static std::unique_ptr<Link> connect(const std::string& path, Proc* proc, int tries = 100);
    ~Link();
    bool readline(std::string& line);  // false on EOF / error
    void send(const std::string& json_line);
    void close();

private:
    Link() = default;
    std::string buf_;
    std::mutex send_mu_;
#ifdef _WIN32
    void* pipe_ = nullptr;
    void* read_ev_ = nullptr;
    void* write_ev_ = nullptr;
#else
    int fd_ = -1;
#endif
    bool closed_ = false;
};
}  // namespace mpvlink
