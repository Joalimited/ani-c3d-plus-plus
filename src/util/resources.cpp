#include "resources.hpp"

#include "paths.hpp"

#ifdef _WIN32
#include <windows.h>
#endif
#ifdef __ANDROID__
#include <SDL.h>
#endif

namespace res {
#ifdef _WIN32
// resource ids match tools/ani-c3d.rc
static int id_of(const std::string& name) {
    if (name == "font.ttf") return 101;
    if (name == "mpv.zip") return 102;
    if (name == "mesa.zip") return 103;
    return 0;
}
#endif

static std::string file_of(const std::string& name) {
    if (name == "font.ttf") return paths::resource("fonts/NotoSansMono-Bold.ttf");
    return paths::resource(name);
}

bool get(const std::string& name, std::string& out) {
#ifdef __ANDROID__
    // the APK's assets/ folder (the font, ...) is read through SDL's RWops
    std::string rel = name == "font.ttf" ? "fonts/NotoSansMono-Bold.ttf" : name;
    if (SDL_RWops* rw = SDL_RWFromFile(rel.c_str(), "rb")) {
        Sint64 n = SDL_RWsize(rw);
        out.resize(n > 0 ? (size_t)n : 0);
        size_t got = n > 0 ? SDL_RWread(rw, out.data(), 1, (size_t)n) : 0;
        SDL_RWclose(rw);
        if (got == (size_t)n && n > 0) return true;
    }
    return false;
#endif
#ifdef _WIN32
    if (int id = id_of(name)) {
        HRSRC h = FindResourceA(nullptr, MAKEINTRESOURCEA(id), MAKEINTRESOURCEA(10) /* RT_RCDATA */);
        if (h) {
            HGLOBAL g = LoadResource(nullptr, h);
            DWORD size = SizeofResource(nullptr, h);
            const char* p = g ? (const char*)LockResource(g) : nullptr;
            if (p && size) {
                out.assign(p, size);
                return true;
            }
        }
    }
#endif
    return paths::read_file(file_of(name), out);
}

bool has(const std::string& name) {
#ifdef __ANDROID__
    std::string tmp;
    return get(name, tmp);
#endif
#ifdef _WIN32
    if (int id = id_of(name))
        if (FindResourceA(nullptr, MAKEINTRESOURCEA(id), MAKEINTRESOURCEA(10))) return true;
#endif
    return paths::exists(file_of(name));
}
}  // namespace res
