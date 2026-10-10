#include "resources.hpp"

#include "paths.hpp"

#ifdef _WIN32
#include <windows.h>
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
#ifdef _WIN32
    if (int id = id_of(name))
        if (FindResourceA(nullptr, MAKEINTRESOURCEA(id), MAKEINTRESOURCEA(10))) return true;
#endif
    return paths::exists(file_of(name));
}
}  // namespace res
