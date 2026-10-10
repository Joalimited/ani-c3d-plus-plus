#include "library.hpp"

#include <algorithm>
#include <cmath>
#include <set>

#include "../app/app.hpp"
#include "../core/core.hpp"
#include "../util/strutil.hpp"

using namespace libc_;
using json = nlohmann::json;

namespace {
const C3 CREAM{0.94, 0.92, 0.86}, BRASS{0.85, 0.66, 0.28}, GOLD{1.0, 0.82, 0.2};
const std::vector<std::pair<std::string, std::string>> SECTIONS = {{"watching", "WATCHING"}, {"planning", "PLANNING TO WATCH"}, {"watched", "WATCHED"}};
const std::vector<std::pair<std::string, std::string>> MANGA_SECTIONS = {{"search", "LIBRARIAN'S PICKS"}, {"reading", "READING"}, {"plan", "GOING TO READ"},
                                                                         {"anime", "MANGA OF YOUR ANIME"}, {"popular", "POPULAR ON MANGADEX"}, {"done", "FINISHED"}};

long long hsh(const std::string& s) {
    long long v = 0;
    for (uint32_t ch : su::utf8_decode(s)) v = (v * 131 + ch) % 1000003;
    return v;
}
std::string cut(const std::string& s, size_t n) { return su::utf8_substr(s, 0, n); }
std::vector<std::string> strv(const json& j, const char* k) {
    std::vector<std::string> out;
    if (j.contains(k) && j[k].is_array())
        for (auto& v : j[k])
            if (v.is_string()) out.push_back(v.get<std::string>());
    return out;
}
std::string sval(const json& j, const char* k) { return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : ""; }
int ival(const json& j, const char* k) { return j.contains(k) && j[k].is_number() ? j[k].get<int>() : 0; }
}  // namespace

Library::Library(App& a, std::vector<json> rows_in, std::vector<json> wl, const std::string& kind_, Groups groups)
    : app(a), raw_rows(std::move(rows_in)), raw_wl(std::move(wl)), kind(kind_), groups_in(std::move(groups)) {
    sections = kind == "manga" ? MANGA_SECTIONS : SECTIONS;
    name = kind == "manga" ? "MANGA LIBRARY" : "LIBRARY";
    layout();
    if (!books.empty()) {
        BookP first = books[0];
        for (auto& b : books)
            if (b->section == "watching" || b->section == "reading" || b->section == "search") {
                first = b;
                break;
            }
        px = first->x;
        level = first->row;
        py = row_y[first->row];
    }
}

std::string Library::label_of(const std::string& section) const {
    for (auto& [k, l] : sections)
        if (k == section) return l;
    return section;
}

void Library::layout(const std::string& focus_id) {
    const Theme& th = app.theme();
    std::set<std::string> starred_ids;
    for (auto& w : raw_wl) starred_ids.insert(sval(w, "id"));
    std::set<std::string> history_ids;
    for (auto& r : raw_rows) history_ids.insert(sval(r, "id"));
    std::map<std::string, std::vector<BookP>> groups = {{"watching", {}}, {"watched", {}}, {"planning", {}}};
    std::map<std::string, BookP> old;
    for (auto& b : books) old[b->id] = b;  // keep fetched art when an anime shelf is rebuilt
    auto reuse = [&](const std::string& id) {
        auto it = old.find(id);
        BookP b = it != old.end() ? it->second : std::make_shared<Book>();
        return b;
    };
    if (kind == "manga") {
        groups_in["plan"];
        groups.clear();
        for (auto& [k, v] : groups_in) groups[k] = v;  // same book objects, so progress/chapter data survive a relayout
        starred_ids.clear();
        for (auto& m : core::mp_list()) starred_ids.insert(m.value("id", ""));  // manga: the star means "going to read"
    } else {
        std::vector<json> sorted_rows = raw_rows;
        std::stable_sort(sorted_rows.begin(), sorted_rows.end(), [](const json& a, const json& b) { return sval(a, "last") > sval(b, "last"); });
        for (auto& rw : sorted_rows) {
            auto eps = strv(rw, "episodes");
            int watched = (int)std::set<std::string>(eps.begin(), eps.end()).size();
            int total = ival(rw, "total");
            BookP b = reuse(sval(rw, "id"));
            b->id = sval(rw, "id");
            b->title = sval(rw, "title");
            b->episodes = eps;
            b->all_eps = strv(rw, "all_eps");
            b->total = total;
            b->last = sval(rw, "last");
            b->poster_url = b->poster_url;  // history rows carry no poster: found by title when needed
            groups[(total && watched >= total) ? "watched" : "watching"].push_back(b);
        }
        for (auto& w : raw_wl) {
            if (history_ids.count(sval(w, "id"))) continue;
            BookP b = reuse(sval(w, "id"));
            b->id = sval(w, "id");
            b->title = sval(w, "title");
            b->episodes.clear();
            b->all_eps = strv(w, "all_eps");
            b->total = ival(w, "total");
            b->last.clear();
            b->poster_url = sval(w, "poster");
            groups["planning"].push_back(b);
        }
    }
    auto hidden = core::hidden_ids(kind);  // books the user removed from the shelf
    hidden_items = core::hidden_list(kind);
    hidden_count = (int)hidden_items.size();
    for (auto& [k, lst] : groups) lst.erase(std::remove_if(lst.begin(), lst.end(), [&](const BookP& b) { return hidden.count(b->id) > 0; }), lst.end());
    // books you dragged to another shelf stay there (stored in the watch database)
    auto ov = core::shelf_overrides(kind);
    std::set<std::string> valid;
    for (auto& [k, l] : sections) valid.insert(k);
    std::vector<BookP> moved;
    for (auto& [k, lst] : groups) {
        for (auto it = lst.begin(); it != lst.end();) {
            auto o = ov.find((*it)->id);
            if (o != ov.end() && valid.count(o->second.first)) {
                moved.push_back(*it);
                it = lst.erase(it);
            } else {
                ++it;
            }
        }
    }
    std::stable_sort(moved.begin(), moved.end(), [&](const BookP& a, const BookP& b) { return ov[a->id].second < ov[b->id].second; });
    for (auto& b : moved) {
        auto [sec, slot] = ov[b->id];
        auto& lst = groups[sec];
        lst.insert(lst.begin() + (long)std::min<long long>(slot, (long long)lst.size()), b);
    }
    books.clear();
    rows.clear();
    section_info.clear();
    for (auto& [key, label0] : sections) {
        std::string label = label0;
        auto& grp = groups[key];
        if (key == "search" && grp.empty()) continue;  // the librarian's shelf only exists after a search
        if (key == "search" && !search_label.empty()) label = search_label;
        int row = (int)rows.size();
        rows.push_back({key, label});
        double x = -SHELF_W / 2 + 0.4;
        for (size_t k_ = 0; k_ < grp.size(); k_++) {
            Book& bk = *grp[k_];
            int watched = (int)std::set<std::string>(bk.episodes.begin(), bk.episodes.end()).size();
            bool face = k_ == 0;  // the first volume of every section stands face-out, cover showing, like a shop shelf
            double w = face ? FACE_W : VOL_W;
            if (x + w > SHELF_W / 2 - 0.2) {
                row = (int)rows.size();
                rows.push_back({key, ""});
                x = -SHELF_W / 2 + 0.4;
            }
            if (kind == "manga") {  // shelf titles: English by default, Japanese (romaji) when toggled
                if (app.manga_jp) bk.title = !bk.title_jp.empty() ? bk.title_jp : (!bk.title_en.empty() ? bk.title_en : bk.title);
                else bk.title = !bk.title_en.empty() ? bk.title_en : bk.title;
            }
            long long hh = hsh(bk.title);
            double shade = 0.75 + (hh % 5) * 0.08;
            C3 base = th.books[(size_t)(hh % (long long)th.books.size())];
            int length = std::max((int)bk.all_eps.size(), bk.total);  // manga: the chapter count is known before the list loads
            double thick = 1.0 + 0.9 * std::min(std::max(watched, length / 12) / 30.0, 1.0);  // how fat the book is when opened
            bk.watched = watched;
            bk.thick = thick;
            bk.x = x + w / 2;
            bk.row = row;
            bk.w = w;
            bk.h = VOL_H + (hh % 3) * 0.06;
            bk.section = key;
            bk.face = face;
            bk.starred = starred_ids.count(bk.id) > 0;
            bk.color = C3(std::min(1.0, base.r * shade), std::min(1.0, base.g * shade), std::min(1.0, base.b * shade));
            bk.white_spine = hh % 5 < 3;  // most manga have white spines with a coloured band; some are all colour
            books.push_back(grp[k_]);
            x += w + (face ? 0.22 : 0.03);
        }
        section_info[key] = {label, (int)grp.size()};
    }
    if (kind == "manga")  // keep the groups in step with any shelf moves made above
        for (auto& [k, v] : groups) groups_in[k] = v;
    row_y.clear();
    for (size_t i = 0; i < rows.size(); i++) row_y.push_back(0.3 + ROW_GAP * i);
    if (row_y.empty()) {
        rows.push_back({sections[0].first, sections[0].second});
        row_y.push_back(0.3);
    }
    // "next up": the most recently watched book in Watching that isn't finished
    next_up.reset();
    for (size_t i = 0; i < books.size(); i++) {
        const Book& b = *books[i];
        if ((b.section == "watching" || b.section == "reading") && (!b.total || b.watched < b.total)) {
            next_up = (int)i;
            break;
        }
    }
    if (!focus_id.empty())
        for (auto& b : books)
            if (b->id == focus_id) {
                px = b->x;
                level = b->row;
                break;
            }
    level = std::min(level, (int)rows.size() - 1);
    py = row_y[level];
}

// add books to a shelf section after it was built (the manga shelf fills 'popular' in the background); held back while a
// book is open so the open book's index can't shift
void Library::extend(const std::string& group, const std::vector<BookP>& more) {
    auto& g = groups_in[group];
    g.insert(g.end(), more.begin(), more.end());
    dirty = true;
    if (!pick) apply_extend();
}

void Library::apply_extend() {
    dirty = false;
    std::string focus_id = near ? books[*near]->id : "";
    layout(focus_id);
}

// the librarian's results become their own shelf (the bottom one) and the person walks over to it
void Library::set_search(const std::string& query, const std::vector<BookP>& found) {
    groups_in["search"] = found;
    std::vector<std::string> ids;
    for (auto& b : found) ids.push_back(b->id);
    core::shelf_unpurge(kind, ids);  // asking the librarian for it brings a cleared book back
    search_label = "LIBRARIAN: " + su::upper(cut(query, 22));
    mangafire.clear();
    layout();
    for (auto& b : books)
        if (b->section == "search") {
            auto_ = std::make_pair(b->x, b->row);
            break;
        }
}

// take a book off the shelf (only hidden: UNDO / RESTORE bring it back; reading progress is kept)
void Library::remove_book(int i) {
    BookP b = books[i];
    core::shelf_hide(kind, b->id, true, !b->title_en.empty() ? b->title_en : b->title);
    last_removed = b;
    pulse = app.t;
    if (pick && pick->i == i) {
        pick->closing = true;
        bv->spread = bv->target = 0;
        bv->flip.reset();
    }
    app.status = "Removed '" + cut(b->title, 40) + "'  -  Z undoes it, or pick it from the REMOVED drawer on the left";
    if (!pick) {
        FallBook fb;
        fb.x = b->x;
        fb.y0 = row_y[b->row] + 0.05;
        fb.z = 0.0;
        fb.w = b->w;
        fb.h = b->h;
        fb.d = BOOK_D;
        fb.color = b->color;
        falling.push_back(tip_book(fb, 0.8, 0.0, 0.6));
        relayout();
    }
}

void Library::undo_remove() {
    BookP b = last_removed;
    if (!b || pick) return;
    core::shelf_hide(kind, b->id, false);
    last_removed.reset();
    app.status = "Put '" + cut(b->title, 40) + "' back on the shelf";
    layout(b->id);
}

void Library::restore_one(const std::string& id, const std::string& title) {
    core::shelf_hide(kind, id, false);
    if (last_removed && last_removed->id == id) last_removed.reset();
    app.status = "Put '" + cut(title, 40) + "' back on the shelf";
    if (!pick) layout(id);
}

// CLEAR LIST in the drawer: the first click arms it, a second click within 4 s forgets the removed books for good
void Library::clear_removed() {
    if (app.t - clear_armed > 4) {
        clear_armed = app.t;
        app.status = "Click CLEAR LIST again to forget these books for good (their reading progress is deleted too)";
        return;
    }
    clear_armed = -9;
    int n = core::shelf_purge_removed(kind);
    last_removed.reset();
    hidden_items.clear();
    hidden_count = 0;
    app.status = "Cleared " + std::to_string(n) + " removed book" + (n != 1 ? "s" : "") + " - they won't come back to the shelf";
}

void Library::restore_all() {
    core::shelf_unhide_all(kind);
    last_removed.reset();
    app.status = "Restored every removed book";
    layout();
}

// EN/JP toggle: relabel every book (spines, covers, info pages) without rebuilding the shelf
void Library::apply_titles() {
    for (auto& bk : books) {
        if (app.manga_jp) bk->title = !bk->title_jp.empty() ? bk->title_jp : (!bk->title_en.empty() ? bk->title_en : bk->title);
        else bk->title = !bk->title_en.empty() ? bk->title_en : bk->title;
    }
    if (bv) {
        bv->art.clear();
        bv->pending.clear();
        bv->cover_tex.reset();
        bv->cover_job.reset();
    }
}

// index of the book under the ray (shelved books only)
std::optional<int> Library::pick_book(const V3& origin, const V3& direction) const {
    double best = 1e9;
    std::optional<int> bi;
    for (size_t i = 0; i < books.size(); i++) {
        const Book& b = *books[i];
        double y0 = row_y[b.row] + 0.05;
        double lo[3] = {b.x - b.w / 2, y0, -BOOK_D / 2}, hi[3] = {b.x + b.w / 2, y0 + b.h, BOOK_D / 2};
        double o[3] = {origin.x, origin.y, origin.z}, d[3] = {direction.x, direction.y, direction.z};
        double tmin = 0.0, tmax = 1e9;
        bool ok = true;
        for (int a = 0; a < 3; a++) {
            if (std::fabs(d[a]) < 1e-9) {
                if (!(lo[a] <= o[a] && o[a] <= hi[a])) {
                    ok = false;
                    break;
                }
                continue;
            }
            double t1 = (lo[a] - o[a]) / d[a], t2 = (hi[a] - o[a]) / d[a];
            tmin = std::max(tmin, std::min(t1, t2));
            tmax = std::min(tmax, std::max(t1, t2));
        }
        if (ok && tmax >= tmin && tmin < best) {
            best = tmin;
            bi = (int)i;
        }
    }
    return bi;
}

// where the ray meets the plane in front of the shelf
std::optional<std::pair<double, double>> Library::drag_point(const V3& origin, const V3& d) const {
    if (std::fabs(d.z) < 1e-6) return std::nullopt;
    double t = (1.6 - origin.z) / d.z;
    if (t <= 0) return std::nullopt;
    return std::make_pair(origin.x + d.x * t, origin.y + d.y * t);
}

int Library::target_row(double y) const {
    int best = 0;
    for (int r = 1; r < (int)rows.size(); r++)
        if (std::fabs(row_y[r] + BOOK_H / 2 - y) < std::fabs(row_y[best] + BOOK_H / 2 - y)) best = r;
    return best;
}

bool Library::start_drag(const V3& origin, const V3& dir) {
    auto i = drag_candidate;
    drag_candidate.reset();
    auto pt = drag_point(origin, dir);
    if (!i || !pt) return false;
    drag = Drag{*i, pt->first, pt->second, target_row(pt->second)};
    app.status = "Carrying '" + cut(books[*i]->title, 40) + "' - drop it on any shelf";
    return true;
}

void Library::drag_move(const V3& origin, const V3& dir) {
    auto pt = drag_point(origin, dir);
    if (drag && pt) {
        drag->x = pt->first;
        drag->y = pt->second;
        drag->row = target_row(pt->second);
    }
}

// put the carried book on the shelf it hovers over and remember the placement
void Library::drop() {
    auto d = drag;
    drag.reset();
    if (!d) return;
    BookP b = books[d->i];
    std::string section = rows[d->row].section;
    double dx = std::max(-SHELF_W / 2, std::min(SHELF_W / 2, d->x));
    long long slot = 0;
    for (auto& o : books)
        if (o->section == section && o != b && std::make_pair(o->row, o->x) < std::make_pair(d->row, dx)) slot++;
    if (kind == "manga" && (section == "plan") != (b->section == "plan")) {  // dragged onto / off Going To Read
        core::mp_set(b->id, section == "plan", !b->title_en.empty() ? b->title_en : b->title, b->poster_url, b->title_jp);
        b->starred = section == "plan";
    }
    core::shelf_set(kind, b->id, section, slot);
    app.status = "'" + cut(b->title, 40) + "' now lives on the " + su::title_case(label_of(section)) + " shelf";
    layout(b->id);  // also walks the person to the book's new spot
}

void Library::walk_to(int i) {
    const Book& b = *books[i];
    auto_ = std::make_pair(b.x, b.row);
    app.status = "Walking to '" + cut(b.title, 40) + "' - press Enter to pull it out";
}

bool Library::librarian_near() const { return kind == "manga" && level == 0 && px < -SHELF_W / 2 + 4; }

// after a star toggle: re-read the watch list and rebuild the shelves, keeping the person where they were
void Library::relayout() {
    std::string focus_id = near && *near < (int)books.size() ? books[*near]->id : "";
    std::vector<json> wl;
    for (auto& w : core::wl_list()) wl.push_back(w);
    raw_wl = app.wl_with_data(wl, raw_wl);
    layout(focus_id);
}

// manga: put a book on (or take it off) the GOING TO READ shelf; books you've started stay on Reading
void Library::toggle_plan(const BookP& b, std::optional<bool> on_in) {
    bool on;
    if (on_in) on = *on_in;
    else {
        on = true;
        for (auto& m : core::mp_list())
            if (m.value("id", "") == b->id) on = false;
    }
    core::mp_set(b->id, on, !b->title_en.empty() ? b->title_en : b->title, b->poster_url, b->title_jp);
    b->starred = on;
    bool started = !b->episodes.empty();
    if (!started) {  // move the book between shelves (it stays put while it's open in your hands)
        for (auto& [k, lst] : groups_in) lst.erase(std::remove(lst.begin(), lst.end(), b), lst.end());
        auto& dest = groups_in[on ? "plan" : (b->section == "search" ? "search" : "popular")];
        dest.insert(dest.begin(), b);
        core::shelf_clear(kind, b->id);  // a dragged-to placement would fight the new shelf
    }
    app.status = (on ? "'" + cut(b->title, 40) + "' is on your Going To Read shelf" : "Took '" + cut(b->title, 40) + "' off Going To Read") +
                 (started ? " (you've already started it, so it stays on Reading)" : "");
    if (!pick) layout(b->id);
    else dirty = true;  // re-shelve when the book is closed
}

void Library::toggle_star(const BookP& b) {
    if (kind == "manga") return toggle_plan(b);
    app.toggle_star(b->id, b->title, b->poster_url);
    b->starred = app.starred.count(b->id) > 0;
    if (bv) bv->art.clear();
    if (!pick) relayout();
    else if (!b->starred && b->section == "planning") app.status = "Removed from your watch list (it leaves the shelf when you close the book)";
}

void Library::key(const std::string& k) {
    if (pick) {
        if (k == "=" || k == "+") {
            zoom = std::min(zoom * 1.25, 4.0);
            return;
        }
        if (k == "-" || k == "_") {
            zoom = std::max(zoom / 1.25, 0.6);
            return;
        }
        if (k == "0") {
            zoom = 1.0;
            pan[0] = pan[1] = 0;
            focus = "spread";
            return;
        }
        if (k == "z") {
            cycle_focus();
            return;
        }
        if (pick->u >= bookc::TOTAL - 0.05 && bv->key(k)) return;
        if (k == "Enter" || k == "e") {
            if (pick->u < bookc::TOTAL - 0.05) pick->u = bookc::TOTAL;  // skip ahead to the open book
            else if (kind == "manga") app.read_chapter(books[pick->i], bv->mode == "toc" ? bv->next_ep() : bv->chapter);
            else app.open_history_book(books[pick->i]);
        } else if (k == "Escape" || k == "Backspace") {
            pick->closing = true;
            bv->spread = bv->target = 0;
            bv->flip.reset();
        } else if (k == "f") {
            toggle_star(books[pick->i]);
        } else if ((k == "x" || k == "Delete") && kind == "manga") {
            remove_book(pick->i);
        }
        return;
    }
    if ((k == "Enter" || k == "e") && near) {
        pick = Pick{*near, 0.0, false};
        bv = std::make_shared<BookView>(app, books[*near], kind, app.manga_rtl);
        if (kind == "manga") app.ensure_chapters(books[*near]);  // fetch the chapter list while the book is opening
        zoom = 1.0;
        pan[0] = pan[1] = 0;
        focus = "spread";
        auto_.reset();
        app.fetch_book_poster(books[*near]);
    } else if (k == "c" && next_up) {
        const Book& b = *books[*next_up];
        auto_ = std::make_pair(b.x, b.row);
        app.status = "Walking to your next-up book: " + b.title;
    } else if (k == "f" && near) {
        toggle_star(books[*near]);
    } else if ((k == "x" || k == "Delete") && kind == "manga" && near) {
        remove_book(*near);
    } else if (k == "z" && kind == "manga") {
        undo_remove();
    } else if (k == "/" && kind == "manga") {
        app.lib_typing = true;
        app.swallow = "/";
    } else if (k == "p" && kind == "manga") {
        app.toggle_manga_direction();
    } else if (k == "j" && kind == "manga") {
        app.toggle_titles();
    } else if (k == "k") {
        app.start_game("kong", "library");
    } else if (k == "m") {
        app.open_library(kind == "manga" ? "anime" : "manga");
    }
}

// Z: whole spread -> the page you read first -> the other page -> whole spread
void Library::cycle_focus() {
    std::string first = bv ? bv->first_side() : "L";
    std::string other = first == "R" ? "L" : "R";
    if (focus == "spread") focus = first;
    else if (focus == first) focus = other;
    else focus = "spread";
    pan[0] = pan[1] = 0;
    std::string what = focus == "spread" ? "Whole spread" : focus == "L" ? "Zoomed on the left page" : "Zoomed on the right page";
    app.status = what + "  (Z to cycle, wheel to zoom, drag to pan)";
}

void Library::zoom_by(double dy) {
    if (bv && bv->scrolling()) return bv->scroll_by(dy * 0.008);  // a scroll: the wheel scrolls it (+ / - still zoom)
    zoom = std::min(std::max(zoom / std::pow(1.0015, dy), 0.6), 4.0);
}

void Library::pan_by(double dx, double dy) {
    double s = app.scene.cam.dist * 0.0016;
    if (bv && bv->scrolling()) {  // drag the paper up to read on
        bv->scroll_by(-dy * s);
        bv->sy = bv->sy_goal;
        pan[0] -= dx * s;
        return;
    }
    pan[0] -= dx * s;
    pan[1] += dy * s;
}

bool Library::click(const V3& origin, const V3& direction) {
    if (!pick && !drag) {
        if (kind == "manga" && std::fabs(direction.z) > 1e-6) {  // click the librarian: ask a question
            double t = (PERSON_Z - 0.5 - origin.z) / direction.z;
            double x = origin.x + direction.x * t, y = origin.y + direction.y * t;
            if (t > 0 && std::fabs(x - LIBRARIAN_X) < 3.4 && row_y[0] - 0.5 < y && y < row_y[0] + 6.5) {
                app.lib_typing = true;
                return true;
            }
        }
        auto i = pick_book(origin, direction);
        if (i) {
            walk_to(*i);
            return true;
        }
    }
    if (pick && pick->u >= bookc::TOTAL - 0.05 && bv) {
        auto h = bv->hit(origin, direction);
        if (h) {
            if (kind == "manga") app.read_chapter(books[pick->i], h->ep);
            else app.play_from_book(books[pick->i], h->ep);
            return true;
        }
    }
    return false;
}

void Library::update(double dt, const Keys& keys) {
    falling.erase(std::remove_if(falling.begin(), falling.end(), [&](FallBook& b) { return !step_book(b, dt, 0.0); }), falling.end());
    double goal = (drawer && !pick) ? 1.0 : 0.0;
    drawer_t += (goal - drawer_t) * (1 - std::exp(-dt * 12));
    if (pick) {
        pick->u = std::min(bookc::TOTAL, std::max(0.0, pick->u + dt * (pick->closing ? -2.2 : 1.0)));
        if (pick->u >= bookc::TOTAL) bv->update(app.t);
        if (pick->closing && pick->u <= 0) {
            pick.reset();
            bv.reset();
            if (dirty) apply_extend();
            else relayout();
        }
        return;
    }
    int move = (held(keys, {"ArrowRight", "d"}) ? 1 : 0) - (held(keys, {"ArrowLeft", "a"}) ? 1 : 0);
    if (auto_) {
        auto [tx, tl] = *auto_;
        if (tl != level && std::fabs(py - row_y[level]) < 0.05) level += tl > level ? 1 : -1;
        if (std::fabs(px - tx) > 0.15 && level == tl) move = tx > px ? 1 : -1;
        else if (level == tl && std::fabs(py - row_y[tl]) < 0.05) auto_.reset();
    } else {
        if (held(app.pressed, {"ArrowUp", "w"})) level = std::min(level + 1, (int)rows.size() - 1);
        if (held(app.pressed, {"ArrowDown", "s"})) level = std::max(level - 1, 0);
    }
    app.pressed.clear();
    moving = move != 0 || std::fabs(py - row_y[level]) > 0.05;
    if (move) {
        facing = move;
        px = std::max(-SHELF_W / 2, std::min(SHELF_W / 2, px + move * 9 * dt));
    }
    py += (row_y[level] - py) * (1 - std::exp(-dt * 7));
    near.reset();
    for (size_t i = 0; i < books.size(); i++) {
        const Book& b = *books[i];
        if (b.row == level && std::fabs(px - b.x) <= b.w / 2 + 0.25) {
            near = (int)i;
            break;
        }
    }
    // manga: count the real number of chapters (all sources) for the book you stand at, so the shelf shows the whole series
    if (kind == "manga" && near) {
        if (near != dwell_i) {
            dwell_i = near;
            dwell_t = 0.0;
        }
        dwell_t += dt;
        BookP b = books[*near];
        if (dwell_t > 0.6 && !b->chapters && !b->loading) app.ensure_chapters(b, nullptr, true);
    }
}

// where the opened book is held: centre of the screen, in front of the shelf
V3 Library::display() const { return {px, 6.6 + level * 0.8, 7.0}; }

CamGoal Library::camera() const {
    double nrows = (double)rows.size();
    V3 base{px * 0.45, 5.4 + level * 0.9, 0.0};
    double far = 27 + std::max(0.0, nrows - 3) * 3.5;
    if (!pick) return {base, 0.0, 9 * PI / 180, far};
    double f = ease(pick->u / 0.9);
    V3 d = display();
    double z = zoom;
    if (bv && bv->scrolling()) z *= 1.25 * (focus != "spread" ? 1.5 : 1.0);  // a scroll is narrower than an open book: come in closer
    else if (focus != "spread" && bv) {  // fit one page to the screen
        d.x += (focus == "R" ? 1 : -1) * (bv->LP / 2 + 0.03);
        z *= 1.9;
    }
    V3 goal{d.x + pan[0], d.y + pan[1], d.z - 1.0};
    V3 tgt{bookc::lerp(base.x, goal.x, f), bookc::lerp(base.y, goal.y, f), bookc::lerp(base.z, goal.z, f)};
    double dist = bookc::lerp(far, (kind == "manga" ? 19.0 : 15.5) / z, f);
    return {tgt, 0.0, bookc::lerp(9 * PI / 180, 11 * PI / 180, f), std::max(dist, 6.0)};
}

std::vector<Box> Library::build(Renderer& r, TextCache& text, const Theme& th, double t) {
    std::vector<Box> out = bookcase(r, text, th, t);
    auto add = [&](const std::vector<Box>& v) { out.insert(out.end(), v.begin(), v.end()); };
    if (drag) out.push_back(box(0, row_y[drag->row] - 0.05, 0.3, SHELF_W + 1.0, 0.16, 2.8, th.accent, 1.6));  // the shelf the carried book would land on
    for (size_t i = 0; i < books.size(); i++) {
        if (pick && pick->i == (int)i) continue;  // that book is in the person's hands
        if (drag && drag->i == (int)i) continue;
        double s = ease((t - app.lib_t0 - 0.03 * i) / 0.5);
        if (s <= 0) continue;
        Book& b = *books[i];
        bool hot = near && *near == (int)i;
        double y0 = row_y[b.row] + 0.05;
        double hgt = b.h * s;
        add(volume(r, text, th, books[i], y0, s, hot));
        double frac = b.total ? (double)b.watched / b.total : 0.0;
        if (frac > 0 && !b.face) {  // progress ribbon: a bookmark tassel hanging out of the top, length = how far you are
            out.push_back(box(b.x + b.w * 0.22, y0 + hgt + 0.1, BOOK_D / 2 - 0.25, 0.1, 0.24, 0.06, th.ep_seen, 0.9));
            out.push_back(box(b.x - b.w / 2 - 0.02, y0 + hgt * (1 - std::min(frac, 1.0) / 2), BOOK_D / 2 - 0.2, 0.04, hgt * std::min(frac, 1.0), 0.12, th.ep_seen, 0.8));
        }
        if (b.starred) out.push_back(box(b.x, y0 + b.h * s + 0.4, 0.2, 0.55, 0.55, 0.3, GOLD, 1.4, PI / 4 + 0.3 * std::sin(t * 2)));
        if (next_up && *next_up == (int)i) out.push_back(box(b.x, y0 + b.h + 1.2 + 0.25 * std::sin(t * 3), 0.3, 0.5, 0.5, 0.5, th.ep_play, 1.3, t * 2));
    }
    for (auto& bk : falling) add(book_boxes(bk, 0.3));
    if (drag) {
        const Book& b = *books[drag->i];
        double sway = 0.12 * std::sin(t * 7);
        out.push_back(box(drag->x, drag->y, 2.0, b.w, b.h, BOOK_D, b.color, 1.0, sway));
        spine(r, text, b, mx::translate(drag->x, drag->y, 2.0) * mx::rot_y(sway), 1.0, C4(1, 1, 1, 1));
    }
    if (pick) add(open_book(r, text, th, t));
    if (kind == "manga") add(librarian(r, text, th, t));
    add(person(t, th));
    return out;
}

// the librarian: glasses, a bun, a desk with a lamp; click her (or press /) to search for a manga
std::vector<Box> Library::librarian(Renderer& r, TextCache& text, const Theme& th, double t) {
    std::vector<Box> out;
    double x = LIBRARIAN_X, y = row_y[0], z = PERSON_Z - 0.3;
    const double sc = 1.25;  // a little bigger than the person who browses, so she reads from across the room
    out.push_back(box(x, y + 0.9, z + 1.4, 4.4, 1.8, 1.8, th.wood, 0.25));                  // desk
    out.push_back(box(x - 1.3, y + 2.1, z + 1.4, 1.1, 0.6, 0.9, {0.9, 0.7, 0.2}, 0.7));      // stack of books
    out.push_back(box(x - 1.3, y + 2.6, z + 1.4, 0.9, 0.4, 0.8, {0.7, 0.2, 0.2}, 0.7));
    out.push_back(box(x + 1.5, y + 2.6, z + 1.4, 0.15, 1.5, 0.15, {0.7, 0.7, 0.75}, 0.4));   // lamp stand
    out.push_back(box(x + 1.5, y + 3.5, z + 1.4, 0.9, 0.45, 0.9, {1.0, 0.9, 0.5}, 1.9));
    auto p = voxel_person(th, x, y, z - 0.2, 1, std::sin(t * 2) * 0.05, ((long long)(t * 0.5)) % 2 == 0 && !app.lib_typing, sc);
    out.insert(out.end(), p.begin(), p.end());
    double hy = y + 2.05 * sc;
    out.push_back(box(x - 0.28 * sc, hy, z + 0.2 + 0.35 * sc, 0.34 * sc, 0.26 * sc, 0.1, {0.1, 0.1, 0.12}, 0.0));  // glasses
    out.push_back(box(x + 0.28 * sc, hy, z + 0.2 + 0.35 * sc, 0.34 * sc, 0.26 * sc, 0.1, {0.1, 0.1, 0.12}, 0.0));
    out.push_back(box(x, hy, z + 0.2 + 0.35 * sc, 0.12, 0.06, 0.1, {0.1, 0.1, 0.12}, 0.0));
    out.push_back(box(x, y + 2.9 * sc, z - 0.5, 0.65 * sc, 0.55 * sc, 0.6 * sc, {0.55, 0.3, 0.2}, 0.2));  // hair bun
    TextTex sign = text.get("LIBRARIAN  -  click or press /", 36, th.accent);
    double ww = std::min(7.5, 0.62 * sign.w / sign.h);
    r.quad3d(sign.tex, mx::translate(x - ww / 2, y + 5.2, z + 0.9) * mx::scale(ww, -ww * sign.h / sign.w, 1));
    if (app.lib_typing) {
        std::string msg = "Looking for: " + app.lib_in.text() + (((long long)(t * 2)) % 2 == 0 ? "|" : " ");
        auto cps = su::utf8_decode(msg);
        if (cps.size() > 34) msg = su::utf8_encode(std::vector<uint32_t>(cps.end() - 34, cps.end()));
        TextTex bub = text.get255(msg, 34, 30, 30, 40);
        double bw = std::min(7.0, 0.5 * bub.w / bub.h);
        double bh = bw * bub.h / bub.w;
        out.push_back(box(x + 4.0, y + 6.4, z + 0.8, bw + 0.8, bh + 0.5, 0.2, {0.96, 0.96, 0.9}, 0.9, 0.0, 0.2));
        r.quad3d(bub.tex, mx::translate(x + 4.0 - bw / 2, y + 6.4 + bh / 2, z + 0.92) * mx::scale(bw, -bh, 1));
    }
    return out;
}

// a proper bookcase: plank back, pilasters, plinth and crown molding, shelves with a lip and a light strip, brass plaques
std::vector<Box> Library::bookcase(Renderer& r, TextCache& text, const Theme& th, double t) {
    std::vector<Box> out;
    C3 wood = th.wood;
    C3 dark = wood * 0.6;
    C3 light(std::min(1.0, wood.r * 1.35 + 0.04), std::min(1.0, wood.g * 1.35 + 0.04), std::min(1.0, wood.b * 1.35 + 0.04));
    double top = row_y.back() + BOOK_H + 1.4;
    const int n_pl = 12;
    for (int k = 0; k < n_pl; k++) {  // back panel: vertical planks in slightly different shades
        double pw = (SHELF_W + 1.2) / n_pl;
        double shade = 0.5 + 0.08 * ((k * 7) % 3);
        out.push_back(box(-SHELF_W / 2 - 0.6 + pw * (k + 0.5), top / 2, -1.25, pw - 0.04, top, 0.3, wood * shade, 0.03, 0.0, 0.2));
    }
    for (int sx : {-1, 1}) {  // sides: a panel with a pilaster on the front edge
        out.push_back(box(sx * (SHELF_W / 2 + 0.6), top / 2, 0, 0.5, top, 2.8, wood, 0.12, 0.0, 0.3));
        out.push_back(box(sx * (SHELF_W / 2 + 0.68), top / 2, 1.45, 0.7, top, 0.2, light, 0.12, 0.0, 0.3));
        for (double yy : {0.6, top - 0.6}) out.push_back(box(sx * (SHELF_W / 2 + 0.68), yy, 1.55, 0.85, 0.3, 0.1, dark, 0.1, 0.0, 0.3));
    }
    out.push_back(box(0, 0.08, 0.0, SHELF_W + 2.6, 0.16, 2.9, dark, 0.08, 0.0, 0.3));  // plinth
    out.push_back(box(0, top, 0, SHELF_W + 1.7, 0.4, 2.8, wood, 0.12, 0.0, 0.3));      // crown molding, stepped
    out.push_back(box(0, top + 0.3, 0.1, SHELF_W + 2.3, 0.2, 3.1, light, 0.12, 0.0, 0.3));
    out.push_back(box(0, top + 0.48, 0.15, SHELF_W + 2.7, 0.16, 3.3, dark, 0.1, 0.0, 0.3));
    C3 warm{1.0, 0.82, 0.5};
    for (size_t i = 0; i < row_y.size(); i++) {
        double y = row_y[i];
        out.push_back(box(0, y - 0.15, 0, SHELF_W + 1.2, 0.3, 2.8, wood, 0.15, 0.0, 0.3));     // shelf board
        out.push_back(box(0, y - 0.13, 1.42, SHELF_W + 1.2, 0.36, 0.12, light, 0.15, 0.0, 0.3));  // front lip
        double light_y = i + 1 < row_y.size() ? row_y[i + 1] - 0.36 : top - 0.24;
        out.push_back(box(0, light_y, 1.15, SHELF_W + 0.8, 0.05, 0.12, warm, 1.7, 0.0, 0.0));  // light strip under the shelf above
        double end = -1e9;
        bool any = false;
        for (auto& b : books)
            if (b->row == (int)i) {
                end = std::max(end, b->x + b->w / 2);
                any = true;
            }
        if (any) {  // metal bookends holding the row
            end += 0.12;
            if (end < SHELF_W / 2 - 0.3) {
                out.push_back(box(end, y + 1.1, 0.0, 0.1, 2.2, 1.5, {0.25, 0.25, 0.28}, 0.25, 0.0, 0.3));
                out.push_back(box(end - 0.35, y + 0.04, 0.0, 0.8, 0.06, 1.5, {0.25, 0.25, 0.28}, 0.25, 0.0, 0.3));
            }
        }
    }
    for (size_t i = 0; i < rows.size(); i++) {  // brass plaques on the shelf lip naming each section
        const Row& rw = rows[i];
        if (rw.label.empty()) continue;
        int n = section_info[rw.section].second;
        std::string msg = (n || kind == "manga") ? rw.label + " (" + std::to_string(n) + ")" : rw.label + " (0) - press F on a book";
        TextTex tt = text.get255(msg, 44, 40, 26, 8);
        double hh = 0.5;
        double ww = std::min(hh * tt.w / tt.h, 9.0);
        hh = ww * tt.h / tt.w;
        double ppx = SHELF_W / 2 - 0.8 - ww / 2;
        double ppy = std::max(row_y[i] - 0.13, hh / 2 + 0.2);  // the bottom one sits just above the plinth
        out.push_back(box(ppx, ppy, 1.5, ww + 0.5, hh + 0.16, 0.06, BRASS, 0.55, 0.0, 0.3));
        r.quad3d(tt.tex, mx::translate(ppx - ww / 2, ppy + hh / 2, 1.54) * mx::scale(ww, -hh, 1));
    }
    // on top: a potted plant and a little stack of volumes lying flat
    double ppx = SHELF_W / 2 - 3.0, ppy = top + 0.56;
    out.push_back(box(ppx, ppy + 0.6, 0.2, 1.2, 1.2, 1.2, {0.72, 0.36, 0.2}, 0.2, 0.0, 0.3));
    out.push_back(box(ppx, ppy + 1.22, 0.2, 1.25, 0.1, 1.25, {0.3, 0.2, 0.12}, 0.1, 0.0, 0.0));
    for (int k = 0; k < 7; k++) {
        double a = k / 7.0 * TAU + 0.3;
        out.push_back(box(ppx + std::cos(a) * 0.5, ppy + 1.9 + 0.25 * (k % 2), 0.2 - std::sin(a) * 0.5, 1.3, 0.12, 0.42, {0.25, 0.55, 0.2}, 0.2, a, 0.2, 0.0, 0.0,
                          0.5 + 0.06 * std::sin(t * 1.3 + k)));
    }
    double sx = -SHELF_W / 2 + 3.0;
    for (int k = 0; k < 4; k++) {
        C3 col = th.books[k % th.books.size()];
        out.push_back(box(sx + 0.05 * (k % 2), ppy + 0.13 + k * 0.26, 0.2, VOL_H, 0.24, BOOK_D, CREAM, 0.1, 0.12 * (k - 1.5), 0.3));
        out.push_back(box(sx + 0.05 * (k % 2) - VOL_H / 2 + 0.3, ppy + 0.13 + k * 0.26, 0.2, 0.5, 0.25, BOOK_D + 0.01, col, 0.3, 0.12 * (k - 1.5), 0.0));
    }
    return out;
}

// one manga volume on the shelf: spine out (colour band with the volume number, the title, a publisher mark), or face-out
std::vector<Box> Library::volume(Renderer& r, TextCache& text, const Theme& th, const BookP& bp, double y0, double s, bool hot) {
    Book& b = *bp;
    std::vector<Box> out;
    double x = b.x, w = b.w, h = b.h * s;
    C3 col = b.color;
    double glow = 0.12 + (hot ? 0.9 : 0);
    C4 tint = s > 0.8 ? C4(1, 1, 1, 1) : C4(1, 1, 1, 0);
    if (b.face) {
        double z = 0.45, depth = 0.34, lean = -0.07;
        double yc = y0 + h / 2;
        out.push_back(box(x, yc, z, w, h, depth, col, glow, 0.0, 0.3, lean));
        out.push_back(box(x + 0.03, yc, z - 0.02, w - 0.08, h - 0.06, depth + 0.02, CREAM, 0.1, 0.0, 0.0, lean));  // page block
        TexP tex = b.card_tex;
        if (!tex) {
            if (b.poster_img) tex = b.card_tex = app.r.make_texture(b.poster_img->rgba.data(), b.poster_img->w, b.poster_img->h);
            else if (!b.poster_loading) app.fetch_book_poster(bp);
        }
        M4 M = mx::translate(x, yc, z) * mx::rot_x(lean) * mx::translate(-w / 2 + 0.05, h / 2 - 0.05, depth / 2 + 0.012);
        if (tex) {
            r.quad3d(tex, M * mx::scale(w - 0.1, -(h - 0.1), 1), tint);
        } else {  // no art yet: the title printed on the cover
            TextTex tt = text.get(cut(b.title, 22), 40, {1, 1, 1});
            double ww = std::min(w - 0.3, 0.32 * tt.w / tt.h);
            r.quad3d(tt.tex, M * mx::translate(0.1, -0.4, 0) * mx::scale(ww, -ww * tt.h / tt.w, 1), tint);
        }
        return out;
    }
    C3 base = b.white_spine ? CREAM : col;
    out.push_back(box(x, y0 + h / 2, 0, w, h, BOOK_D, base, glow, 0.0, 0.3));
    out.push_back(box(x, y0 + h - 0.015, -0.05, w * 0.82, 0.04, BOOK_D * 0.9, {0.98, 0.97, 0.92}, 0.1, 0.0, 0.0));  // page edges on top
    double band = 0.62 * s;
    double fz = BOOK_D / 2 + 0.006;
    out.push_back(box(x, y0 + h - 0.16 - band / 2, fz, w + 0.004, band, 0.012, b.white_spine ? col : CREAM, 0.35, 0.0, 0.0));
    out.push_back(box(x, y0 + 0.32 * s, fz, w * 0.5, w * 0.5 * s, 0.012, b.white_spine ? col : CREAM, 0.35, 0.0, 0.0));  // publisher mark
    if (s > 0.8) {
        int n = b.total ? b.total : (int)b.all_eps.size();
        if (!n) n = 9;
        std::string vol = std::to_string(std::max(1, n / 9));  // ~9 chapters a volume
        TextTex vt = !b.white_spine ? text.get255(vol, 40, 20, 20, 24) : text.get255(vol, 40, 255, 255, 255);
        double nh = 0.3;
        double nw = nh * vt.w / vt.h;
        if (nw > w * 0.9) {
            nw = w * 0.9;
            nh = w * 0.9 * vt.h / vt.w;
        }
        r.quad3d(vt.tex, mx::translate(x - nw / 2, y0 + h - 0.16 - band / 2 + nh / 2, fz + 0.01) * mx::scale(nw, -nh, 1), tint);
        C3 ink = b.white_spine ? C3(25 / 255.0, 25 / 255.0, 30 / 255.0) : C3(1, 1, 1);
        spine(r, text, b, mx::translate(x, y0 + 0.62 + (h - 0.62 - 0.16 - band) / 2, 0), 1.0, tint, h - band - 0.95, ink);
    }
    return out;
}

void Library::spine(Renderer& r, TextCache& text, const Book& b, const M4& M, double scale, C4 tint, double avail, C3 color) {
    TextTex tt = text.get(cut(b.title, 26), 40, color);
    double sw = b.face ? VOL_W : b.w;
    double texth = std::min(sw * 0.62, 0.85);
    double texw = texth * tt.w / tt.h;
    double room = avail >= 0 ? avail : b.h * 0.92;
    if (texw > room) {
        texw = room;
        texth = room * tt.h / tt.w;
    }
    M4 m = M * mx::translate(0, 0, BOOK_D * scale / 2 + 0.05) * mx::rot_z(PI / 2) * mx::translate(-texw / 2 * scale, texth / 2 * scale, 0) * mx::scale(texw * scale, -texth * scale, 1);
    r.quad3d(tt.tex, m, tint);
}

std::vector<Box> Library::open_book(Renderer& r, TextCache& text, const Theme& th, double t) {
    Book& b = *books[pick->i];
    double u = pick->u;
    V3 d = display();
    if (u < 0.9) {  // the closed book slides off the shelf, turns to face you, leans back and grows to reading size
        double a = ease(u / 0.9);
        double sx = b.x, sy = row_y[b.row] + 0.05 + b.h / 2;
        V3 pos{bookc::lerp(sx, d.x, a), bookc::lerp(sy, d.y, a) + std::sin(a * PI) * 0.8, bookc::lerp(0.0, d.z, a)};
        double yaw = -PI / 2 * a;
        if (bv->rtl) yaw = PI / 2 * a;  // right-to-left binding: the spine ends up on the right
        double P = bookc::book_thickness(b);
        double dimx = bookc::lerp(b.w, P, a), dimy = bookc::lerp(b.h, bv->HC, a), dimz = bookc::lerp(BOOK_D, bv->WC, a);
        double sc = bookc::lerp(1.0, bookc::S, a);
        double tilt = bookc::tilt_for(u);
        spine(r, text, b, mx::translate(pos.x, pos.y, pos.z) * mx::rot_x(tilt) * mx::rot_y(yaw), sc, C4(1, 1, 1, 1));
        return {box(pos.x, pos.y, pos.z, dimx, dimy, dimz, b.color, 1.0, yaw, 0.3, tilt, 1.0)};
    }
    return bv->build(r, text, th, t, u, d);
}

std::vector<Box> Library::person(double t, const Theme& th) {
    std::vector<Box> out;
    double x = px, y = py, z = PERSON_Z;
    const double sc = 0.75;
    double swing = moving && !pick ? std::sin(t * 12) * 0.35 : 0.0;
    C3 skin{0.96, 0.80, 0.66}, shirt = th.ped_sel, hair = th.core;
    double f = facing;
    for (double dx : {-0.55, 0.55}) out.push_back(box(x + dx, y / 2 + 0.1, 1.55, 0.1, y + 0.2, 0.1, th.wood, 0.1));  // rolling ladder
    for (int k = 1; k <= (int)(y / 0.9); k++) out.push_back(box(x, k * 0.9, 1.55, 1.1, 0.08, 0.1, th.wood, 0.1));
    bool holding = (bool)pick;
    out.push_back(box(x - 0.4 * sc, y + 0.45 * sc, z + swing * 0.5, 0.56 * sc, 0.9 * sc, 0.6 * sc, {0.15, 0.15, 0.3}, 0.0));
    out.push_back(box(x + 0.4 * sc, y + 0.45 * sc, z - swing * 0.5, 0.56 * sc, 0.9 * sc, 0.6 * sc, {0.15, 0.15, 0.3}, 0.0));
    out.push_back(box(x, y + 1.3 * sc, z, 0.95 * sc, 0.95 * sc, 0.55 * sc, shirt, 0.2));
    out.push_back(box(x, y + 2.1 * sc, z, 0.75 * sc, 0.7 * sc, 0.7 * sc, skin, 0.1));
    out.push_back(box(x, y + 2.5 * sc, z, 0.85 * sc, 0.25 * sc, 0.8 * sc, hair, 0.3));
    out.push_back(box(x + f * 0.42 * sc, y + 2.05 * sc, z + 0.3 * sc, 0.12, 0.12, 0.15, skin, 0.0));
    double arm_y = y + (holding ? 1.95 : 1.3) * sc;
    for (int sx : {-1, 1}) out.push_back(box(x + sx * 0.62 * sc, arm_y, z + (holding ? 0.35 * sc : 0), 0.24 * sc, 0.8 * sc, 0.24 * sc, skin, 0.05));
    return out;
}

// a slide-out drawer on the left edge listing removed books: restore any one of them, or all
void Library::removed_drawer(double W, double H) {
    const Theme& th = app.theme();
    double pw = 320, top = 150;
    double ph = H - top - 144;
    double x = -pw + pw * (drawer_t * drawer_t * (3 - 2 * drawer_t));
    bool glow = pulse > -1 && app.t - pulse < 1.6;
    std::string tab_label = hidden_count ? "REMOVED (" + std::to_string(hidden_count) + ")" : "REMOVED";
    app.r.rect(x + pw, top, 34, 190, C4(th.edge.r, th.edge.g, th.edge.b, (glow && ((long long)(app.t * 6)) % 2 == 0) ? 0.85 : 0.45));
    app.button(drawer ? "<" : ">", x + pw, top, 34, 34, [this] { drawer = !drawer; }, drawer);
    auto cps = su::utf8_decode(tab_label);
    for (size_t i = 0; i < cps.size() && i < 11; i++) app.txt(su::utf8_encode({cps[i]}), x + pw + 17, top + 38 + i * 13, 12, {1, 1, 1}, true);  // one letter per line
    app.hits.push_back({x + pw, top + 34, 34, 156, [this](double, double) { drawer = !drawer; }});
    if (drawer_t < 0.02) return;
    app.r.rect(x, top, pw, ph, C4(0.01, 0.015, 0.03, 0.96));
    app.panel(x, top, pw, ph);
    app.txt("REMOVED FROM SHELF", x + 16, top + 12, 16, th.accent);
    if (!hidden_items.empty()) {
        double half = (pw - 40) / 2;
        app.button("RESTORE ALL (" + std::to_string(hidden_items.size()) + ")", x + 16, top + 42, half, 30, [this] { restore_all(); });
        bool armed = app.t - clear_armed < 4;
        app.button(armed ? "SURE? CLICK AGAIN" : "CLEAR LIST", x + 24 + half, top + 42, half, 30, [this] { clear_removed(); }, armed);
    } else {
        app.txt("Nothing removed.", x + 16, top + 50, 13, th.dim);
        app.txt("Press X on a book to take it off.", x + 16, top + 72, 11, th.dim);
    }
    int nrows = std::max(1, (int)((ph - 96) / 46));
    drawer_scroll = std::max(0, std::min(drawer_scroll, std::max(0, (int)hidden_items.size() - nrows)));
    for (int k = 0; k < nrows && drawer_scroll + k < (int)hidden_items.size(); k++) {
        auto [iid, title] = hidden_items[drawer_scroll + k];
        double ry = top + 84 + k * 46;
        app.r.rect(x + 12, ry, pw - 24, 40, C4(0, 0, 0, 0.4));
        app.txt(cut(title, 30), x + 20, ry + 4, 13, {1, 1, 1});
        app.button("RESTORE", x + pw - 108, ry + 21, 90, 18, [this, iid = iid, title = title] { restore_one(iid, title); });
    }
    if ((int)hidden_items.size() > nrows)
        app.txt(std::to_string(drawer_scroll + 1) + "-" + std::to_string(std::min(drawer_scroll + nrows, (int)hidden_items.size())) + " of " +
                    std::to_string(hidden_items.size()) + "  (wheel scrolls)",
                x + 16, top + ph - 22, 10, th.dim);
}

std::string Library::section_here() const { return rows[level].section; }

void Library::hud(double W, double H) {
    const Theme& th = app.theme();
    app.txt("THE LIBRARY  -  " + label_of(section_here()), W / 2, 66, 18, th.accent, true);
    if (pick) {
        BookP b = books[pick->i];
        app.txt(cut(b->title, 44) + (b->starred ? "  [*]" : ""), W / 2, 92, 16, {1, 1, 1}, true);
        if (pick->u >= bookc::TOTAL - 0.05) {
            BookView* v = bv.get();
            std::string msg;
            if (kind == "manga") {
                std::string direction = v->rtl ? "right to left" : "left to right";
                if (v->scrolling()) msg = "wheel / W S / arrows scroll down   A D a screen at a time   [ ] chapters   O list   V read as book   +/- zoom   Z zoom width   U hide UI   Esc close";
                else if (v->mode == "read") msg = "arrows / A D turn page (" + direction + ")   [ ] chapters   O list   G direction   wheel/+/- zoom   Z fit page   U hide UI   Esc close";
                else msg = "click a chapter to read it   C continue page   Enter continue reading   G direction   wheel zoom   Esc close";
            } else {
                msg = "A/D or arrows flip   W/S jump 5   C continue page   click an episode to play   F star   Enter open anime   Esc close";
            }
            app.txt(msg, W / 2, 118, 12, th.dim, true);
            int n = v->n_spreads();
            double cx = W / 2, y = H - 70;
            int ld = v->left_delta();
            app.button(!v->rtl ? "|<" : ">|", cx - 215, y, 56, 34, [v] { v->jump(!v->rtl); });
            app.button("<", cx - 150, y, 56, 34, [v, ld] { v->go(ld); });
            app.button(">", cx + 94, y, 56, 34, [v, ld] { v->go(-ld); });
            app.button(!v->rtl ? ">|" : "|<", cx + 159, y, 56, 34, [v] { v->jump(v->rtl); });
            std::string lab = "SPREAD " + std::to_string(v->target + 1) + "/" + std::to_string(n);
            if (v->scrolling()) lab = "CH " + v->chapter + "  -  page " + std::to_string(v->strip_page().first + 1) + "/" + std::to_string(v->pages_urls.size());
            else if (v->mode == "read") lab = "CH " + v->chapter + "  -  " + std::to_string(v->target + 1) + "/" + std::to_string(n);
            app.txt(lab, cx, y + 8, 14, th.text, true);
            app.button("HIDE UI (U)", W - 560, y - 42, 110, 34, [this] { app.on_key("u"); });
            const char* zl[4] = {"ZOOM -", "ZOOM +", "FIT PAGE (Z)", "RESET"};
            const char* zk[4] = {"-", "=", "z", "0"};
            for (int i = 0; i < 4; i++) app.button(zl[i], W - 440 + i * 108 - (i == 2 ? 8 : 0), y - 42, 100 + (i == 2 ? 16 : 0), 34, [this, k = std::string(zk[i])] { key(k); });
            if (v->manga) {
                app.button(v->rtl ? "READ: R>L" : "READ: L>R", 22, y, 120, 34, [v] { v->key("g"); });
                if (v->mode == "read") app.button(v->scrolling() ? "BOOK (V)" : "SCROLL (V)", 150, y, 110, 34, [v] { v->key("v"); });
                app.button("OPEN IN DOCUMENT READER", 22, y - 42, 232, 34, [this, b, v] {
                    app.export_chapter(b, v->mode == "read" ? std::optional<std::string>(v->chapter) : std::nullopt);
                });
                app.button("MANGAFIRE", 22, y - 84, 120, 34, [this, b] { app.open_mangafire(b->title); });
                app.button("REMOVE FROM SHELF (X)", 150, y - 84, 206, 34, [this] {
                    if (pick) remove_book(pick->i);
                });
            }
            if (v->manga && v->mode == "read") {
                app.button("CHAPTERS (O)", cx - 330, y, 100, 34, [v] { v->key("o"); });
                app.button("[ PREV", cx + 240, y, 80, 34, [v] { v->key("["); });
                app.button("NEXT ]", cx + 326, y, 80, 34, [v] { v->key("]"); });
            }
            if (v->hover) {
                const std::string& ep = v->hover->ep;
                std::string unit = v->manga ? "Chapter" : "Episode";
                app.txt(unit + " " + ep + (v->watched().count(ep) ? " (done)" : "") + "  -  click to " + (v->manga ? "read" : "play"), 22, H - 36, 15, th.accent);
            }
        }
        return;
    }
    std::string keys_ = std::string("A/D walk   W/S climb   Enter open   C next-up   ") +
                        (kind == "manga" ? "/ librarian   F going to read   X remove   Z undo   J titles   P pages   M anime shelf" : "F star   M manga shelf") +
                        "   drag books between shelves   K barrels   Esc leave";
    if (keys_.size() * 7.9 > W - 40) {  // too wide for the window: two centred lines
        auto parts = su::split(keys_, "   ");
        size_t half = (parts.size() + 1) / 2;
        app.txt(su::join(std::vector<std::string>(parts.begin(), parts.begin() + (long)half), "   "), W / 2, 92, 13, th.dim, true);
        app.txt(su::join(std::vector<std::string>(parts.begin() + (long)half, parts.end()), "   "), W / 2, 108, 13, th.dim, true);
    } else {
        app.txt(keys_, W / 2, 92, 13, th.dim, true);
    }
    if (near) {
        const Book& b = *books[*near];
        std::string pct = b.total ? std::to_string(100 * b.watched / b.total) + "%" : "?";
        std::string star = b.starred ? (kind == "manga" ? "  [going to read]" : "  [starred]") : "";
        std::string tot = b.total ? std::to_string(b.total) : "?";
        if (kind == "manga") {
            std::string src = !b.source.empty() && b.source != "MangaDex" ? "  via " + b.source : "";
            std::string loading = b.chapters ? "" : " (counting...)";
            app.txt(cut(b.title, 46) + "   -   " + std::to_string(b.watched) + "/" + tot + " chapters (" + pct + ")" + loading + src + star, 40, H - 70, 15, th.accent);
        } else {
            app.txt(cut(b.title, 46) + "   -   " + std::to_string(b.watched) + "/" + tot + " episodes (" + pct + ")" + star, 22, H - 70, 15, th.accent);
        }
    } else if (next_up && section_here() == "watching") {
        app.txt("Next up: " + cut(books[*next_up]->title, 48) + "   (press C to walk there)", 22, H - 70, 15, th.good);
    } else if (books.empty()) {
        app.txt("Nothing here yet - watch something, or star anime (F in the world view) to plan them.", 22, H - 70, 15, th.accent);
    }
    if (kind == "manga") {
        double ry = H - 116;
        app.button("ASK THE LIBRARIAN (/)", W - 232, ry, 214, 34, [this] { app.lib_typing = true; }, app.lib_typing);
        app.button(std::string("TITLES: ") + (app.manga_jp ? "JP" : "EN"), W - 346, ry, 104, 34, [this] { app.toggle_titles(); });
        app.button(std::string("PAGES: ") + (app.manga_rtl ? "R>L" : "L>R"), W - 462, ry, 108, 34, [this] { app.toggle_manga_direction(); });
        if (!mangafire.empty()) app.button("NOT ON MANGADEX - TRY MANGAFIRE", W - 700, ry, 230, 34, [this] { app.open_mangafire(mangafire); });
        if (near) app.button("REMOVE FROM SHELF (X)", 40, H - 116, 206, 34, [this] {
                if (near) remove_book(*near);
            });
        removed_drawer(W, H);
    }
    app.button("BARREL CLIMB", W - 190, H - 74, 170, 34, [this] { app.start_game("kong", "library"); });
    app.button(kind == "manga" ? "ANIME SHELF" : "MANGA SHELF (M)", W - 390, H - 74, 190, 34, [this] { app.open_library(kind == "manga" ? "anime" : "manga"); });
    if (near && kind != "manga") {
        BookP b = books[*near];
        app.button(b->starred ? "UNSTAR" : "STAR IT", W - 500, H - 74, 100, 34, [this, b] { toggle_star(b); });
    } else if (near) {
        BookP b = books[*near];
        app.button(b->starred ? "NOT GOING TO READ" : "GOING TO READ (F)", 256, H - 116, 220, 34, [this, b] { toggle_plan(b); });
    }
}
