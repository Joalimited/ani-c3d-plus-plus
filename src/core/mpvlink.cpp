#include "mpvlink.hpp"

#include <chrono>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#endif

namespace mpvlink {
#ifdef _WIN32
static std::wstring widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}
// Windows command-line quoting (the rules CommandLineToArgvW / the CRT use)
static std::wstring quote_arg(const std::wstring& a) {
    if (!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos) return a;
    std::wstring out = L"\"";
    for (size_t i = 0;; i++) {
        size_t bs = 0;
        while (i < a.size() && a[i] == L'\\') { i++; bs++; }
        if (i == a.size()) { out.append(bs * 2, L'\\'); break; }
        if (a[i] == L'"') { out.append(bs * 2 + 1, L'\\'); out += L'"'; }
        else { out.append(bs, L'\\'); out += a[i]; }
    }
    return out + L"\"";
}

std::unique_ptr<Proc> Proc::spawn(const std::vector<std::string>& args) {
    std::wstring cmd;
    for (size_t i = 0; i < args.size(); i++) {
        if (i) cmd += L' ';
        cmd += quote_arg(widen(args[i]));
    }
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    std::wstring exe = widen(args[0]);
    // CREATE_NO_WINDOW: no console flash next to the player
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return nullptr;
    CloseHandle(pi.hThread);
    std::unique_ptr<Proc> p(new Proc());
    p->handle_ = pi.hProcess;
    return p;
}
Proc::~Proc() {
    if (handle_) CloseHandle((HANDLE)handle_);
}
bool Proc::running() { return handle_ && WaitForSingleObject((HANDLE)handle_, 0) == WAIT_TIMEOUT; }
void Proc::wait() {
    if (handle_) WaitForSingleObject((HANDLE)handle_, INFINITE);
}
void Proc::terminate() {
    if (running()) {
        TerminateProcess((HANDLE)handle_, 1);
        WaitForSingleObject((HANDLE)handle_, 3000);
    }
}

std::unique_ptr<Link> Link::connect(const std::string& path, Proc* proc, int tries) {
    std::wstring w = widen(path);
    for (int i = 0; i < tries; i++) {
        HANDLE h = CreateFileW(w.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            std::unique_ptr<Link> l(new Link());
            l->pipe_ = h;
            l->read_ev_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            l->write_ev_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            return l;
        }
        if (proc && !proc->running()) return nullptr;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return nullptr;
}
Link::~Link() {
    close();
    if (read_ev_) CloseHandle((HANDLE)read_ev_);
    if (write_ev_) CloseHandle((HANDLE)write_ev_);
}
void Link::close() {
    if (pipe_ && !closed_) {
        closed_ = true;
        CancelIoEx((HANDLE)pipe_, nullptr);
        CloseHandle((HANDLE)pipe_);
    }
}
bool Link::readline(std::string& line) {
    while (true) {
        size_t nl = buf_.find('\n');
        if (nl != std::string::npos) {
            line = buf_.substr(0, nl);
            buf_.erase(0, nl + 1);
            return true;
        }
        if (closed_) return false;
        char tmp[4096];
        OVERLAPPED ov{};
        ov.hEvent = (HANDLE)read_ev_;
        ResetEvent(ov.hEvent);
        DWORD got = 0;
        if (!ReadFile((HANDLE)pipe_, tmp, sizeof tmp, nullptr, &ov)) {
            if (GetLastError() != ERROR_IO_PENDING) return false;
        }
        if (!GetOverlappedResult((HANDLE)pipe_, &ov, &got, TRUE) || got == 0) return false;
        buf_.append(tmp, got);
    }
}
void Link::send(const std::string& s) {
    std::lock_guard<std::mutex> lk(send_mu_);
    if (closed_) return;
    OVERLAPPED ov{};
    ov.hEvent = (HANDLE)write_ev_;
    ResetEvent(ov.hEvent);
    DWORD put = 0;
    if (!WriteFile((HANDLE)pipe_, s.data(), (DWORD)s.size(), nullptr, &ov) && GetLastError() != ERROR_IO_PENDING) return;
    GetOverlappedResult((HANDLE)pipe_, &ov, &put, TRUE);
}
#else
std::unique_ptr<Proc> Proc::spawn(const std::vector<std::string>& args) {
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    pid_t pid = fork();
    if (pid < 0) return nullptr;
    if (pid == 0) {
        int nul = open("/dev/null", O_RDWR);
        if (nul >= 0) {
            dup2(nul, 0);
            dup2(nul, 1);
            dup2(nul, 2);
        }
        setsid();
        execvp(argv[0], argv.data());
        _exit(127);
    }
    std::unique_ptr<Proc> p(new Proc());
    p->pid_ = pid;
    return p;
}
Proc::~Proc() {
    std::lock_guard<std::mutex> lk(mu_);
    if (pid_ > 0 && !reaped_) {
        if (waitpid(pid_, nullptr, WNOHANG) == pid_) reaped_ = true;
    }
}
bool Proc::running() {
    std::lock_guard<std::mutex> lk(mu_);
    if (pid_ <= 0 || reaped_) return false;
    int st;
    pid_t r = waitpid(pid_, &st, WNOHANG);
    if (r == pid_ || (r < 0 && errno == ECHILD)) {
        reaped_ = true;
        return false;
    }
    return true;
}
void Proc::wait() {
    while (running()) std::this_thread::sleep_for(std::chrono::milliseconds(50));
}
void Proc::terminate() {
    if (!running()) return;
    kill(pid_, SIGTERM);
    for (int i = 0; i < 60 && running(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (running()) kill(pid_, SIGKILL);
}

std::unique_ptr<Link> Link::connect(const std::string& path, Proc* proc, int tries) {
    for (int i = 0; i < tries; i++) {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
        if (fd >= 0 && ::connect(fd, (sockaddr*)&addr, sizeof addr) == 0) {
            std::unique_ptr<Link> l(new Link());
            l->fd_ = fd;
            return l;
        }
        if (fd >= 0) ::close(fd);
        if (proc && !proc->running()) return nullptr;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return nullptr;
}
Link::~Link() { close(); }
void Link::close() {
    if (fd_ >= 0 && !closed_) {
        closed_ = true;
        shutdown(fd_, SHUT_RDWR);
        ::close(fd_);
    }
}
bool Link::readline(std::string& line) {
    while (true) {
        size_t nl = buf_.find('\n');
        if (nl != std::string::npos) {
            line = buf_.substr(0, nl);
            buf_.erase(0, nl + 1);
            return true;
        }
        if (closed_) return false;
        char tmp[4096];
        ssize_t n = ::recv(fd_, tmp, sizeof tmp, 0);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return false;
        }
        buf_.append(tmp, (size_t)n);
    }
}
void Link::send(const std::string& s) {
    std::lock_guard<std::mutex> lk(send_mu_);
    if (closed_) return;
    size_t off = 0;
    while (off < s.size()) {
        ssize_t n = ::send(fd_, s.data() + off, s.size() - off, MSG_NOSIGNAL);
        if (n <= 0) return;
        off += (size_t)n;
    }
}
#endif
}  // namespace mpvlink
