#include "paths.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "resources.hpp"
#include "../../third_party/miniz.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#else
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace paths {
static std::string g_exe_dir;

std::string getenv_or(const char* name, const std::string& fallback) {
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : fallback;
}

std::string home() {
#ifdef _WIN32
    return getenv_or("USERPROFILE", getenv_or("HOMEDRIVE", "C:") + getenv_or("HOMEPATH", "\\"));
#else
    return getenv_or("HOME", "/tmp");
#endif
}

std::string join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    char last = a.back();
    if (last == '/' || last == '\\') return a + b;
#ifdef _WIN32
    return a + "\\" + b;
#else
    return a + "/" + b;
#endif
}

std::string dirname(const std::string& p) { return fs::path(p).parent_path().string(); }

std::string cache_dir() {
    if (WINDOWS) return join(join(getenv_or("LOCALAPPDATA", home()), "ani-c3d"), "cache");
    return join(home(), ".cache/ani-c3d");
}
std::string config_dir() {
    if (WINDOWS) return join(getenv_or("APPDATA", home()), "ani-c3d");
    return join(home(), ".config/ani-c3d");
}
std::string data_dir() {
    if (WINDOWS) return join(getenv_or("LOCALAPPDATA", home()), "ani-c3d");
    return join(home(), ".local/share/ani-c3d");
}
// the same history database as ani-cli / ani-3d, so all of them share what you watched
std::string watch_db() { return getenv_or("ANI_CLI_WATCH_DB", join(home(), "Watch-history.db")); }

void set_exe_dir(const std::string& argv0) {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    g_exe_dir = fs::path(std::wstring(buf, n)).parent_path().string();
#else
    std::error_code ec;
    auto p = fs::read_symlink("/proc/self/exe", ec);
    g_exe_dir = ec ? fs::absolute(fs::path(argv0)).parent_path().string() : p.parent_path().string();
#endif
}
std::string exe_dir() { return g_exe_dir; }

std::string resource(const std::string& rel) {
    // next to the binary (dev build / AppImage usr/bin) or ../share/ani-c3d (AppImage layout)
    for (auto base : {g_exe_dir, join(g_exe_dir, "assets"), join(dirname(g_exe_dir), "share/ani-c3d"), join(dirname(g_exe_dir), "assets")}) {
        std::string p = join(base, rel);
        if (exists(p)) return p;
    }
    return join(g_exe_dir, rel);
}

std::string ipc_name() {
#ifdef _WIN32
    return "\\\\.\\pipe\\ani-c3d-mpv-" + std::to_string(GetCurrentProcessId());
#else
    return join(getenv_or("TMPDIR", "/tmp"), "ani-c3d-mpv-" + std::to_string(getpid()) + ".sock");
#endif
}

bool exists(const std::string& p) {
    std::error_code ec;
    return fs::exists(fs::u8path(p), ec);
}
bool is_dir(const std::string& p) {
    std::error_code ec;
    return fs::is_directory(fs::u8path(p), ec);
}
void makedirs(const std::string& p) {
    std::error_code ec;
    fs::create_directories(fs::u8path(p), ec);
}

bool read_file(const std::string& path, std::string& out) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}
bool write_file(const std::string& path, const std::string& data) {
    std::ofstream f(fs::u8path(path), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(data.data(), (std::streamsize)data.size());
    return (bool)f;
}

static std::string which(const std::string& name) {
    std::string path = getenv_or("PATH", "");
#ifdef _WIN32
    char sep = ';';
    std::string exe = name + ".exe";
#else
    char sep = ':';
    std::string exe = name;
#endif
    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find(sep, start);
        if (end == std::string::npos) end = path.size();
        std::string dir = path.substr(start, end - start);
        if (!dir.empty()) {
            std::string cand = join(dir, exe);
#ifdef _WIN32
            if (exists(cand)) return cand;
#else
            if (access(cand.c_str(), X_OK) == 0 && !is_dir(cand)) return cand;
#endif
        }
        start = end + 1;
    }
    return "";
}

std::string find_mpv() {
#ifdef __ANDROID__
    return "android-player";  // MediaPlayer inside the app (core/mpvlink_android.cpp)
#endif
    std::string found = which("mpv");
    if (!found.empty()) return found;
    std::string local = join(join(data_dir(), "mpv"), WINDOWS ? "mpv.exe" : "mpv");
    return exists(local) ? local : "";
}

// unpack a zip held in memory into `target` (the bundled Windows mpv / software OpenGL)
bool unzip_to(const std::string& zip, const std::string& target) {
    mz_zip_archive za{};
    if (!mz_zip_reader_init_mem(&za, zip.data(), zip.size(), 0)) return false;
    bool ok = true;
    int n = (int)mz_zip_reader_get_num_files(&za);
    for (int i = 0; i < n && ok; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&za, (mz_uint)i, &st)) { ok = false; break; }
        std::string name = st.m_filename;
        if (name.find("..") != std::string::npos) continue;
        std::string out = join(target, name);
        if (mz_zip_reader_is_file_a_directory(&za, (mz_uint)i)) { makedirs(out); continue; }
        makedirs(dirname(out));
        ok = mz_zip_reader_extract_to_file(&za, (mz_uint)i, out.c_str(), 0);
    }
    mz_zip_reader_end(&za);
    return ok;
}

std::string ensure_mpv() {
#ifdef __ANDROID__
    return "android-player";
#endif
    std::string path = find_mpv();
    std::string local = join(data_dir(), "mpv");
    if (!path.empty()) {
        // a copy unpacked by an older build lacks vulkan-1.dll (clean Windows has none, and mpv.exe imports it)
        bool stale = WINDOWS && dirname(path) == local && !exists(join(local, "vulkan-1.dll"));
        if (!(stale && res::has("mpv.zip"))) return path;
    }
    std::string zip;
    if (res::get("mpv.zip", zip)) {
        makedirs(local);
        if (unzip_to(zip, local)) {
            path = find_mpv();
            if (!path.empty()) return path;
        }
    }
    throw std::runtime_error("mpv was not found. Install it (Windows: winget install mpv, Linux: your package manager) and try again.");
}

void open_file(const std::string& path) {
    if (!getenv_or("ANIC3D_NO_OPEN", "").empty()) {  // tests: report instead of launching a viewer / browser
        std::printf("open: %s\n", path.c_str());
        std::fflush(stdout);
        return;
    }
#ifdef _WIN32
    int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, w.data(), n);
    ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    pid_t pid = fork();
    if (pid == 0) {
        // detach fully so the opener outlives us and leaves no zombie
        if (fork() == 0) {
            FILE* nul = std::fopen("/dev/null", "w");
            if (nul) { dup2(fileno(nul), 1); dup2(fileno(nul), 2); }
#ifdef __APPLE__
            execlp("open", "open", path.c_str(), (char*)nullptr);
#else
            execlp("xdg-open", "xdg-open", path.c_str(), (char*)nullptr);
#endif
            _exit(127);
        }
        _exit(0);
    }
    if (pid > 0) waitpid(pid, nullptr, 0);
#endif
}
void open_url(const std::string& url) { open_file(url); }

std::string documents_dir() {
    std::string docs = join(home(), "Documents");
    std::string base = is_dir(docs) ? docs : data_dir();
    return join(base, "ani-c3d manga");
}
}  // namespace paths
