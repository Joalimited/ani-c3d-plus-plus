// Where things live on each OS, bundled resources, and finding (or unpacking) mpv. Port of paths.py.
#pragma once
#include <string>

namespace paths {
#ifdef _WIN32
constexpr bool WINDOWS = true;
#else
constexpr bool WINDOWS = false;
#endif

std::string home();
std::string cache_dir();
std::string config_dir();
std::string data_dir();
std::string watch_db();
std::string resource(const std::string& rel);  // bundled data file (next to the exe, or in the AppImage)
std::string exe_dir();
void set_exe_dir(const std::string& argv0);
std::string ipc_name();
bool unzip_to(const std::string& zip_bytes, const std::string& target);  // extract a zip held in memory
std::string find_mpv();    // "" when not found
std::string ensure_mpv();  // throws std::runtime_error with a helpful message
void open_file(const std::string& path);
void open_url(const std::string& url);
std::string documents_dir();
bool exists(const std::string& p);
bool is_dir(const std::string& p);
void makedirs(const std::string& p);
std::string join(const std::string& a, const std::string& b);
std::string dirname(const std::string& p);
std::string getenv_or(const char* name, const std::string& fallback);
bool read_file(const std::string& path, std::string& out);
bool write_file(const std::string& path, const std::string& data);
}  // namespace paths
