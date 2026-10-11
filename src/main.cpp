// ani-c3d entry point: the window and event loop (SDL2 + OpenGL 3.3), plus --check, --snapshot and the crash report.
#include <SDL.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "app/app.hpp"
#include "app/remote.hpp"
#include "core/androidplayer.hpp"
#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif
#include "core/core.hpp"
#include "core/scrape.hpp"
#include "gfx/gl.hpp"
#include "gfx/image.hpp"
#include "gfx/text.hpp"
#include "util/db.hpp"
#include "util/http.hpp"
#include "util/paths.hpp"
#include "util/resources.hpp"
#include "util/strutil.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#endif

using json = nlohmann::json;
static auto T0 = std::chrono::steady_clock::now();
static double since_start() { return std::chrono::duration<double>(std::chrono::steady_clock::now() - T0).count(); }

// ---------------------------------------------------------------- window + GL context
struct Window {
    SDL_Window* win = nullptr;
    SDL_GLContext ctx = nullptr;
};

static bool create_gl(Window& wd, int w, int h, bool hidden) {
#ifdef ANI_GLES
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (hidden ? SDL_WINDOW_HIDDEN : 0);
    wd.win = SDL_CreateWindow("ani-c3d", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, flags);
    if (!wd.win) throw std::runtime_error(std::string("could not open a window: ") + SDL_GetError());
    wd.ctx = SDL_GL_CreateContext(wd.win);
    if (!wd.ctx) return false;
    SDL_GL_MakeCurrent(wd.win, wd.ctx);
    return gl_load();
}

static void close_gl(Window& wd) {
    if (wd.ctx) SDL_GL_DeleteContext(wd.ctx);
    if (wd.win) SDL_DestroyWindow(wd.win);
    wd = Window{};
}

static Window open_window(int w, int h, bool hidden) {
    Window wd;
    bool force_sw = paths::getenv_or("ANIC3D_SOFTWARE_GL", "") == "1";
    if (!force_sw && create_gl(wd, w, h, hidden)) return wd;
#ifdef _WIN32
    // no OpenGL 3.3 driver (a VM, or a PC without GPU drivers): fall back to the bundled Mesa software renderer, the way
    // the Python version fell back to Direct3D 12's WARP
    close_gl(wd);
    std::string zip;
    if (res::get("mesa.zip", zip)) {
        std::string dir = paths::join(paths::data_dir(), "mesa");
        std::string dll = paths::join(dir, "opengl32.dll");
        if (!paths::exists(dll) || !paths::exists(paths::join(dir, "libgallium_wgl.dll"))) {
            paths::makedirs(dir);
            paths::unzip_to(zip, dir);
        }
        int n = MultiByteToWideChar(CP_UTF8, 0, dir.c_str(), -1, nullptr, 0);
        std::wstring wdir(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, dir.c_str(), -1, wdir.data(), n);
        SetDllDirectoryW(wdir.c_str());  // so opengl32.dll finds libgallium_wgl.dll next to it
        SDL_GL_UnloadLibrary();
        if (SDL_GL_LoadLibrary(dll.c_str()) == 0 && create_gl(wd, w, h, hidden)) {
            std::cout << "ani-c3d: no OpenGL 3.3 driver - using the bundled software renderer (Mesa llvmpipe)" << std::endl;
            return wd;
        }
        close_gl(wd);
    }
#endif
    close_gl(wd);
    throw std::runtime_error(std::string("could not create an OpenGL 3.3 context (the graphics driver is too old?): ") + SDL_GetError());
}

// ---------------------------------------------------------------- SDL keys -> the key names the app uses
static std::string key_name(const SDL_Keysym& ks) {
    SDL_Keycode k = ks.sym;
    Uint16 mod = ks.mod;
    // letters come from the physical key: on SDL3 (sdl2-compat) the keycode can carry Caps Lock / layout changes, which
    // turned A/D/W/S into names the movement code never looked for
    if (ks.scancode >= SDL_SCANCODE_A && ks.scancode <= SDL_SCANCODE_Z) {
        SDL_Keycode lk = SDL_GetKeyFromScancode(ks.scancode);
        char c = (lk >= 'a' && lk <= 'z') ? (char)lk : (lk >= 'A' && lk <= 'Z') ? (char)(lk + 32) : (char)('a' + (ks.scancode - SDL_SCANCODE_A));
        if (mod & KMOD_SHIFT) c = (char)(c - 32);
        return std::string(1, c);
    }
    switch (k) {
        case SDLK_RETURN:
        case SDLK_KP_ENTER: return "Enter";
        case SDLK_ESCAPE: return "Escape";
        case SDLK_BACKSPACE: return "Backspace";
        case SDLK_TAB: return "Tab";
        case SDLK_DELETE: return "Delete";
        case SDLK_UP: return "ArrowUp";
        case SDLK_DOWN: return "ArrowDown";
        case SDLK_LEFT: return "ArrowLeft";
        case SDLK_RIGHT: return "ArrowRight";
        case SDLK_HOME: return "Home";
        case SDLK_END: return "End";
        case SDLK_PAGEUP: return "PageUp";
        case SDLK_PAGEDOWN: return "PageDown";
        case SDLK_LSHIFT:
        case SDLK_RSHIFT: return "Shift";
        case SDLK_LCTRL:
        case SDLK_RCTRL: return "Control";
        case SDLK_LALT:
        case SDLK_RALT: return "Alt";
        case SDLK_LGUI:
        case SDLK_RGUI: return "Meta";
        case SDLK_SPACE: return " ";
        case SDLK_KP_PLUS: return "+";
        case SDLK_KP_MINUS: return "-";
        default: break;
    }
    if (k >= 32 && k < 127) {
        char c = (char)k;
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (c >= 'a' && c <= 'z' && (mod & KMOD_SHIFT)) c = (char)(c - 32);
        return std::string(1, c);
    }
    const char* n = SDL_GetKeyName(k);
    return n ? n : "";
}

static std::set<std::string> mod_names(Uint16 mod) {
    std::set<std::string> m;
    if (mod & KMOD_SHIFT) m.insert("Shift");
    if (mod & KMOD_CTRL) m.insert("Control");
    if (mod & KMOD_ALT) m.insert("Alt");
    if (mod & KMOD_GUI) m.insert("Meta");
    return m;
}

static int button_of(Uint8 b) { return b == SDL_BUTTON_LEFT ? 1 : b == SDL_BUTTON_RIGHT ? 2 : b == SDL_BUTTON_MIDDLE ? 3 : (int)b; }

// ---------------------------------------------------------------- --check: imports, font, scraper, database, mpv, GPU
static int check(const std::string& out_path) {
    std::ofstream log;
    if (!out_path.empty()) log.open(out_path);
    auto say = [&](const std::string& m) {
        std::cout << m << std::endl;
        if (log) log << m << std::endl;
    };
    if (paths::getenv_or("ANI_CLI_WATCH_DB", "").empty()) {
        std::string tmp = paths::join(paths::cache_dir(), "check.db");
        paths::makedirs(paths::cache_dir());
        db::WATCH_DB = tmp;
    }
    bool ok = true;
    auto step = [&](const std::string& name, std::function<std::string()> fn) {
        try {
            say("[ ok ] " + name + ": " + fn());
        } catch (const std::exception& e) {
            ok = false;
            say("[FAIL] " + name + ": " + e.what());
        }
    };
#ifdef _WIN32
    step("platform", [] { return std::string("win32"); });
#else
    step("platform", [] { return std::string("linux"); });
#endif
    step("font", [] {
        if (!Font::get().ok()) throw std::runtime_error("no font");
        return Font::get().path.empty() ? std::string("bundled (inside the exe)") : Font::get().path;
    });
    step("settings dir", [] { return paths::config_dir(); });
    step("watch db", [] {
        db::Conn con;
        con.query("select count(*) from history");
        return db::WATCH_DB;
    });
    step("scraper: search", [] { return std::to_string(scrape::search("frieren").size()) + " results"; });
    step("scraper: episodes", [] { return std::to_string(scrape::episodes("frieren-beyond-journeys-end-481").size()) + " episodes"; });
    step("scraper: stream link", [] { return scrape::link("frieren-beyond-journeys-end-481", "1", "sub").url.substr(0, 60) + "..."; });
    step("miruro fallback: search", [] { return std::to_string(scrape::mi_search("frieren").size()) + " results"; });
    step("miruro fallback: stream link", [] { return scrape::mi_link(scrape::mi_search("frieren").at(0).id, "1", "sub").url.substr(0, 60) + "..."; });
    step("manga api", [] { return std::to_string(core::md_popular(5).size()) + " popular titles"; });
    step("mpv", [] {
        std::string p = paths::find_mpv();
        if (!p.empty()) return p;
        if (res::has("mpv.zip")) return std::string("bundled copy (unpacks on first play)");
        throw std::runtime_error("NOT FOUND");
    });
    step("mpv unpack + run", [] {
        std::string p = paths::ensure_mpv();
#ifdef _WIN32
        return p;
#else
        std::string cmd = "\"" + p + "\" --version 2>/dev/null";
        FILE* f = popen(cmd.c_str(), "r");
        char buf[256] = {0};
        if (f) {
            if (!fgets(buf, sizeof buf, f)) buf[0] = 0;
            pclose(f);
        }
        return p + " -> " + su::strip(buf);
#endif
    });
    step("themes", [] { return su::join(themes::order(), ", "); });
    step("gpu (OpenGL 3.3)", [] {
        Window w = open_window(64, 64, true);
        std::string r = (const char*)glGetString(GL_RENDERER);
        std::string v = (const char*)glGetString(GL_VERSION);
        SDL_GL_DeleteContext(w.ctx);
        SDL_DestroyWindow(w.win);
        return r + " (" + v + ")";
    });
    say(ok ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return ok ? 0 : 1;
}

// ---------------------------------------------------------------- headless snapshot (testing)
static int snapshot(const std::string& out, const std::string& query, int select, int W, int H) {
    Window w = open_window(W, H, true);
    Renderer r;
    App app(r, 1.0);
    app.q_in.set(query);
    app.typing = false;
    app.search();
    double start = since_start(), last = start, opened = 0;
    int stage = 0;
    while (true) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
        }
        double now = since_start();
        app.frame(0, W, H, 1.0, now - last);
        last = now;
        double elapsed = now - start;
        bool posters = !app.scene.peds.empty();
        for (size_t i = 0; i < app.scene.peds.size() && i < 8; i++) posters = posters && (bool)app.scene.peds[i].poster;
        if ((stage == 0 && posters) || (stage == 0 && elapsed > 25 && !app.scene.peds.empty())) {
            stage = 1;
            opened = now;
            if (select >= 0) app.open_ped(select);
        } else if (stage == 1 && (select < 0 || app.scene.field) && now - opened > 3.5) {
            break;
        }
        if (elapsed > 60) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    Image im;
    im.w = r.width();
    im.h = r.height();
    im.rgba = r.read_pixels();
    img::save_png(out, im);
    std::cout << "saved " << out << std::endl;
    SDL_GL_DeleteContext(w.ctx);
    SDL_DestroyWindow(w.win);
    return 0;
}

// ---------------------------------------------------------------- --playtest: search, open, play episode 1, read the player state
static int playtest(const std::string& query) {
    Window w = open_window(640, 360, true);
    Renderer r;
    App app(r, 1.0);
    app.q_in.set(query);
    app.typing = false;
    app.search();
    double start = since_start(), last = start, played = 0;
    int stage = 0;
    int rc = 1;
    while (since_start() - start < 120) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
        }
        double now = since_start();
        app.frame(0, 640, 360, 1.0, now - last);
        last = now;
        if (stage == 0 && !app.scene.peds.empty()) {
            app.open_ped(0);
            stage = 1;
        } else if (stage == 1 && app.scene.field) {
            app.play(app.scene.field->eps.at(0));
            stage = 2;
        } else if (stage == 2 && app.player.value("playing", false)) {
            played = now;
            stage = 3;
        } else if (stage == 3 && app.player.value("pos", 0.0) > 3) {
            std::cout << "playtest: playing '" << app.player["anime"].value("title", "") << "' episode " << app.player.value("episode", json("?")).dump()
                      << " at " << app.player.value("pos", 0.0) << "s of " << app.player.value("duration", 0.0) << "s via " << app.player.value("source", "") << " ("
                      << now - played << "s after start)" << std::endl;
            app.control("pause");
            stage = 4;
            played = now;
        } else if (stage == 4 && now - played > 2) {
            std::cout << "playtest: paused=" << app.player.value("paused", false) << " status='" << app.status << "'" << std::endl;
            app.control("stop");
            rc = 0;
            stage = 5;
            played = now;
        } else if (stage == 5 && now - played > 2) {
            std::cout << "playtest: stopped, playing=" << app.player.value("playing", false) << std::endl;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (rc) std::cout << "playtest: FAILED at stage " << stage << " status='" << app.status << "'" << std::endl;
    SDL_GL_DeleteContext(w.ctx);
    SDL_DestroyWindow(w.win);
    return rc;
}

// ---------------------------------------------------------------- the scripted self-test (ANIC3D_SELFTEST=seconds)
static std::string g_shot;  // the self-test asks for a screenshot of the next frame (ANIC3D_SHOTS=<dir>)
static void shot(const std::string& name) {
    std::string dir = paths::getenv_or("ANIC3D_SHOTS", "");
    if (!dir.empty()) g_shot = paths::join(dir, name + ".png");
}

// ANIC3D_SELFTEST_GAMES=1: every arcade game for a few seconds, with a screenshot of each
static void games_step(App& app, double el, double secs, bool& quit) {
    static int idx = -1;
    static double t0 = 0;
    static const std::vector<std::string> keys = {"snake", "breakout", "quiz", "kong", "trivia", "maze", "galaga"};
    if (el < 6) return;  // let HOME / the posters load (the poster quiz needs them)
    if (idx >= 0 && el - t0 > 3.6 && el - t0 < 3.7) shot("game-" + keys[idx]);  // a frame of this game, before the next starts
    if (idx < 0 || el - t0 > 4.0) {
        idx++;
        t0 = el;
        if (idx >= (int)keys.size() || el > secs) {
            std::cout << "games: all " << keys.size() << " games ran" << std::endl;
            quit = true;
            return;
        }
        app.start_game(keys[idx]);
        if (keys[idx] == "breakout") {
            app.on_key_down(" ", {});
            app.on_key_up(" ");
        }
        if (keys[idx] == "galaga") {
            app.on_key_down("Enter", {});
            app.on_key_up("Enter");
        }
        std::cout << "games: " << app.game->name << std::endl;
    }
    if (keys[idx] == "galaga" || keys[idx] == "kong") {  // keep firing / jumping so something happens
        if (((int)(el * 4)) % 2 == 0) app.on_key_down(" ", {});
        else app.on_key_up(" ");
    }
}

static void selftest_step(App& app, int frame, double el, double secs, bool& quit) {
    if (paths::getenv_or("ANIC3D_SELFTEST_GAMES", "") == "1") return games_step(app, el, secs, quit);
    static int done_upto = 0;  // steps fire once, at the time the 60 fps Python script reached them
    auto at = [&](int f) {
        if (f <= done_upto || el < f / 60.0) return false;
        done_upto = f;
        return true;
    };
    auto ev = [&](const std::string& k) {  // a full key press: down then up
        app.on_key_down(k, {});
        app.on_key_up(k);
    };
    auto sec = [&](Library* l) {
        std::string s = "{";
        if (l)
            for (auto& [k, v] : l->section_info) s += k + ":" + std::to_string(v.second) + " ";
        return s + "}";
    };
    if (at(30)) {
        for (char c : std::string("frieren")) app.on_char(std::string(1, c));
        ev("Enter");
    }
    if (at(400)) {
        app.start_game("snake");
        ev("ArrowDown");
        std::cout << "selftest: mode=" << app.mode << " game=" << app.game->name << std::endl;
    }
    if (at(520)) {
        app.exit_mode();
        app.open_library();
    }
    if (at(380)) shot("01-search");
    if (at(700)) shot("02-library");
    // walk with real SDL key events (D then A, Caps Lock style upper-case keycodes) to test the whole key path
    static double walk_x0 = 0, walk_x1 = 0;
    auto push_key = [](Uint32 type, SDL_Scancode sc) {
        SDL_Event e{};
        e.type = type;
        e.key.keysym.scancode = sc;
        e.key.keysym.sym = SDL_GetKeyFromScancode(sc) - 32;  // 'D' / 'A'
        e.key.keysym.mod = KMOD_CAPS;
        SDL_PushEvent(&e);
    };
    if (at(705) && app.lib) {
        walk_x0 = app.lib->px;
        push_key(SDL_KEYDOWN, SDL_SCANCODE_D);
    }
    if (at(720) && app.lib) {
        push_key(SDL_KEYUP, SDL_SCANCODE_D);
        walk_x1 = app.lib->px;
        push_key(SDL_KEYDOWN, SDL_SCANCODE_A);
    }
    if (at(735) && app.lib) {
        push_key(SDL_KEYUP, SDL_SCANCODE_A);
        std::cout << "selftest: walk d " << walk_x0 << " -> " << walk_x1 << ", a -> " << app.lib->px << std::endl;
    }
    if (at(760)) {
        std::cout << "selftest: mode=" << app.mode << " sections=" << sec(app.lib.get()) << std::endl;
        ev("Enter");  // pick the book next to the person
    }
    if (at(1000) && app.lib) {
        std::cout << "selftest: book open=" << (bool)app.lib->pick << " spread=" << (app.lib->bv ? app.lib->bv->spread : -1) << std::endl;
        ev("ArrowRight");
    }
    if (at(1110)) shot("03-book-open");
    if (at(1120) && app.lib) {
        std::cout << "selftest: spread after flip=" << (app.lib->bv ? app.lib->bv->spread : -1) << std::endl;
        ev("Escape");  // close the book
    }
    if (at(1250) && app.lib) {
        std::cout << "selftest: book closed=" << !app.lib->pick << std::endl;
        ev("k");  // barrel climb from the shelves
    }
    if (at(1320)) shot("04-barrel-climb");
    if (at(1330)) {
        std::cout << "selftest: mode=" << app.mode << " game=" << (app.game ? app.game->name : "none") << std::endl;
        ev("Escape");
    }
    if (at(1400)) {
        std::cout << "selftest: back in " << app.mode << std::endl;
        ev("m");  // switch to the manga shelf
    }
    if (at(1900) && app.lib) {
        std::cout << "selftest: mode=" << app.mode << " kind=" << app.lib->kind << " sections=" << sec(app.lib.get()) << std::endl;
        for (auto& bk : app.lib->books)
            if (bk->total > 20) {
                app.lib->px = bk->x;
                app.lib->level = bk->row;
                break;
            }
    }
    if (at(1950)) shot("05-manga-shelf");
    if (at(1960)) ev("Enter");
    if (at(2190)) shot("06-manga-toc");
    if (at(2200) && app.lib && app.lib->pick) {
        BookP bk = app.lib->books[app.lib->pick->i];
        std::cout << "selftest: opened manga '" << bk->title << "' chapters=" << bk->all_eps.size() << std::endl;
        if (!bk->all_eps.empty()) app.read_chapter(bk, bk->all_eps[0]);
    }
    if (at(2500) && app.lib && app.lib->bv) {
        auto& bv = *app.lib->bv;
        std::cout << "selftest: reading mode=" << bv.mode << " pages=" << bv.pages_urls.size() << " images=" << bv.imgs.size() << " rtl=" << bv.rtl << std::endl;
        ev(bv.rtl ? "ArrowLeft" : "ArrowRight");  // next page
    }
    if (at(2490)) shot("07-manga-reading");
    if (at(2520) && app.lib && app.lib->pick) {  // OPEN IN DOCUMENT READER: the chapter as a PDF
        BookP bk = app.lib->books[app.lib->pick->i];
        if (!bk->all_eps.empty()) app.export_chapter(bk, bk->all_eps[0]);
    }
    if (at(2750)) std::cout << "selftest: export status='" << app.status << "'" << std::endl;
    if (at(2550) && app.lib) {
        double z0 = app.lib->zoom;
        app.mouse = {800, 400};
        app.on_wheel(-300);
        std::cout << "selftest: wheel zoom before=" << z0 << std::endl;
    }
    if (at(2600) && app.lib) {
        std::cout << "selftest: wheel zoom after=" << app.lib->zoom << std::endl;
        ev("u");
    }
    if (at(2630)) {
        std::cout << "selftest: hide_ui=" << app.hide_ui << " hits=" << app.hits.size() << std::endl;
        ev("u");
    }
    if (at(2650) && app.lib && app.lib->bv) {
        std::cout << "selftest: manga spread=" << app.lib->bv->spread << " hide_ui=" << app.hide_ui << std::endl;
        ev("Escape");
    }
    if (at(2780) && app.lib) {
        std::cout << "selftest: manga book closed=" << !app.lib->pick << std::endl;
        ev("Escape");
    }
    if (el > secs) {
        std::cout << "selftest: " << frame << " frames, " << (int)(frame / el) << " fps, query='" << app.q_in.text() << "', peds=" << app.scene.peds.size()
                  << ", mode=" << app.mode << ", theme=" << app.theme().key << std::endl;
        quit = true;
    }
}

// ---------------------------------------------------------------- the window
static int run_window() {
    Window w = open_window(1500, 900, false);
    SDL_GL_SetSwapInterval(1);
    Renderer r;
    double ui = 1.0;  // UI scale: a TV is read from the couch, so the Android build draws everything bigger
#ifdef __ANDROID__
    {
        // tuned for the Fire TV Stick GPUs; `adb shell setprop debug.anic3d.scale 0.67` (also .msaa, .ui) to experiment
        auto prop = [](const char* name, double dflt) {
            char v[PROP_VALUE_MAX] = {0};
            return __system_property_get(name, v) > 0 ? std::atof(v) : dflt;
        };
        double scale = prop("debug.anic3d.scale", 0.67);
        int msaa = (int)prop("debug.anic3d.msaa", 2);
        ui = prop("debug.anic3d.ui", 1.3);
        r.set_quality(scale, msaa);
        SDL_DisableScreenSaver();  // keep the TV awake while the app is open
    }
#endif
    std::cout << "ani-c3d: OpenGL renderer = " << r.gpu_name << "  (ready in " << su::fmt("%.2f", since_start()) << "s)" << std::endl;
    int ww, wh, dw, dh;
    SDL_GetWindowSize(w.win, &ww, &wh);
    SDL_GL_GetDrawableSize(w.win, &dw, &dh);
    App app(r, (double)dw / std::max(1, ww) * ui);
    app.gpu_label = "OPENGL";
#ifdef __ANDROID__
    aplayer::init();  // the Java MediaPlayer side; its picture arrives as an external-OES texture
    aplayer::init_surface(r.ensure_video_tex());
#endif
    Remote remote;
    remote.init(app, ww / ui, wh / ui);
    if (remote.enabled) app.typing = false;  // don't pop the on-screen keyboard open at launch
    if (remote.enabled) SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");  // Back goes to the app, which decides (see remote.hpp)
    // text input only while a text box has focus: with it on, KDE/Wayland passes letters through its input-method path,
    // which turns a held A/D/W/S into one instant press + release, so walking with letters never worked
    bool text_on = false;
    SDL_StopTextInput();
    bool keylog = !paths::getenv_or("ANIC3D_KEYLOG", "").empty();
    double secs = std::atof(paths::getenv_or("ANIC3D_SELFTEST", paths::getenv_or("ANI3D_SELFTEST", "0")).c_str());
    double started = since_start(), last = started;
    int frames = 0;
    bool quit = false;
    while (!quit) {
        bool want_text = (app.mode == "world" && app.typing) || app.lib_typing;
        if (want_text != text_on) {
            text_on = want_text;
            app.swallow.reset();  // the key that opened the box ("/") sent no text while input was off
            if (text_on) SDL_StartTextInput();
            else SDL_StopTextInput();
        }
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
                case SDL_QUIT: quit = true; break;
                case SDL_MOUSEMOTION: app.on_pointer_move(ev.motion.x / ui, ev.motion.y / ui); break;
                case SDL_MOUSEBUTTONDOWN: app.on_pointer_down(ev.button.x / ui, ev.button.y / ui, button_of(ev.button.button)); break;
                case SDL_MOUSEBUTTONUP: app.on_pointer_up(ev.button.x / ui, ev.button.y / ui, button_of(ev.button.button)); break;
                case SDL_MOUSEWHEEL: {
                    double dy = ev.wheel.preciseY != 0 ? ev.wheel.preciseY : ev.wheel.y;
                    app.on_wheel(-dy * 100.0);  // wheel down = positive dy, about 100 per notch (like the browser events)
                    break;
                }
                case SDL_TEXTINPUT:
                    if (keylog) std::cerr << "text '" << ev.text.text << "'" << std::endl;
                    app.on_char(ev.text.text);
                    break;
                case SDL_KEYDOWN:
#ifdef __ANDROID__
                    std::cout << "ani-c3d: key scancode=" << (int)ev.key.keysym.scancode << " sym=" << ev.key.keysym.sym << std::endl;
#endif
                    if (remote.key(ev.key, app, (int)(ww / ui), (int)(wh / ui), quit)) break;
                    if (keylog)
                        std::cerr << "key down sym=" << ev.key.keysym.sym << " scancode=" << ev.key.keysym.scancode << " mod=" << ev.key.keysym.mod
                                  << " repeat=" << (int)ev.key.repeat << " -> '" << key_name(ev.key.keysym) << "'" << std::endl;
                    if (ev.key.repeat) break;  // held keys are repeated by the app itself (see TextInput)
                    app.on_key_down(key_name(ev.key.keysym), mod_names(ev.key.keysym.mod));
                    break;
                case SDL_KEYUP:
                    if (remote.key(ev.key, app, (int)(ww / ui), (int)(wh / ui), quit)) break;
                    if (keylog) std::cerr << "key up   -> '" << key_name(ev.key.keysym) << "'" << std::endl;
                    app.on_key_up(key_name(ev.key.keysym));
                    break;
                case SDL_WINDOWEVENT:
                    if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST) app.keys.clear();  // releases we will never see
                    break;
                default: break;
            }
        }
        double now = since_start();
        double dt = std::min(now - last, 0.1);
        last = now;
        SDL_GetWindowSize(w.win, &ww, &wh);
        SDL_GL_GetDrawableSize(w.win, &dw, &dh);
        remote.update(app, dt, (int)(ww / ui), (int)(wh / ui));
#ifdef __ANDROID__
        {
            aplayer::Frame vf = aplayer::frame();
            r.set_video(vf.showing, vf.matrix, vf.w, vf.h);
            app.video_on = r.video_on();
        }
#endif
        app.frame(0, dw, dh, (double)dw / std::max(1, ww) * ui, dt);
        if (!g_shot.empty()) {
            Image im;
            im.w = r.width();
            im.h = r.height();
            im.rgba = r.read_pixels();
            img::save_png(g_shot, im);
            g_shot.clear();
        }
        SDL_GL_SwapWindow(w.win);
        frames++;
        if (frames % 300 == 0) {  // a frame-rate line in the log every ~5 s (adb logcat -s ani-c3d)
            static double fps_t = 0;
            static int fps_f = 0;
            double nowt = since_start();
            if (fps_t > 0) std::cout << "ani-c3d: fps=" << su::fmt("%.1f", (frames - fps_f) / (nowt - fps_t)) << " mode=" << app.mode << " boxes=" << r.last_boxes << " quads=" << r.last_quads << " theme=" << app.theme().key << std::endl;
            fps_t = nowt;
            fps_f = frames;
        }
        if (frames == 1) std::cout << "ani-c3d: first frame after " << su::fmt("%.2f", since_start()) << "s" << std::endl;
        if (secs > 0) selftest_step(app, frames, now - started, secs, quit);
    }
    SDL_StopTextInput();
    SDL_GL_DeleteContext(w.ctx);
    SDL_DestroyWindow(w.win);
    return 0;
}

// the Windows exe has no console: show what went wrong in a dialog and keep a log
static void report_crash(const std::string& what) {
    std::string log = paths::join(paths::data_dir(), "crash.log");
    paths::makedirs(paths::data_dir());
    if (!paths::write_file(log, what + "\n")) log = "(could not write a log)";
    std::string hint;
    std::string low = su::lower(what);
    if (su::contains(low, "opengl") || su::contains(low, "context")) hint = "\n\nThis usually means the graphics driver is too old or missing OpenGL 3.3 support. Updating your GPU driver normally fixes it.";
    std::string msg = "ani-c3d hit a problem and had to close." + hint + "\n\nDetails were saved to:\n" + log + "\n\n" + what;
#ifdef _WIN32
    MessageBoxA(nullptr, msg.c_str(), "ani-c3d", MB_ICONERROR);
#else
    std::cerr << msg << std::endl;
#endif
}

#ifdef __ANDROID__
#include <android/log.h>
// stdout / stderr go to logcat (adb logcat -s ani-c3d)
class LogcatBuf : public std::streambuf {
    std::string line_;

  protected:
    int overflow(int c) override {
        if (c == '\n' || c == EOF) {
            if (!line_.empty()) __android_log_write(ANDROID_LOG_INFO, "ani-c3d", line_.c_str());
            line_.clear();
        } else {
            line_.push_back((char)c);
        }
        return c;
    }
};
#endif

int main(int argc, char** argv) {
#ifdef __ANDROID__
    static LogcatBuf logcat;
    std::cout.rdbuf(&logcat);
    std::cerr.rdbuf(&logcat);
    if (const char* internal = SDL_AndroidGetInternalStoragePath()) {  // app-private storage stands in for $HOME
        setenv("HOME", internal, 1);
        setenv("TMPDIR", internal, 1);
        db::WATCH_DB = paths::watch_db();  // the static initializer ran before HOME existed: it had picked /tmp, which Android won't let us write
    }
#endif
    paths::set_exe_dir(argc > 0 ? argv[0] : "");
    std::string logf = paths::getenv_or("ANIC3D_LOG", paths::getenv_or("ANI3D_LOG", ""));
    if (!logf.empty()) {  // send everything the app prints to a file (the Windows exe has no console)
        static std::ofstream out(logf, std::ios::app);
        std::cout.rdbuf(out.rdbuf());
        std::cerr.rdbuf(out.rdbuf());
    }
    http::init();
    std::vector<std::string> args(argv + 1, argv + argc);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        report_crash(std::string("SDL could not start: ") + SDL_GetError());
        return 1;
    }
    int rc = 0;
    try {
        if (!args.empty() && args[0] == "--check") rc = check(args.size() > 1 ? args[1] : "");
        else if (!args.empty() && args[0] == "--playtest" && args.size() >= 2) rc = playtest(args[1]);
        else if (!args.empty() && args[0] == "--snapshot" && args.size() >= 3) {
            // --snapshot out.png "query" [select_index|-] [WxH]
            int select = args.size() > 3 && args[3] != "-" ? std::atoi(args[3].c_str()) : -1;
            int W = 1600, H = 896;
            if (args.size() > 4) std::sscanf(args[4].c_str(), "%dx%d", &W, &H);
            rc = snapshot(args[1], args[2], select, W, H);
        } else {
            rc = run_window();
        }
    } catch (const std::exception& e) {
        report_crash(e.what());
        rc = 1;
    }
    SDL_Quit();
    std::fflush(stdout);
    std::_Exit(rc);  // worker threads may still sit in network calls: leave without waiting for them
}
