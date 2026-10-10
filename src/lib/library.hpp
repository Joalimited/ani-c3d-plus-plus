// The library (port of library.py): a bookcase built from your watch history and star list (or your manga), plus a tiny
// person who walks, climbs and pulls books out; a picked book opens like a real one (BookView).
#pragma once
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "../games/games.hpp"
#include "book.hpp"
#include "bookdata.hpp"
#include "json.hpp"

class App;

namespace libc_ {
constexpr double BOOK_H = 3.5, BOOK_D = 2.0;
constexpr double VOL_W = 0.62, VOL_H = 3.2, FACE_W = 2.2;
constexpr double SHELF_W = 21.0, PERSON_Z = 2.9, ROW_GAP = 4.7;
constexpr double LIBRARIAN_X = -SHELF_W / 2 - 4.2;
}  // namespace libc_

class Library {
public:
    using json = nlohmann::json;
    using Groups = std::map<std::string, std::vector<BookP>>;
    Library(App& app, std::vector<json> rows, std::vector<json> wl, const std::string& kind = "anime", Groups groups = {});
    App& app;
    std::vector<json> raw_rows, raw_wl;
    std::string kind;
    Groups groups_in;
    std::vector<std::pair<std::string, std::string>> sections;
    std::string search_label, mangafire, name;
    BookP last_removed;
    int hidden_count = 0;
    std::vector<std::pair<std::string, std::string>> hidden_items;
    bool drawer = false;
    double drawer_t = 0, pulse = -9, clear_armed = -9;
    int drawer_scroll = 0;
    struct Drag {
        int i;
        double x, y;
        int row;
    };
    std::optional<Drag> drag;
    std::optional<int> drag_candidate;
    int facing = 1;
    bool moving = false;
    std::optional<std::pair<double, int>> auto_;  // walk target (x, row)
    struct Pick {
        int i;
        double u;
        bool closing;
    };
    std::optional<Pick> pick;
    std::shared_ptr<BookView> bv;
    std::optional<std::pair<V3, V3>> ray;
    std::optional<int> near;
    std::vector<FallBook> falling;
    bool dirty = false;
    double zoom = 1.0;
    double pan[2] = {0, 0};
    std::string focus = "spread";
    double px = 0, py = 0.3;
    int level = 0;
    std::vector<BookP> books;
    struct Row {
        std::string section, label;
    };
    std::vector<Row> rows;
    std::map<std::string, std::pair<std::string, int>> section_info;
    std::vector<double> row_y;
    std::optional<int> next_up;
    std::optional<int> dwell_i;
    double dwell_t = 0;

    void layout(const std::string& focus_id = "");
    void extend(const std::string& group, const std::vector<BookP>& more);
    void apply_extend();
    void set_search(const std::string& query, const std::vector<BookP>& found);
    void remove_book(int i);
    void undo_remove();
    void restore_one(const std::string& id, const std::string& title);
    void clear_removed();
    void restore_all();
    void apply_titles();
    std::optional<int> pick_book(const V3& origin, const V3& dir) const;
    std::optional<std::pair<double, double>> drag_point(const V3& origin, const V3& dir) const;
    int target_row(double y) const;
    bool start_drag(const V3& origin, const V3& dir);
    void drag_move(const V3& origin, const V3& dir);
    void drop();
    void walk_to(int i);
    bool librarian_near() const;
    void relayout();
    void toggle_plan(const BookP& b, std::optional<bool> on = std::nullopt);
    void toggle_star(const BookP& b);
    void key(const std::string& k);
    void cycle_focus();
    void zoom_by(double dy);
    void pan_by(double dx, double dy);
    bool click(const V3& origin, const V3& dir);
    void update(double dt, const Keys& keys);
    V3 display() const;
    CamGoal camera() const;
    std::vector<Box> build(Renderer& r, TextCache& text, const Theme& th, double t);
    void hud(double W, double H);
    std::string section_here() const;

private:
    std::vector<Box> librarian(Renderer& r, TextCache& text, const Theme& th, double t);
    std::vector<Box> bookcase(Renderer& r, TextCache& text, const Theme& th, double t);
    std::vector<Box> volume(Renderer& r, TextCache& text, const Theme& th, const BookP& b, double y0, double s, bool hot);
    void spine(Renderer& r, TextCache& text, const Book& b, const M4& M, double scale, C4 tint, double avail = -1, C3 color = {1, 1, 1});
    std::vector<Box> open_book(Renderer& r, TextCache& text, const Theme& th, double t);
    std::vector<Box> person(double t, const Theme& th);
    void removed_drawer(double W, double H);
    std::string label_of(const std::string& section) const;
};
