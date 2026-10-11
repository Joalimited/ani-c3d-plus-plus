// The ani-c3d application (port of app.py): engine glue, input, HUD and the frame function.
#pragma once
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "../core/advance.hpp"
#include "../core/engine.hpp"
#include "../games/games.hpp"
#include "../gfx/renderer.hpp"
#include "../gfx/text.hpp"
#include "../lib/library.hpp"
#include "../world/creatures.hpp"
#include "../world/scene.hpp"
#include "../world/themes.hpp"
#include "json.hpp"
#include "tasks.hpp"
#include "textinput.hpp"

class App : public GameHost {
public:
    using json = nlohmann::json;
    App(Renderer& r, double ratio = 1.0);
    ~App() override;

    Renderer& r;
    std::string gpu_label;
    double ratio;
    TextCache text;
    Scene scene;
    Tasks tasks_;
    std::shared_ptr<Engine> engine;
    bool auto_intro = false, auto_outro = false;
    double t = 0, dt_last = 0;
    TextInput q_in, lib_in;
    bool typing = true;
    std::string audio = "sub";
    std::set<std::string> mods;
    std::string status = "Type an anime name and press Enter";
    json player = json::object();
    json info_manga = json::array();
    json info_related;  // null = not loaded, "loading", or {novels, games, sites}
    Hit hover;
    struct Blurb {
        std::string state;  // loading | ok | none | err
        json data;
        double t;
    };
    std::map<std::pair<std::string, std::string>, std::shared_ptr<Blurb>> blurbs;
    struct Card {
        std::string key;
        double since;
    };
    std::optional<Card> card;
    std::pair<double, double> mouse{0, 0};
    struct HitRect {
        double x, y, w, h;
        std::function<void(double, double)> cb;
    };
    std::vector<HitRect> hits;
    std::pair<double, double> size{1500, 900};
    int seq = 0, home_gen = 0, rows_gen = 0;
    std::optional<std::string> swallow;
    std::optional<std::pair<double, double>> down;
    std::pair<double, double> last_drag{0, 0};
    std::set<int> buttons_held;
    bool dragged = false, history_mode = false, home_started = false;
    std::string view = "none";  // home | search | history
    std::string mode = "world";  // world | game | library
    std::unique_ptr<Game> game;
    std::unique_ptr<Library> lib;
    std::set<std::string> keys, pressed;
    bool arcade_menu = false;
    // TV layout (Android): rail + hero + poster rows + detail page (see tvui.cpp)
    bool tv_ui = false;
    int tv_zone = 1, tv_rail = 1, tv_sec = 0, tv_idx = 0, tv_ep = -2, tv_cols = 6;  // zone 0 = rail, 1 = content
    double tv_scroll = 0, vol_until = 0;
    std::map<int, double> tv_hscroll;
    struct TvItem {
        std::string title, sub, id;
        TexP poster;
        int ref = -1;  // anime: index into scene.peds; manga: index into mg_items
        bool starred = false;
    };
    struct TvCat {
        std::string label;
        std::vector<TvItem> items;
    };
    struct TvDetail {
        bool open = false;
        std::string title, unit;  // unit: "EP" / "CH"
        TexP poster;
        std::vector<std::string> eps;
        std::set<std::string> watched;
        bool starred = false;
    };
    std::vector<TvCat> tv_cats() const;  // what the dashboard lists: anime sections, or the manga categories
    std::vector<std::string> tv_actions() const;
    TvDetail tv_detail() const;
    void tv_detail_play(const std::string& ep);
    void tv_detail_star();
    void tv_open(int ref);
    void tv_action(const std::string& name);
    // ---- manga on the TV (tvmanga.cpp)
    struct MgItem {
        json book;
        TexP poster;
        bool poster_loading = false;
    };
    bool tv_manga = false, mg_detail = false, mg_reading = false;
    std::vector<MgItem> mg_items;
    std::vector<std::pair<std::string, std::vector<int>>> mg_groups;
    int mg_gen = 0, mg_open_item = -1, mg_cur = 0, mg_strip = -1;
    BookP mg_book;
    std::vector<std::string> mg_chapters;
    std::set<std::string> mg_read, mg_listed, mg_busy;
    std::string mg_chapter;
    std::vector<std::string> mg_pages;
    std::map<std::string, TexP> mg_tex;
    double mg_scroll = 0, mg_hud = 0;
    void mg_enter();
    void mg_search(const std::string& q);
    void mg_fill(const json& groups);
    void mg_open(int item);
    void mg_read_chapter(const std::string& ch);
    void mg_step_chapter(int dir);
    void mg_close_reader();
    void mg_load(const std::string& url);
    bool mg_key(const std::string& key);
    void mg_draw(double W, double H);
    std::vector<std::vector<int>> tv_rows() const;
    bool tv_key(const std::string& key);
    bool tv_back();
    void tv_menu();
    void tv_rail_go(int i);
    void tv_hud(double W, double H);
    void dash_build();   // the 3D dashboard (dash.cpp)
    void dash_text(const std::string& s, double x, double y, double z, double size, C3 col, double alpha, bool center);
    std::string dash_clip(const std::string& s, size_t n) const;
    double dash_tab = 0, dash_item = 0, dash_grid = 0, dash_pulse = 0;
    int dash_last_tab = -1;
    void draw_volume(double W, double H);
    void tv_volume(double delta);
    void show_watchlist();
    void show_watching();  // the TV's home: what you are in the middle of, then what you watched recently
    // easter egg: UP UP DOWN DOWN LEFT RIGHT LEFT RIGHT sends an anime cat bouncing around the screen like a DVD logo
    bool egg_on = false;
    double egg_x = 40, egg_y = 30, egg_vx = 190, egg_vy = 125, egg_hue = 0.55, egg_flash = 0;
    int egg_corners = 0;
    std::deque<std::string> egg_keys;
    void egg_draw(double W, double H);
    bool tv_manga_reading_guard() const { return mg_reading; }
    bool video_on = false;        // Android: a video fills the screen (the renderer shows it instead of the 3D world)
    double last_input = 0, hud_until = 0, egg_start = 0;
    bool egg_auto = false, egg_nyan = false;  // video: last key / pointer event; the full HUD is shown until hud_until
    bool tv_remote = false, tv_cursor_on = false;  // TV remote: the D-pad pointer (see remote.hpp)
    std::pair<double, double> tv_cursor{0, 0};
    std::pair<double, double> cam_ground{0, 60};
    std::optional<int> dino_hover, follow;
    std::optional<std::pair<int, double>> dino_card;
    std::map<int, std::pair<std::string, double>> busy;
    int busy_seq = 0;
    struct Upload {
        int gen, idx;
        std::shared_ptr<const Image> img;
    };
    std::deque<Upload> upload_q;
    bool hide_ui = false;
    std::string game_back;
    bool manga_rtl = true, manga_jp = false, lib_typing = false, auto_fs = false, lib_busy = false;
    int lib_sel = -1;
    std::vector<std::string> recent;
    std::vector<std::string> suggest;
    int sugg_sel = -1;
    std::string sugg_for;
    double sugg_due = 0;
    struct Toast {
        std::string title, desc, group;
        double born;
    };
    std::vector<Toast> toasts;
    advance::Tracker adv;
    bool adv_open = false;
    double adv_scroll = 0;
    json edits;  // null | "loading" | [..]
    std::set<std::string> starred;
    double lib_t0 = 0;
    const Theme* theme_ = nullptr;
    std::vector<json> popular_todo;
    std::function<void(const std::string&)> open_url_hook;  // tests can intercept browser opens

    // GameHost
    double now() const override { return t; }
    Scene& world_scene() override { return scene; }
    const Theme& theme() const override { return *theme_; }
    Renderer& renderer() override { return r; }
    Tasks& tasks() override { return tasks_; }
    bool is_current_game(const void* g) const override { return game && game.get() == g; }
    std::vector<std::string> library_titles() const override;
    void txt(const std::string& s, double x, double y, double px, C3 color, bool center = false) override;
    void button(const std::string& label, double x, double y, double w, double h, std::function<void()> cb, bool active = false) override;

    // engine glue
    void call(const std::string& cmd, const json& args, Tasks::Cb cb = nullptr);
    Tasks::Cb tracked(const std::string& msg, Tasks::Cb cb);
    void pump();
    void on_engine_event(const json& ev);

    // actions
    bool jurassic() const;
    void keep_clear(std::vector<Box>& fx, size_t n_q3d, const V3& eye);
    bool reading_focus() const;
    Creatures& herd();
    std::optional<std::pair<double, double>> floor_point();
    bool click_dino();
    void toggle_follow();
    void search(const std::string& q = "");
    void show_home();
    void show_rows(const std::vector<json>& rows, const std::string& heading, const std::vector<std::pair<std::string, std::vector<json>>>* groups = nullptr);
    void load_poster(int gen, int idx);
    void drain_uploads(int budget = 2);
    void show_history();
    void poster_for(const std::string& title, const std::string& id, std::function<void(const std::string&)> cb);
    void update_suggestions();
    void pick_suggestion(int i);
    void load_edits();
    void check_adv(std::optional<advance::Toast> only = std::nullopt);
    void open_ped(int idx);
    void play(const std::string& ep, double start = 0);
    void toggle_skip(const std::string& kind);
    void toggle_autofs();
    void control(const std::string& action, const json& value = nullptr);
    void toggle_mode();
    void load_related();
    void exit_mode();
    void start_game(const std::string& key, const std::string& back = "");
    void open_library(const std::string& kind = "anime");
    std::vector<json> wl_with_data(std::vector<json> new_wl, const std::vector<json>& old_wl);
    void toggle_star(const std::string& id, const std::string& title, const std::string& poster = "");
    void star_selected();
    void ensure_chapters(const BookP& book, std::function<void()> then = nullptr, bool quiet = false);
    void read_chapter(const BookP& book, const std::string& chapter);
    void fetch_page_image(const std::string& url, BookView* bv);
    void play_from_book(const BookP& book, const std::string& ep);
    void fetch_book_poster(const BookP& book);
    void open_history_book(const BookP& book);
    void fill_jp_titles();
    void toggle_titles();
    void open_mangafire(const std::string& title);
    void open_link(const std::string& url);
    void librarian_search(const std::string& q);
    std::vector<std::string> lib_choices() const;
    void export_chapter(const BookP& book, std::optional<std::string> chapter = std::nullopt);
    void toggle_manga_direction();
    void cycle_theme(int step = 1);
    void home();

    // input (window events, already in logical pixels)
    void on_pointer_move(double x, double y);
    void on_pointer_down(double x, double y, int button);
    void on_pointer_up(double x, double y, int button);
    void on_wheel(double dy);
    void on_char(const std::string& c);
    void on_key_down(const std::string& key, const std::set<std::string>& mods);
    void on_key_up(const std::string& key);
    void on_key(const std::string& key);
    void click(double x, double y);
    std::pair<V3, V3> mouse_ray() const;

    // frame + HUD
    void frame(int target_fbo, int phys_w, int phys_h, double ratio, double dt);
    void panel(double x, double y, double w, double h);
    std::pair<double, double> txt_cb(const std::string& s, double x, double y, double px, C3 color, std::function<void(double, double)> cb, bool center = false);
    bool hit_test(double x, double y, double w, double h) const;
    bool over_hits() const;
    void hud(double W, double H);
    void video_overlay(double W, double H);  // subtitles, the SKIP INTRO / CREDITS prompt, a thin progress bar
    std::string skip_prompt() const;         // "intro" / "outro" while that scene is on screen, else ""
    void suggestions_dropdown(double sx, double sw);
    void toast_hud(double W, double H);
    void advancement_panel(double W, double H);
    void vcr_overlay(double W, double H);
    std::optional<std::array<double, 4>> screen_box(V3 lo, V3 hi) const;
    std::shared_ptr<Blurb> blurb(const std::string& kind, const std::string& title);
    std::vector<std::string> wrap(const std::string& s, double px, double width, size_t max_lines) const;
    double text_w(const std::string& s, double px) const;
    void hover_card(double W, double H);
    void dino_hud(double W, double H);
    World roam_world() const;
    void arcade_overlay(double W, double H);
    void librarian_overlay(double W, double H);
    void info_panel(double W, double H);
    void player_bar(double W, double H);

private:
    void library_ready_anime(const json& result);
    void library_ready_manga(const json& result);
    BookP book_from(const json& j) const;
};
