// TV remote control (Fire TV / Android TV): the D-pad drives an on-screen pointer, so every mouse interaction of the app
// works from the couch. Select = click (hold Select + D-pad = drag / orbit), Back = Escape (leaves the app from the home
// screen), Menu toggles between the pointer and plain arrow keys (walking the library, games), Play/Pause = space,
// Rewind / Fast-forward = zoom in the world. Enabled on Android, or with ANIC3D_REMOTE=1 for testing on a desktop.
#pragma once
#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <limits>

#include "app.hpp"

class Remote {
  public:
    bool enabled = false;

    void init(App& app, int w, int h) {
#ifdef __ANDROID__
        enabled = true;
#endif
        if (const char* e = std::getenv("ANIC3D_REMOTE")) enabled = e[0] == '1';
        app.tv_remote = enabled;
        app.tv_ui = enabled;  // the Crunchyroll-style TV layout (tvui.cpp) instead of the 3D world
        cx = w / 2.0;
        cy = h * 0.55;
    }

    // true when the key was consumed
    bool key(const SDL_KeyboardEvent& ke, App& app, int w, int h, bool& quit) {
        if (!enabled) return false;
        bool down = ke.type == SDL_KEYDOWN;
        SDL_Scancode sc = ke.keysym.scancode;
        bool vol_key = sc == SDL_SCANCODE_VOLUMEUP || sc == SDL_SCANCODE_VOLUMEDOWN;
        if (ke.repeat && !vol_key) return true;  // (a held volume button keeps stepping)
        auto tap = [&](const char* k) {
            if (down) {
                app.on_key_down(k, {});
                app.on_key_up(k);
            }
        };
        // volume: in-app (the MediaPlayer volume, kept across episodes) so the +/- buttons always do something visible
        if (sc == SDL_SCANCODE_VOLUMEUP || sc == SDL_SCANCODE_VOLUMEDOWN || sc == SDL_SCANCODE_KP_PLUS || sc == SDL_SCANCODE_KP_MINUS ||
            sc == SDL_SCANCODE_EQUALS || sc == SDL_SCANCODE_MINUS) {
            if (down) {
                bool upv = sc == SDL_SCANCODE_VOLUMEUP || sc == SDL_SCANCODE_KP_PLUS || sc == SDL_SCANCODE_EQUALS;
                app.tv_volume(upv ? 5 : -5);
            }
            return true;
        }
        if (app.tv_ui && !app.video_on && app.mode == "world" && !app.arcade_menu && !app.adv_open) {  // TV layout: plain D-pad focus
            if (sc == SDL_SCANCODE_AC_BACK || sc == SDL_SCANCODE_ESCAPE) {
                if (down && !app.tv_back()) quit = true;  // nothing left to go back to: leave the app
                return true;
            }
            if (sc == SDL_SCANCODE_MENU) {
                if (down) app.tv_menu();
                return true;
            }
            switch (sc) {
                case SDL_SCANCODE_SELECT:
                case SDL_SCANCODE_RETURN:
                case SDL_SCANCODE_KP_ENTER:
                    if (down) {
                        app.on_key_down("Enter", {});
                        app.on_key_up("Enter");
                    }
                    return true;
                case SDL_SCANCODE_UP: tap("ArrowUp"); return true;
                case SDL_SCANCODE_DOWN: tap("ArrowDown"); return true;
                case SDL_SCANCODE_LEFT: tap("ArrowLeft"); return true;
                case SDL_SCANCODE_RIGHT: tap("ArrowRight"); return true;
                case SDL_SCANCODE_AUDIOREWIND: tap(app.mg_reading ? "p" : "ArrowLeft"); return true;
                case SDL_SCANCODE_AUDIOFASTFORWARD: tap(app.mg_reading ? "n" : "ArrowRight"); return true;
                default: break;
            }
        }
        if (app.video_on) {  // watching a video
            bool hud = app.t < app.hud_until;
            if (sc == SDL_SCANCODE_AC_BACK || sc == SDL_SCANCODE_ESCAPE) {
                if (down) {
                    app.control("stop");
                    app.hud_until = 0;
                    release(app);
                }
                return true;
            }
            if (sc == SDL_SCANCODE_MENU) {
                if (down) {
                    app.hud_until = hud ? 0 : app.t + 6;
                    if (hud) release(app);
                }
                return true;
            }
            if (sc == SDL_SCANCODE_AUDIOPLAY || sc == SDL_SCANCODE_SPACE) {
                tap(" ");
                return true;
            }
            if (!hud) {
                switch (sc) {
                    case SDL_SCANCODE_SELECT:
                    case SDL_SCANCODE_RETURN:
                    case SDL_SCANCODE_KP_ENTER: {
                        if (!down) return true;
                        std::string p = app.skip_prompt();
                        if (!p.empty()) app.control(p == "intro" ? "skip_intro" : "skip_outro");  // Select takes the skip prompt
                        else {
                            tap(" ");
                            app.hud_until = app.t + 5;
                        }
                        return true;
                    }
                    case SDL_SCANCODE_LEFT:
                    case SDL_SCANCODE_AUDIOREWIND: tap("ArrowLeft"); return true;
                    case SDL_SCANCODE_RIGHT:
                    case SDL_SCANCODE_AUDIOFASTFORWARD: tap("ArrowRight"); return true;
                    case SDL_SCANCODE_UP: tap("ArrowUp"); return true;
                    case SDL_SCANCODE_DOWN: tap("ArrowDown"); return true;
                    default: break;
                }
            } else if (down) {
                app.hud_until = app.t + 6;  // keep the controls up while the D-pad is in use
            }
        }
        switch (sc) {
            case SDL_SCANCODE_MENU:
                if (down) {
                    override_ = !override_;
                    app.status = pointer_mode(app) ? "Remote: pointer mode (D-pad moves the cursor, Select clicks)"
                                                   : "Remote: key mode (D-pad = arrow keys, Select = Enter)";
                    if (!pointer_mode(app)) release(app);
                    app.tv_cursor_on = pointer_mode(app);
                }
                return true;
            case SDL_SCANCODE_AC_BACK:
            case SDL_SCANCODE_ESCAPE:
                if (down) {
                    bool at_root = app.mode == "world" && !app.typing && !app.arcade_menu && !app.adv_open && !app.lib_typing &&
                                   app.view != "search" && app.view != "history" && app.scene.selected < 0;
                    if (at_root) quit = true;
                    else tap("Escape");
                }
                return true;
            case SDL_SCANCODE_AUDIOPLAY:
            case SDL_SCANCODE_SPACE:
                tap(" ");
                return true;
            case SDL_SCANCODE_AUDIOREWIND:
            case SDL_SCANCODE_AUDIOFASTFORWARD:
                if (down) {
                    bool ff = sc == SDL_SCANCODE_AUDIOFASTFORWARD;
                    if (app.mode == "world" && !app.player.value("playing", false)) app.on_wheel(ff ? 120 : -120);
                    else tap(ff ? "ArrowRight" : "ArrowLeft");
                }
                return true;
            case SDL_SCANCODE_SELECT:
            case SDL_SCANCODE_RETURN:
            case SDL_SCANCODE_KP_ENTER:
                if (pointer_mode(app)) {
                    if (down) {
                        select_down = true;
                        app.on_pointer_down(cx, cy, 1);
                    } else if (select_down) {
                        select_down = false;
                        app.on_pointer_up(cx, cy, 1);
                    }
                } else {
                    if (down) app.on_key_down("Enter", {});
                    else app.on_key_up("Enter");
                }
                return true;
            case SDL_SCANCODE_UP:
            case SDL_SCANCODE_DOWN:
            case SDL_SCANCODE_LEFT:
            case SDL_SCANCODE_RIGHT: {
                if (!pointer_mode(app)) return false;  // plain arrow keys
                int d = sc == SDL_SCANCODE_UP ? 0 : sc == SDL_SCANCODE_DOWN ? 1 : sc == SDL_SCANCODE_LEFT ? 2 : 3;
                if (down) {
                    held[d] = true;
                    held_t[d] = 0;
                    snap(app, d, w, h);
                } else {
                    held[d] = false;
                }
                return true;
            }
            default: return false;
        }
    }

    void update(App& app, double dt, int w, int h) {
        if (!enabled) return;
        bool pm = pointer_mode(app);
        app.tv_cursor_on = pm;
        if (!pm) return;
        double scale = h / 1080.0;
        double vx = 0, vy = 0;
        for (int d = 0; d < 4; d++) {
            if (!held[d]) continue;
            held_t[d] += dt;
            if (held_t[d] < 0.25) continue;  // a quick tap only snaps (below); holding moves freely
            double speed = (450 + std::min(1400.0, (held_t[d] - 0.25) * 1500)) * scale;
            if (d == 0) vy -= speed;
            else if (d == 1) vy += speed;
            else if (d == 2) vx -= speed;
            else vx += speed;
        }
        if (vx != 0 || vy != 0) {
            cx = std::clamp(cx + vx * dt, 0.0, w - 1.0);
            cy = std::clamp(cy + vy * dt, 0.0, h - 1.0);
            app.on_pointer_move(cx, cy);
        }
        app.tv_cursor = {cx, cy};
    }

  private:
    double cx = 0, cy = 0;
    bool held[4] = {false, false, false, false};
    double held_t[4] = {0, 0, 0, 0};
    bool select_down = false, override_ = false;

    bool default_pointer(const App& app) const {
        if (app.video_on) return app.t < app.hud_until;  // the controls are up: pointer; otherwise plain keys
        if (app.arcade_menu || app.adv_open) return true;
        return app.mode == "world" && !app.typing && !app.lib_typing;
    }
    bool pointer_mode(const App& app) const { return override_ ? !default_pointer(app) : default_pointer(app); }

    void release(App& app) {
        for (bool& h : held) h = false;
        if (select_down) {
            select_down = false;
            app.on_pointer_up(cx, cy, 1);
        }
    }

    // a tap on the D-pad jumps to the nearest button / pedestal / episode block in that direction; with nothing to snap
    // to the cursor takes a small step (holding the key moves it freely)
    void snap(App& app, int d, int w, int h) {
        double dx = d == 2 ? -1 : d == 3 ? 1 : 0, dy = d == 0 ? -1 : d == 1 ? 1 : 0;
        double best = std::numeric_limits<double>::max(), bx = 0, by = 0;
        bool found = false;
        auto consider = [&](double mx, double my) {
            double along = (mx - cx) * dx + (my - cy) * dy, across = std::fabs((mx - cx) * dy) + std::fabs((my - cy) * dx);
            if (along < 10 || across > along * 1.6 + 30) return;
            double score = along + 2.0 * across;
            if (score < best) {
                best = score;
                bx = mx;
                by = my;
                found = true;
            }
        };
        for (auto& hr : app.hits) {
            if (hr.w * hr.h > 0.2 * w * h || hr.w < 6 || hr.h < 6) continue;  // full-screen catch-alls
            if (cx >= hr.x && cx <= hr.x + hr.w && cy >= hr.y && cy <= hr.y + hr.h) continue;  // the one we are on
            consider(hr.x + hr.w / 2, hr.y + hr.h / 2);
        }
        if (app.mode == "world" && !app.arcade_menu && !app.adv_open) {
            for (auto& pb : app.scene.pick_boxes) {  // the 3D things you can click, projected to the screen
                double x = (pb.mn.x + pb.mx.x) / 2, y = (pb.mn.y + pb.mx.y) / 2, z = (pb.mn.z + pb.mx.z) / 2;
                auto c = app.r.viewproj.mul(x, y, z, 1.0);
                if (c[3] <= 1e-6) continue;
                double nx = c[0] / c[3], ny = c[1] / c[3];
                if (std::fabs(nx) > 1 || std::fabs(ny) > 1) continue;
                double sx = (nx + 1) / 2 * w, sy = (1 - ny) / 2 * h;
                if (std::hypot(sx - cx, sy - cy) < 6) continue;
                consider(sx, sy);
            }
        }
        if (found && best < 0.6 * w) {
            cx = bx;
            cy = by;
            held_t[d] = -0.2;  // hold on for a moment before the free movement takes over
        } else {
            double step = 48.0 * h / 1080.0;
            cx = std::clamp(cx + dx * step, 0.0, w - 1.0);
            cy = std::clamp(cy + dy * step, 0.0, h - 1.0);
            held_t[d] = -0.1;
        }
        app.on_pointer_move(cx, cy);
    }
};
