// The open book, built to behave like a real one (port of book.py). It leans back on a lectern; pages curve into the
// gutter; the stacks are curved layers; the leaf being turned is a chain of strips whose free edge trails and settles, and
// the page art is mapped across those strips. Manga mirror the binding (right to left) and show image pages; long-strip
// chapters (manhwa / webtoons) become a hanging scroll.
#pragma once
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "bookdata.hpp"

class App;
class TextCache;
struct Theme;

namespace bookc {
constexpr double S = 2.4;       // scale of the held book relative to the one on the shelf
constexpr double TOTAL = 2.9;   // seconds until the opening animation (cover + riffle) is done
constexpr double TAU_ = -0.34;  // the book leans back this far (radians)
double book_thickness(const Book& b);
double lerp(double a, double b, double f);
double tilt_for(double u);
}  // namespace bookc

// the page art drawn on the worker, keyed like the Python tuples: ("poster"), ("info"), ("eps", i), ("img", i), ...
using PageKey = std::tuple<std::string, int, std::string>;

struct ArtJob;  // a page being drawn on the art worker

class BookView {
public:
    BookView(App& app, BookP book, const std::string& kind = "anime", bool rtl = false);
    App& app;
    BookP b;
    std::string kind;
    bool manga, rtl;
    int rest;
    double LP, HL, WP, HP, WC, HC;
    int PW, PH;
    double kx, ky;
    int spread = 0, target = 0;
    struct Flip {
        int dir;
        double t0, dur;
    };
    std::optional<Flip> flip;
    std::string mode = "toc";  // manga: "toc" (chapter list) or "read"
    std::string chapter;       // "" = none
    std::vector<std::string> pages_urls;
    std::map<std::string, std::shared_ptr<const Image>> imgs;
    std::set<std::string> requested;
    struct Geom {
        double xh, cy, zl, zr;
        PageKey lkey, rkey;
    };
    std::optional<Geom> geom;
    struct Hover {
        std::string ep, side;
        double rx, ry, rw, rh;
    };
    std::optional<Hover> hover;
    std::map<PageKey, std::pair<std::string, TexP>> art;
    std::map<PageKey, std::pair<std::string, std::shared_ptr<ArtJob>>> pending;
    TexP cover_tex;
    std::shared_ptr<ArtJob> cover_job;
    M4 TM = M4::identity();
    std::optional<bool> strip;  // long-strip scroll: unknown until the first pages are measured
    double sy = 0, sy_goal = 0;
    std::optional<double> unroll_t0, t_last;
    std::map<std::string, double> aspect;  // url -> height / width
    std::map<std::string, std::vector<std::pair<TexP, double>>> tiles;
    std::map<std::string, std::shared_ptr<ArtJob>> tile_pending;

    // content
    std::vector<std::string> eps() const { return b->all_eps; }
    int n_items() const;
    int pages() const;
    int n_spreads() const;
    std::set<std::string> watched() const;
    std::string next_ep() const;
    std::string first_side() const { return rtl ? "R" : "L"; }
    PageKey page_key(const std::string& side, int s) const;
    std::string side_of(const PageKey& key) const;
    // manga reading state
    void set_chapter(const std::string& ch, const std::vector<std::string>& urls);
    void set_direction(bool rtl);
    void back_to_toc();
    int chapter_index() const;
    // long-strip scroll
    bool scrolling() const { return manga && mode == "read" && strip.value_or(false); }
    void set_strip(bool on, bool remember = true);
    void detect_strip();
    std::vector<double> strip_heights() const;
    std::pair<int, std::vector<double>> strip_page() const;
    double strip_max() const;
    void scroll_by(double d);
    void scroll_to_page(int i);
    // flipping
    int left_delta() const { return rtl ? 1 : -1; }
    bool key(const std::string& k);
    void go(int delta);
    void jump(bool first);
    void update(double t);
    double spread_float(double t) const;
    std::optional<Hover> hit(const V3& origin, const V3& dir) const;
    std::vector<Box> build(Renderer& r, TextCache& text, const Theme& th, double t, double u, V3 pos);

private:
    std::map<std::pair<long long, int>, std::pair<std::vector<double>, double>> prof_;
    TexP placeholder_;
    TexP placeholder();
    std::string sig(const PageKey& key) const;
    TexP texture(const PageKey& key);
    void poll(int budget = 2);
    void warm(int s0);
    TexP cover_art();
    std::vector<std::pair<TexP, double>>* strip_tiles(const std::string& url);
    void poll_tiles(const std::set<std::string>& keep, int budget = 3);
    std::vector<Box> build_scroll(Renderer& r, const Theme& th, double t, V3 pos);
    std::pair<std::vector<double>, double> profile(double R, int n);
    std::tuple<std::vector<double>, double, double> prof_for_rise(double rise, int n = 12);
};

std::optional<bool> strip_pref(Book& b);
void set_strip_pref(Book& b, bool on);
