#include "book.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

#include "../app/app.hpp"
#include "../gfx/canvas.hpp"
#include "../gfx/text.hpp"
#include "../util/strutil.hpp"
#include "../world/themes.hpp"
#include "library.hpp"

namespace bookc {
double book_thickness(const Book& b) { return std::max(2.6, std::min(b.thick * S, 3.6)); }
double lerp(double a, double b, double f) { return a + (b - a) * f; }
double tilt_for(double u) { return TAU_ * (u < 0.9 ? ease(u / 0.9) : 1.0); }
}  // namespace bookc

using namespace bookc;

namespace {
const int NR = 5;          // leaves riffled during the opening
const double T = 0.2;      // cover board thickness
const int STRIPS = 12;     // strips per turning leaf / resting page
const C3 CREAM{0.95, 0.91, 0.79}, CREAM2{0.89, 0.84, 0.71}, RED{0.62, 0.08, 0.08}, GOLD{0.87, 0.70, 0.27};
const RGBA INK{45, 30, 20, 255}, INK_L{170, 150, 125, 255}, INK_RED{150, 28, 22, 255};
const int PER = 24, ROWS = 12;                         // chapters/episodes per page
const double GX = 56, GY = 150, ROWH = 70, COLW = 272;  // grid layout inside a 640-wide page texture
const double STRIP_ASPECT = 1.8;                       // pages this much taller than wide are a long strip (manhwa / webtoon)
const double SW = 5.4, SVH = 8.6;                      // scroll: paper width and the height unrolled between the rollers
const int TILE_W = 720, TILE_H = 2048;                 // strip pages are cut into textures of at most this size
const C3 WOOD{0.45, 0.27, 0.13}, WOOD_D{0.30, 0.17, 0.08};

bool isnum(const std::string& e) {
    double f;
    return su::to_double(e, f);
}
double tonum(const std::string& e) {
    double f = 0;
    su::to_double(e, f);
    return f;
}
}  // namespace

// ---------------------------------------------------------------- the art worker (page art is drawn off the render thread)
struct ArtJob {
    std::atomic<bool> done{false};
    bool failed = false;
    Image img;
    std::vector<std::pair<Image, double>> tiles;
};

namespace {
class ArtWorker {
public:
    static ArtWorker& get() {
        static ArtWorker w;
        return w;
    }
    void submit(std::function<void()> fn) {
        {
            std::lock_guard<std::mutex> lk(mu_);
            q_.push_back(std::move(fn));
        }
        cv_.notify_one();
    }

private:
    ArtWorker() {
        std::thread([this] {
            while (true) {
                std::function<void()> fn;
                {
                    std::unique_lock<std::mutex> lk(mu_);
                    cv_.wait(lk, [&] { return !q_.empty(); });
                    fn = std::move(q_.front());
                    q_.pop_front();
                }
                try {
                    fn();
                } catch (...) {
                }
            }
        }).detach();
    }
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> q_;
};

// everything a page needs, copied on the render thread so the worker never touches live state
struct ArtCtx {
    std::string title, last, chapter, next;
    std::vector<std::string> eps;
    std::set<std::string> watched;
    int total = 0;
    bool starred = false, manga = false, rtl = false;
    int PW = 640, PH = 1115;
    double kx = 1, ky = 1;
    size_t n_pages = 0;
    std::shared_ptr<const Image> poster, page;
};

double F(const ArtCtx& c, double px) { return std::round(px * c.kx); }

Image gutter(Image img, const std::string& side) {  // a soft shadow where the page curves into the spine
    int w = img.w, h = img.h, gw = (int)(w * 0.2);
    Image shade;
    shade.w = w;
    shade.h = h;
    shade.rgba.assign((size_t)w * h * 4, 0);
    for (int x = 0; x < gw; x++) {
        double v = std::pow(gw > 1 ? 1.0 - (double)x / (gw - 1) : 1.0, 1.6) * 120;
        int col = side == "R" ? x : w - 1 - x;
        for (int y = 0; y < h; y++) {
            uint8_t* p = &shade.rgba[((size_t)y * w + col) * 4];
            p[0] = 50;
            p[1] = 32;
            p[2] = 14;
            p[3] = (uint8_t)v;
        }
    }
    Canvas c(w, h);
    c.img = std::move(img);
    c.composite(shade);
    return std::move(c.img);
}

Image art_blank(const ArtCtx& c) {
    Canvas d(c.PW, c.PH);
    d.line({{c.PW / 2 - 50.0, c.PH / 2.0}, {c.PW / 2 + 50.0, c.PH / 2.0}}, INK_L, 3);
    return d.img;
}

Image art_poster(const ArtCtx& c) {
    Canvas d(c.PW, c.PH);
    double W = c.PW, H = c.PH, kx = c.kx, ky = c.ky;
    RGBA gold{176, 138, 52, 255};
    d.rounded_rect(26 * kx, 30 * ky, W - 26 * kx, H - 30 * ky, 18 * kx, nullptr, &gold, (int)(5 * kx));
    d.rounded_rect(38 * kx, 42 * ky, W - 38 * kx, H - 42 * ky, 12 * kx, nullptr, &gold, (int)(2 * kx));
    if (c.poster) {
        Image p = img::thumbnail(*c.poster, (int)(W - 130 * kx), (int)(H - 190 * ky));
        d.paste(p, ((int)W - p.w) / 2, (int)(70 * ky) + ((int)(H - 190 * ky) - p.h) / 2, true);
    } else {
        d.text(W / 2, H / 2, "fetching cover...", F(c, 34), INK_L, "mm");
    }
    // caption under the poster: shrink to fit inside the frame, then wrap onto two lines if it is still too wide
    double room = W - 110 * kx;
    bool done = false;
    for (int size : {30, 26, 22})
        if (Canvas::textlength(c.title, F(c, size)) <= room) {
            d.text(W / 2, H - 82 * ky, c.title, F(c, size), INK, "mm");
            done = true;
            break;
        }
    if (!done) {
        auto lines = d.wrap(c.title, F(c, 24), room, 2);
        for (size_t i = 0; i < lines.size(); i++) d.text(W / 2, H - (100 - 28 * (double)i) * ky, lines[i], F(c, 24), INK, "mm");
    }
    return d.img;
}

Image art_info(const ArtCtx& c) {
    Canvas d(c.PW, c.PH);
    double W = c.PW, kx = c.kx, ky = c.ky;
    double gx = GX * kx;
    double y = 90 * ky;
    for (auto& ln : d.wrap(c.title, F(c, 58), W - 2 * gx, 4)) {
        d.text(gx, y, ln, F(c, 58), INK);
        y += 74 * ky;
    }
    d.line({{gx, y + 6 * ky}, {W - gx, y + 6 * ky}}, INK_L, 3);
    std::string unit = c.manga ? "chapters" : "episodes";
    int total = !c.eps.empty() ? (int)c.eps.size() : c.total;
    int w = (int)c.watched.size();
    y += 50 * ky;
    d.text(gx, y, std::string(c.manga ? "READ" : "WATCHED") + " " + std::to_string(w) + " / " + (total ? std::to_string(total) : "?"), F(c, 44), INK);
    y += 78 * ky;
    RGBA track{70, 55, 40, 255}, green{40, 150, 75, 255};
    d.rounded_rect(gx, y, W - gx, y + 34 * ky, 10, &track);
    double frac = total ? std::min(1.0, (double)w / total) : 0;
    if (frac > 0) d.rounded_rect(gx, y, gx + std::max(20.0, (W - 2 * gx) * frac), y + 34 * ky, 10, &green);
    y += 74 * ky;
    if (!c.last.empty()) d.text(gx, y, std::string("LAST ") + (c.manga ? "READ" : "WATCHED") + "  " + c.last.substr(0, 10), F(c, 32), INK);
    if (c.starred) d.text(gx, y + 54 * ky, "* ON YOUR WATCH LIST", F(c, 30), {176, 120, 20, 255});
    d.text(gx, 880 * ky, std::string("NEXT: ") + (c.manga ? "CHAPTER" : "EPISODE") + " " + c.next, F(c, 50), INK_RED);
    d.text(gx, 950 * ky, c.manga ? "(click to read it)" : "(click to play it)", F(c, 28), INK_L);
    std::string arrow = c.rtl ? "<<" : ">>";
    d.text(W / 2, c.PH - 70 * ky, "turn the page for all " + unit + "  " + arrow, F(c, 30), INK_L, "mm");
    return d.img;
}

Image art_eps(const ArtCtx& c, int idx) {
    Canvas d(c.PW, c.PH);
    double kx = c.kx, ky = c.ky;
    std::vector<std::string> chunk;
    for (int i = idx * PER; i < std::min((int)c.eps.size(), (idx + 1) * PER); i++) chunk.push_back(c.eps[i]);
    if (chunk.empty()) return d.img;
    std::string unit = c.manga ? "CHAPTERS" : "EPISODES";
    d.text(GX * kx, 56 * ky, unit + " " + chunk.front() + " - " + chunk.back(), F(c, 44), INK);
    d.line({{GX * kx, 122 * ky}, {c.PW - GX * kx, 122 * ky}}, INK_L, 3);
    RGBA green{40, 150, 75, 255}, white{255, 255, 255, 255}, rule{220, 205, 180, 255};
    for (size_t i = 0; i < chunk.size(); i++) {
        int row = (int)i / 2, col = (int)i % 2;
        double x0 = (GX + col * COLW) * kx, y0 = (GY + row * ROWH) * ky;
        const std::string& ep = chunk[i];
        bool seen = c.watched.count(ep) > 0, up = ep == c.next;
        d.text(x0 + 10 * kx, y0 + 12 * ky, std::string(c.manga ? "CH" : "EP") + " " + ep, F(c, 38), up ? INK_RED : INK);
        double cx = x0 + 226 * kx, cy = y0 + 34 * ky, r_ = 16 * kx;
        if (seen) {
            d.ellipse(cx - r_, cy - r_, cx + r_, cy + r_, &green);
            d.line({{cx - 8 * kx, cy}, {cx - 2 * kx, cy + 7 * ky}, {cx + 9 * kx, cy - 8 * ky}}, white, (int)(5 * kx));
        } else if (up) {
            d.polygon({{cx - 11 * kx, cy - r_}, {cx - 11 * kx, cy + r_}, {cx + 17 * kx, cy}}, INK_RED);
        } else {
            d.ellipse(cx - r_, cy - r_, cx + r_, cy + r_, nullptr, &INK_L, (int)(3 * kx));
        }
        d.line({{x0, y0 + (ROWH - 3) * ky}, {x0 + (COLW - 12) * kx, y0 + (ROWH - 3) * ky}}, rule, 1);
    }
    d.text(c.PW / 2.0, c.PH - 50 * ky, "- " + std::to_string(idx + 1) + " -", F(c, 30), INK_L, "mm");
    return d.img;
}

Image art_chapcard(const ArtCtx& c) {
    Canvas d(c.PW, c.PH);
    double W = c.PW, kx = c.kx, ky = c.ky;
    d.text(W / 2, 300 * ky, "CHAPTER", F(c, 46), INK_L, "mm");
    d.text(W / 2, 400 * ky, c.chapter, F(c, 130), INK_RED, "mm");
    d.line({{120 * kx, 500 * ky}, {W - 120 * kx, 500 * ky}}, INK_L, 3);
    d.text(W / 2, 570 * ky, std::to_string(c.n_pages) + " pages", F(c, 40), INK, "mm");
    std::string turn = c.rtl ? "read right to left  <<<" : "read left to right  >>>";
    const std::vector<std::string> lines = {turn, "arrows / A D turn the page", "[ ] previous / next chapter", "O chapter list", "G flip reading direction", "wheel zoom  -  Z fit page"};
    for (size_t i = 0; i < lines.size(); i++) d.text(W / 2, (700 + i * 48) * ky, lines[i], F(c, 28), INK_L, "mm");
    return d.img;
}

Image art_img(const ArtCtx& c, int idx) {
    Canvas d(c.PW, c.PH);
    if (!c.page) {
        d.text(c.PW / 2.0, c.PH / 2.0, "page " + std::to_string(idx + 1) + "\nloading...", F(c, 40), INK_L, "mm");
        return d.img;
    }
    Image p = img::thumbnail(*c.page, c.PW, c.PH);
    d.paste(p, (c.PW - p.w) / 2, (c.PH - p.h) / 2, false);
    return d.img;
}

Image render_cover(const std::string& title, bool manga) {
    Canvas d(512, 896);
    RGBA gold{222, 178, 70, 255};
    d.rounded_rect(22, 22, 490, 874, 14, nullptr, &gold, 6);
    d.rounded_rect(38, 38, 474, 858, 10, nullptr, &gold, 2);
    auto lines = d.wrap(title, 54, 380, 4);
    double y = 330 - 36.0 * lines.size();
    for (auto& ln : lines) {
        d.text(256, y, ln, 54, gold, "mm");
        y += 72;
    }
    d.line({{110, 640}, {402, 640}}, gold, 3);
    d.text(256, 690, manga ? "MANGA" : "WATCH LOG", 34, gold, "mm");
    return d.img;
}
}  // namespace

// ---------------------------------------------------------------- BookView
BookView::BookView(App& app_, BookP book, const std::string& kind_, bool rtl_) : app(app_), b(std::move(book)), kind(kind_) {
    manga = kind == "manga";
    rtl = manga && rtl_;  // right-to-left binding: spine on the right, pages turn left to right
    rest = rtl ? -1 : 1;  // side the unread pages rest on (+1 right, -1 left)
    if (manga) {
        LP = 5.5;
        HL = 7.9;
    } else {
        LP = 4.65;
        HL = 8.1;
    }
    WP = LP + 0.15;
    HP = HL + 0.3;
    WC = WP + 0.1;
    HC = HP + 0.15;  // a slim cover overhang, like a real hardback
    PW = manga ? 880 : 640;
    PH = (int)std::lround(PW * HL / LP);
    kx = PW / 640.0;  // page layouts are designed on a 640x1115 sheet
    ky = PH / 1115.0;
}

int BookView::n_items() const { return mode == "read" ? (int)pages_urls.size() : (int)eps().size(); }
int BookView::pages() const { return mode == "toc" ? (n_items() + PER - 1) / PER : (int)pages_urls.size(); }
int BookView::n_spreads() const { return 1 + (pages() + 1) / 2; }
std::set<std::string> BookView::watched() const { return std::set<std::string>(b->episodes.begin(), b->episodes.end()); }

std::string BookView::next_ep() const {
    auto es = eps();
    auto w = watched();
    std::vector<double> nums;
    for (auto& e : w)
        if (isnum(e)) nums.push_back(tonum(e));
    if (!nums.empty() && !es.empty()) {
        double mxn = *std::max_element(nums.begin(), nums.end());
        for (auto& e : es)
            if (isnum(e) && tonum(e) > mxn) return e;
    }
    if (es.empty()) return nums.empty() ? "1" : std::to_string((long long)*std::max_element(nums.begin(), nums.end()) + 1);
    for (auto& e : es)
        if (!w.count(e)) return e;
    return es.back();
}

PageKey BookView::page_key(const std::string& side, int s) const {
    if (s == 0) {
        if (mode == "read") return side == "L" ? PageKey{"poster", -1, ""} : PageKey{"chapcard", -1, ""};
        return side == "L" ? PageKey{"poster", -1, ""} : PageKey{"info", -1, ""};
    }
    int idx = 2 * (s - 1) + (side == first_side() ? 0 : 1);
    if (mode == "read") return idx < (int)pages_urls.size() ? PageKey{"img", idx, ""} : PageKey{"blank", -1, side};
    return idx < pages() ? PageKey{"eps", idx, ""} : PageKey{"blank", -1, side};
}

std::string BookView::side_of(const PageKey& key) const {
    const std::string& k = std::get<0>(key);
    if (k == "poster") return "L";
    if (k == "info" || k == "chapcard") return "R";
    if (k == "blank") return std::get<2>(key);
    std::string first = first_side(), other = first == "R" ? "L" : "R";
    return std::get<1>(key) % 2 == 0 ? first : other;
}

void BookView::set_chapter(const std::string& ch, const std::vector<std::string>& urls) {
    mode = "read";
    chapter = ch;
    pages_urls = urls;
    spread = target = 0;
    flip.reset();
    strip = strip_pref(*b);  // a series already known to be a long strip opens straight into the scroll
    sy = sy_goal = 0.0;
    unroll_t0.reset();
    tiles.clear();
    tile_pending.clear();
    std::set<std::string> keep(urls.begin(), urls.end());
    for (auto it = aspect.begin(); it != aspect.end();) it = keep.count(it->first) ? std::next(it) : aspect.erase(it);
    for (auto it = art.begin(); it != art.end();) it = std::get<0>(it->first) == "poster" ? std::next(it) : art.erase(it);
    pending.clear();
    for (auto it = imgs.begin(); it != imgs.end();) it = keep.count(it->first) ? std::next(it) : imgs.erase(it);
}

void BookView::set_direction(bool r) {
    rtl = manga && r;
    rest = rtl ? -1 : 1;
    spread = target = 0;
    flip.reset();
    for (auto it = art.begin(); it != art.end();) it = std::get<0>(it->first) == "poster" ? std::next(it) : art.erase(it);
    pending.clear();
    cover_tex.reset();
    cover_job.reset();
}

void BookView::back_to_toc() {
    mode = "toc";
    chapter.clear();
    pages_urls.clear();
    tiles.clear();
    tile_pending.clear();
    spread = target = 0;
    flip.reset();
    pending.clear();
    for (auto it = art.begin(); it != art.end();) it = std::get<0>(it->first) == "poster" ? std::next(it) : art.erase(it);
}

int BookView::chapter_index() const {
    auto es = eps();
    auto it = std::find(es.begin(), es.end(), chapter);
    return it == es.end() ? -1 : (int)(it - es.begin());
}

void BookView::set_strip(bool on, bool remember) {
    strip = on;
    sy = sy_goal = 0.0;
    unroll_t0.reset();
    spread = target = 0;
    flip.reset();
    if (remember) set_strip_pref(*b, on);
}

// decide book or scroll from the shape of the first pages, once enough of them have loaded
void BookView::detect_strip() {
    std::vector<std::string> first(pages_urls.begin(), pages_urls.begin() + std::min<size_t>(4, pages_urls.size()));
    std::vector<double> known;
    for (auto& u : first)
        if (aspect.count(u)) known.push_back(aspect[u]);
    if (first.empty() || known.size() < std::min<size_t>(2, first.size())) return;
    std::sort(known.begin(), known.end());
    set_strip(known[known.size() / 2] >= STRIP_ASPECT);
    if (*strip) app.status = "Chapter " + chapter + " is a long strip (manhwa / webtoon) - it opens as a scroll: wheel, W/S or arrows to scroll down, V for the book";
}

std::vector<double> BookView::strip_heights() const {
    std::vector<double> known;
    for (auto& [u, a] : aspect) known.push_back(a);
    std::sort(known.begin(), known.end());
    double guess = known.empty() ? 3.0 : known[known.size() / 2];
    std::vector<double> out;
    for (auto& u : pages_urls) {
        auto it = aspect.find(u);
        out.push_back((it == aspect.end() ? guess : it->second) * SW);
    }
    return out;
}

std::pair<int, std::vector<double>> BookView::strip_page() const {
    auto hs = strip_heights();
    double mid = sy + SVH / 2, y = 0.0;
    for (size_t i = 0; i < hs.size(); i++) {
        if (y + hs[i] > mid) return {(int)i, hs};
        y += hs[i];
    }
    return {std::max(0, (int)hs.size() - 1), hs};
}

double BookView::strip_max() const {
    double s = 0;
    for (double h : strip_heights()) s += h;
    return std::max(0.0, s - SVH);
}
void BookView::scroll_by(double d) { sy_goal = std::min(std::max(sy_goal + d, 0.0), strip_max()); }
void BookView::scroll_to_page(int i) {
    auto hs = strip_heights();
    double s = 0;
    for (int k = 0; k < std::max(0, i) && k < (int)hs.size(); k++) s += hs[k];
    sy_goal = std::min(std::max(s, 0.0), strip_max());
}

std::vector<std::pair<TexP, double>>* BookView::strip_tiles(const std::string& url) {
    auto hit = tiles.find(url);
    if (hit != tiles.end()) return &hit->second;
    auto im = imgs.find(url);
    if (im != imgs.end() && !tile_pending.count(url)) {
        auto job = std::make_shared<ArtJob>();
        tile_pending[url] = job;
        std::shared_ptr<const Image> src = im->second;
        ArtWorker::get().submit([job, src] {  // a strip page scaled to the scroll's width and cut into textures the GPU can hold
            int w = std::min(src->w, TILE_W);
            Image scaled = w == src->w ? *src : img::resize(*src, w, std::max(1, (int)std::lround(src->h * (double)w / src->w)));
            for (int y0 = 0; y0 < scaled.h; y0 += TILE_H) {
                int hh = std::min(scaled.h, y0 + TILE_H) - y0;
                Image part;
                part.w = w;
                part.h = hh;
                part.rgba.assign(scaled.rgba.begin() + (long)((size_t)y0 * w * 4), scaled.rgba.begin() + (long)((size_t)(y0 + hh) * w * 4));
                job->tiles.emplace_back(std::move(part), (double)hh / w * SW);
            }
            job->done = true;
        });
    }
    return nullptr;
}

void BookView::poll_tiles(const std::set<std::string>& keep, int budget) {
    for (auto it = tile_pending.begin(); it != tile_pending.end() && budget > 0;) {
        if (!it->second->done) {
            ++it;
            continue;
        }
        std::vector<std::pair<TexP, double>> t;
        for (auto& [im, h] : it->second->tiles) t.emplace_back(app.r.make_texture(im.rgba.data(), im.w, im.h, true), h);
        tiles[it->first] = t;
        it = tile_pending.erase(it);
        budget--;
    }
    for (auto it = tiles.begin(); it != tiles.end();) it = keep.count(it->first) ? std::next(it) : tiles.erase(it);  // only pages near the window stay on the GPU
}

// the hanging scroll: paper unrolled between two wooden rollers, the strip pages laid end to end on it
std::vector<Box> BookView::build_scroll(Renderer& r, const Theme& th, double t, V3 pos) {
    double dx = pos.x, cy = pos.y, cz = pos.z;
    if (!unroll_t0) unroll_t0 = t;
    double a = ease((t - *unroll_t0) / 0.9);
    double vh = std::max(SVH * a, 0.02);
    auto [cur, hs] = strip_page();
    double total = 0;
    for (double h : hs) total += h;
    total = std::max(total, 1e-6);
    std::set<std::string> keep;
    for (int i = std::max(0, cur - 3); i < std::min((int)pages_urls.size(), cur + 5); i++) keep.insert(pages_urls[i]);
    poll_tiles(keep);
    std::vector<Box> out;
    double top = cy + vh / 2;
    out.push_back(box(dx, cy, cz - 0.03, SW + 0.12, vh, 0.04, CREAM, 0.95, 0.0, 0.0, 0.0, 1.0));  // the paper behind the art
    double y = 0.0;
    for (size_t i = 0; i < pages_urls.size(); i++) {
        double h = hs[i];
        if (y > sy + vh) break;
        if (y + h >= sy && keep.count(pages_urls[i])) {
            auto* tl = strip_tiles(pages_urls[i]);
            if (tl && !tl->empty()) {
                double ty = y;
                for (auto& [tex, th_] : *tl) {
                    double a0 = std::max(ty, sy), a1 = std::min(ty + th_, sy + vh);
                    if (a1 > a0) {
                        double v0 = (a0 - ty) / th_, v1 = (a1 - ty) / th_;
                        r.quad3d(tex, mx::translate(dx - SW / 2, top - (a0 - sy), cz + 0.001) * mx::scale(SW, -(a1 - a0), 1), C4(1, 1, 1, 1), UV{0, (float)v0, 1, (float)v1});
                    }
                    ty += th_;
                }
            }
        }
        y += h;
    }
    // rollers: read paper winds onto the top one, unread paper waits on the bottom one, and they turn as you scroll
    double done = std::min(std::max((sy + vh) / total, 0.0), 1.0);
    for (auto [side, frac] : {std::pair<int, double>{1, done}, {-1, 1 - done}}) {
        double rad = 0.2 + 0.28 * frac;
        double ry = side > 0 ? top + rad : cy - vh / 2 - rad;
        double spin = -side * sy / std::max(rad, 0.1);
        for (int k = 0; k < 3; k++) out.push_back(box(dx, ry, cz, SW + 0.06, 2 * rad, 2 * rad, k % 2 == 0 ? CREAM : CREAM2, 0.9, 0.0, 0.0, 0.0, 1.0, 0.0, spin + k * PI / 6));
        for (int k = 0; k < 2; k++) out.push_back(box(dx, ry, cz, SW + 1.1, 0.3, 0.3, WOOD, 0.4, 0.0, 0.2, 0.0, 1.0, 0.0, spin + k * PI / 4));
        for (int ex : {-1, 1}) {  // turned knobs on the ends
            double kxx = dx + ex * (SW / 2 + 0.62);
            out.push_back(box(kxx, ry, cz, 0.22, 0.52, 0.52, WOOD_D, 0.4, 0.0, 0.2, 0.0, 1.0, 0.0, PI / 4));
            out.push_back(box(kxx, ry, cz, 0.22, 0.52, 0.52, GOLD, 0.8, 0.0, 0.2, 0.0, 1.0));
        }
    }
    // a silk cord from the ends of the top roller up to a hook, so it reads as hanging
    double ry = top + 0.2 + 0.28 * done;
    double hook = ry + 2.2;
    for (int ex : {-1, 1}) {
        double x0 = dx + ex * (SW / 2 + 0.45);
        double mxx = (x0 + dx) / 2, my = (ry + hook) / 2;
        double ln = std::hypot(x0 - dx, hook - ry);
        out.push_back(box(mxx, my, cz, 0.06, ln, 0.06, RED, 0.6, 0.0, 0.0, 0.0, 1.0, std::atan2(dx - x0, hook - ry) * -1));
    }
    out.push_back(box(dx, hook, cz, 0.3, 0.3, 0.3, GOLD, 1.0, 0.0, 0.2, 0.0, 1.0, 0.0, PI / 4));
    geom.reset();
    hover.reset();
    return out;
}

std::string BookView::sig(const PageKey& key) const {
    std::string base = std::to_string(watched().size()) + "|" + (b->poster_img ? "1" : "0") + "|" + std::to_string(eps().size()) + "|" + (b->starred ? "1" : "0") + "|" + chapter;
    if (std::get<0>(key) == "img") {
        int i = std::get<1>(key);
        base += i < (int)pages_urls.size() && imgs.count(pages_urls[i]) ? "|1" : "|0";
    }
    return base;
}

TexP BookView::placeholder() {
    if (!placeholder_) {
        uint8_t px[16] = {0};
        placeholder_ = app.r.make_texture(px, 2, 2);
    }
    return placeholder_;
}

// the page texture for `key`; never blocks: if it is not ready it is queued and the previous version (or a blank) shows
TexP BookView::texture(const PageKey& key) {
    std::string s = sig(key);
    auto hit = art.find(key);
    if (hit != art.end() && hit->second.first == s) return hit->second.second;
    auto pend = pending.find(key);
    if (pend == pending.end() || pend->second.first != s) {
        ArtCtx c;
        c.title = b->title;
        c.last = b->last;
        c.chapter = chapter;
        c.eps = eps();
        c.watched = watched();
        c.total = b->total;
        c.starred = b->starred;
        c.manga = manga;
        c.rtl = rtl;
        c.PW = PW;
        c.PH = PH;
        c.kx = kx;
        c.ky = ky;
        c.n_pages = pages_urls.size();
        c.next = next_ep();
        c.poster = b->poster_img;
        if (std::get<0>(key) == "img" && std::get<1>(key) < (int)pages_urls.size()) {
            auto it = imgs.find(pages_urls[std::get<1>(key)]);
            if (it != imgs.end()) c.page = it->second;
        }
        auto job = std::make_shared<ArtJob>();
        pending[key] = {s, job};
        std::string side = side_of(key);
        PageKey k = key;
        ArtWorker::get().submit([job, c, k, side] {
            const std::string& kind = std::get<0>(k);
            Image im = kind == "poster" ? art_poster(c) : kind == "info" ? art_info(c) : kind == "eps" ? art_eps(c, std::get<1>(k)) : kind == "img" ? art_img(c, std::get<1>(k))
                       : kind == "chapcard" ? art_chapcard(c) : art_blank(c);
            job->img = gutter(std::move(im), side);
            job->done = true;
        });
    }
    return hit != art.end() ? hit->second.second : placeholder();
}

// upload finished page textures (at most `budget` per frame so a flip never hitches)
void BookView::poll(int budget) {
    for (auto it = pending.begin(); it != pending.end() && budget > 0;) {
        auto& [s, job] = it->second;
        if (!job->done) {
            ++it;
            continue;
        }
        if (!job->failed && !job->img.empty()) {
            art[it->first] = {s, app.r.make_texture(job->img.rgba.data(), job->img.w, job->img.h, true)};
            budget--;
        }
        it = pending.erase(it);
    }
    std::vector<PageKey> img_keys;
    for (auto& [k, v] : art)
        if (std::get<0>(k) == "img") img_keys.push_back(k);
    if (img_keys.size() > 14)  // keep memory bounded while reading a long chapter
        for (size_t i = 0; i < img_keys.size() - 14; i++) art.erase(img_keys[i]);
}

void BookView::warm(int s0) {
    for (int s = std::max(0, s0 - 1); s < std::min(n_spreads(), s0 + 3); s++)
        for (const char* side : {"L", "R"}) texture(page_key(side, s));
}

TexP BookView::cover_art() {
    if (!cover_tex) {
        if (!cover_job) {
            cover_job = std::make_shared<ArtJob>();
            auto job = cover_job;
            std::string title = b->title;
            bool m = manga;
            ArtWorker::get().submit([job, title, m] {
                job->img = render_cover(title, m);
                job->done = true;
            });
        } else if (cover_job->done) {
            cover_tex = app.r.make_texture(cover_job->img.rgba.data(), cover_job->img.w, cover_job->img.h, true);
        }
    }
    return cover_tex ? cover_tex : placeholder();
}

bool BookView::key(const std::string& k) {
    if (k == "v" && manga && mode == "read") {
        set_strip(!strip.value_or(false));
        app.status = *strip ? "Reading as a scroll (scroll down to read)" : "Reading as a book (turn the pages)";
        return true;
    }
    if (scrolling() && k != "o" && k != "g" && k != "[" && k != "]") {
        static const std::map<std::string, double> steps = {{"ArrowDown", 1.6}, {"s", 1.6}, {"j", 1.6}, {"ArrowUp", -1.6}, {"w", -1.6}, {"k", -1.6},
                                                            {"ArrowRight", SVH * 0.85}, {"d", SVH * 0.85}, {"PageDown", SVH * 0.85}, {" ", SVH * 0.85},
                                                            {"ArrowLeft", -SVH * 0.85}, {"a", -SVH * 0.85}, {"PageUp", -SVH * 0.85}};
        if (k == "Home") sy_goal = 0.0;
        else if (k == "End") sy_goal = strip_max();
        else if (steps.count(k)) scroll_by(steps.at(k));
        else return false;
        return true;
    }
    int n = n_spreads() - 1;
    int ld = left_delta();
    std::optional<int> delta;
    if (k == "ArrowLeft" || k == "a") delta = ld;
    else if (k == "ArrowRight" || k == "d") delta = -ld;
    else if (k == "ArrowUp" || k == "w" || k == "PageDown") delta = 5;
    else if (k == "ArrowDown" || k == "s" || k == "PageUp") delta = -5;
    if (k == "Home") target = 0;
    else if (k == "End") target = n;
    else if (k == "c" && mode == "toc") {
        auto es = eps();
        std::string nxt = next_ep();
        auto it = std::find(es.begin(), es.end(), nxt);
        target = it != es.end() ? 1 + ((int)(it - es.begin()) / PER) / 2 : 0;
    } else if (k == "o" && manga && mode == "read") back_to_toc();
    else if (k == "g" && manga) app.toggle_manga_direction();
    else if ((k == "[" || k == "]") && manga && mode == "read") {
        int i = chapter_index() + (k == "]" ? 1 : -1);
        auto es = eps();
        if (0 <= i && i < (int)es.size()) app.read_chapter(b, es[i]);
    } else if (delta) target = std::min(std::max(target + *delta, 0), n);
    else return false;
    return true;
}

void BookView::go(int delta) {
    if (scrolling()) return scroll_by(SVH * 0.85 * (-delta * left_delta()));  // the < > buttons move a window's height (left = back)
    target = std::min(std::max(target + delta, 0), n_spreads() - 1);
}

void BookView::jump(bool first) {
    if (scrolling()) {
        sy_goal = first == !rtl ? 0.0 : strip_max();  // the left-hand button goes to the top
        return;
    }
    target = first ? 0 : n_spreads() - 1;
}

void BookView::update(double t) {
    double dt = t_last ? t - *t_last : 0.0;
    t_last = t;
    if (mode == "read") {
        for (auto& u : pages_urls) {
            auto it = imgs.find(u);
            if (it != imgs.end() && !aspect.count(u)) aspect[u] = (double)it->second->h / std::max(1, it->second->w);
        }
        if (!strip) detect_strip();
    }
    if (scrolling()) {
        sy_goal = std::min(sy_goal, strip_max());
        sy += (sy_goal - sy) * (1 - std::exp(-dt * 10));
        int cur = strip_page().first;
        for (int i = std::max(0, cur - 2); i < std::min((int)pages_urls.size(), cur + 5); i++) {  // fetch the pages around the window ahead
            const std::string& url = pages_urls[i];
            if (!imgs.count(url) && !requested.count(url)) {
                requested.insert(url);
                app.fetch_page_image(url, this);
            }
        }
        for (int i = 0; i < (int)pages_urls.size(); i++) {  // long chapters: let go of pages far behind or ahead
            if (std::abs(i - cur) > 10 && imgs.count(pages_urls[i])) {
                imgs.erase(pages_urls[i]);
                requested.erase(pages_urls[i]);
            }
        }
        return;
    }
    if (flip && t - flip->t0 >= flip->dur) {
        spread += flip->dir;
        flip.reset();
    }
    if (!flip && spread != target) {
        int d = target > spread ? 1 : -1;
        flip = Flip{d, t, std::abs(target - spread) == 1 ? 0.95 : 0.32};
    }
    if (mode == "read") {  // fetch the images around the current spread ahead of time
        for (int s = std::max(0, spread - 1); s < spread + 3; s++)
            for (const char* side : {"L", "R"}) {
                PageKey key = page_key(side, s);
                if (std::get<0>(key) == "img") {
                    const std::string& url = pages_urls[std::get<1>(key)];
                    if (!imgs.count(url) && !requested.count(url)) {
                        requested.insert(url);
                        app.fetch_page_image(url, this);
                    }
                }
            }
    }
}

double BookView::spread_float(double t) const {
    if (!flip) return spread;
    return spread + flip->dir * smooth01((t - flip->t0) / flip->dur);
}

// ray -> (episode/chapter, side, rect) for the page cell under the mouse
std::optional<BookView::Hover> BookView::hit(const V3& origin, const V3& direction) const {
    if (!geom || flip || mode == "read") return std::nullopt;
    const Geom& g = *geom;
    M4 inv = TM.inverse();  // the book is tilted: bring the ray into the book's own frame
    auto o4 = inv.mul(origin.x, origin.y, origin.z, 1.0);
    V3 o{o4[0], o4[1], o4[2]};
    auto d4 = inv.mul(direction.x, direction.y, direction.z, 0.0);
    V3 d{d4[0], d4[1], d4[2]};
    d = d / d.norm();
    auto es = eps();
    struct S_ {
        const char* side;
        const PageKey* key;
        double x_left, z;
    };
    const S_ sides[2] = {{"L", &g.lkey, g.xh - 0.03 - LP, g.zl}, {"R", &g.rkey, g.xh + 0.03, g.zr}};
    for (auto& sd : sides) {
        if (std::fabs(d.z) < 1e-6) continue;
        double t = (sd.z + 0.02 - o.z) / d.z;
        if (t < 0) continue;
        double x = o.x + t * d.x, y = o.y + t * d.y;
        if (!(sd.x_left <= x && x <= sd.x_left + LP && g.cy - HL / 2 <= y && y <= g.cy + HL / 2)) continue;
        double px = (x - sd.x_left) / LP * 640, py = (g.cy + HL / 2 - y) / HL * 1115;
        const std::string& kk = std::get<0>(*sd.key);
        if (kk == "eps") {
            int col = (int)std::floor((px - GX) / COLW), row = (int)std::floor((py - GY) / ROWH);
            if (0 <= col && col < 2 && 0 <= row && row < ROWS && px >= GX) {
                int i = std::get<1>(*sd.key) * PER + row * 2 + col;
                if (i < (int)es.size()) return Hover{es[i], sd.side, GX + col * COLW, GY + row * ROWH, COLW - 12, ROWH - 4};
            }
        } else if (kk == "info" && 870 <= py && py <= 990 && GX <= px && px <= 640 - GX) {
            return Hover{next_ep(), sd.side, GX - 8, 872, 640 - 2 * GX + 16, 112};
        }
    }
    return std::nullopt;
}

// resting-page curve: strip angles (negative = rising away from the gutter) and the page's exact rise
std::pair<std::vector<double>, double> BookView::profile(double R, int n) {
    auto key = std::make_pair((long long)std::llround(R * 1000), n);
    auto it = prof_.find(key);
    if (it != prof_.end()) return it->second;
    double l = LP / n;
    std::vector<double> ang;
    double z = 0.0;
    for (int j = 0; j < n; j++) {
        double x = (j + 0.5) * l;
        double slope = R * 3 / LP * std::pow(1 - x / LP, 2);
        ang.push_back(-std::atan(slope));
        z += l * slope / std::sqrt(1 + slope * slope);
    }
    prof_[key] = {ang, z};
    return prof_[key];
}

// curvature that makes a page rise exactly `rise` from the gutter to its outer edge
std::tuple<std::vector<double>, double, double> BookView::prof_for_rise(double rise, int n) {
    double R = rise;
    for (int k = 0; k < 3; k++) R *= rise / std::max(profile(R, n).second, 1e-6);
    auto [a, z] = profile(R, n);
    return {a, z, R};
}

// how far a side's top page climbs from the gutter to its outer edge (proportional to the stack, capped for readability)
static double side_rise(double thick) { return std::min(std::max(0.8 * thick, 0.05), 1.1); }

std::vector<Box> BookView::build(Renderer& r, TextCache& text, const Theme& th, double t, double u, V3 pos) {
    double dx = pos.x, cy = pos.y, cz = pos.z;
    double P = book_thickness(*b);
    double Pp = P - 2 * T;
    std::vector<Box> out;
    double open_amt = smooth01((u - 0.9) / 0.9);
    double th_c = -PI * open_amt;
    double xh = dx - rest * WP / 2 * (1 - open_amt);  // the hinge slides toward centre as the book opens
    C3 cover = b->color * 0.8;
    double z0 = cz - P / 2 + T;
    double tau = TAU_;
    TM = mx::translate(dx, cy, cz) * mx::rot_x(tau) * mx::translate(-dx, -cy, -cz);
    auto Q = [&](const TexP& tex, const M4& M, C4 tint = C4(1, 1, 1, 1), UV uv = UV{}) { r.quad3d(tex, TM * M, tint, uv); };

    bool interactive = u >= TOTAL;
    if (interactive && scrolling()) return build_scroll(r, th, t, pos);
    poll();
    warm(interactive ? spread : 0);
    double land_th;
    if (interactive) {
        double f = spread_float(t) / std::max(1, n_spreads() - 1);
        land_th = Pp * (0.12 + 0.76 * f);
    } else {
        double s = 0;
        for (int k = 0; k < NR; k++) s += smooth01((u - (1.45 + 0.14 * k)) / (0.55 + 0.04 * k));
        land_th = Pp * 0.12 * s / NR;
    }
    double rest_th = Pp - land_th;
    double th_left = rest < 0 ? rest_th : land_th, th_right = rest < 0 ? land_th : rest_th;
    double zl = z0 + th_left, zr = z0 + th_right;

    // lectern: a slim ledge and back board (dark, so they never read as extra frames around the page)
    double grow = ease((u - 0.9) / 0.5);
    C3 dark = th.wood * 0.5;
    out.push_back(box(dx, cy - HC / 2 - 0.55, cz + 0.15, (2 * WP + 0.6) * grow, 0.34, 1.2, dark, 0.15, 0.0, 0.2));
    out.push_back(box(dx, cy, cz - P / 2 - 0.4, (2 * WC + 0.5) * grow, HC + 0.35, 0.3, dark, 0.1, 0.0, 0.2));

    // covers and spine
    out.push_back(box(xh + rest * (WC / 2 - 0.08), cy, cz - P / 2 + T / 2, WC, HC, T, cover, 0.45, 0.0, 0.1));
    double sp_x = xh - rest * 0.15;
    double sp_th = lerp(P + 0.06, T, open_amt);  // once open the spine lies under the gutter so the pages fan out of it
    double sp_z = lerp(cz, cz - P / 2 + sp_th / 2, open_amt);
    out.push_back(box(sp_x, cy, sp_z, 0.34, HC, sp_th, cover, 0.5, 0.0, 0.1));
    if (open_amt < 0.35) {
        out.push_back(box(sp_x - rest * 0.2, cy, sp_z, 0.12, HC - 0.4, std::max(sp_th - 0.5, 0.2), cover * 0.75, 0.4, 0.0, 0.1));
        for (double off : {-0.38, -0.30, 0.30, 0.38}) out.push_back(box(sp_x - rest * 0.01, cy + off * HC, sp_z, 0.38, 0.07, sp_th + 0.04, GOLD, 1.0, 0.0, 0.0));  // gilt bands
    }
    double phi_c = !rtl ? th_c : PI - th_c;
    double lx = WC / 2 - 0.08, lz = rest * (P / 2 - T / 2);
    double fx = xh + lx * std::cos(phi_c) + lz * std::sin(phi_c);
    double fz = cz - lx * std::sin(phi_c) + lz * std::cos(phi_c);
    out.push_back(box(fx, cy, fz, WC, HC, T, cover, 0.45, phi_c, 0.1));
    double zo = (T / 2 + 0.006) * rest;
    UV uv_c = rtl ? UV{1, 0, 0, 1} : UV{0, 0, 1, 1};
    Q(cover_art(), mx::translate(fx, cy, fz) * mx::rot_y(phi_c) * mx::translate(-WC / 2, HC / 2, zo) * mx::scale(WC, -HC, 1), C4(1, 1, 1, 1), uv_c);

    // page blocks: thin curved layers fanning out of the spine
    const double l = LP / STRIPS;
    auto chain = [&](int side, double z_outer, double thick, double glow, C3 shade, const TexP* tex, double tex_a, double rise, int n) {
        auto [ang, rs, R_] = prof_for_rise(rise, n);
        double ln = LP / n;
        double px = xh + side * 0.03, pz = z_outer - rs;
        for (int j = 0; j < n; j++) {
            double phi = side > 0 ? ang[j] : -PI - ang[j];
            double cx_ = px + ln / 2 * std::cos(phi), cz_ = pz - ln / 2 * std::sin(phi);
            out.push_back(box(cx_, cy, cz_ - thick / 2 + 0.02, ln + 0.012, HL, thick, shade, glow, phi, 0.0));
            if (tex) {
                double zoff = side > 0 ? (thick / 2 + 0.004) : -(thick / 2 + 0.004);
                UV uv = side > 0 ? UV{(float)((double)j / n), 0, (float)((double)(j + 1) / n), 1} : UV{(float)(1 - (double)j / n), 0, (float)(1 - (double)(j + 1) / n), 1};
                Q(*tex, mx::translate(cx_, cy, cz_ - thick / 2 + 0.02) * mx::rot_y(phi) * mx::translate(-ln / 2, HL / 2, zoff) * mx::scale(ln, -HL, 1), C4(1, 1, 1, tex_a), uv);
            }
            px += ln * std::cos(phi);
            pz -= ln * std::sin(phi);
        }
    };
    auto stack = [&](int side, double thick) {
        if (thick < 0.03) return;  // no pages on this side yet (the book is still closed)
        int n = (int)std::min(64.0, std::max(3.0, thick / 0.07));
        double st = thick / n;
        double top_rise = side_rise(thick);
        for (int k = 0; k < n; k++) {
            // low layers stay close to the spine, higher ones fan out - none climbs higher than the top page
            double rise = std::max(top_rise * (k + 0.5) / n, 0.02);
            C3 shade = k % 2 == 0 ? CREAM : CREAM * 0.965;
            chain(side, z0 + (k + 1) * st - 0.06, st * 1.05, 0.95, shade, nullptr, 1.0, rise, 8);
        }
    };
    stack(-1, th_left);
    stack(1, th_right);
    auto prof_l = prof_for_rise(side_rise(th_left));
    auto prof_r = prof_for_rise(side_rise(th_right));
    double gzl = zl - std::get<1>(prof_l), gzr = zr - std::get<1>(prof_r);  // height of each side's top page at the gutter

    using Side = std::pair<const std::vector<double>*, double>;
    // the page being turned: strips from the gutter; it rises off one stack's curve, trails and settles on the other's
    auto leaf = [&](Side start, Side end, double p, double theta, int sgn, const TexP* tex_a, const TexP* tex_b, double curl, double settle) {
        const auto& ps = *start.first;
        const auto& pe = *end.first;
        double px = xh, pz = lerp(start.second, end.second, smooth01(p)) + 0.04 + 0.14 * std::sin(PI * p);
        double amp = curl * std::sin(PI * p);
        for (int j = 0; j < STRIPS; j++) {
            double frac = (j + 0.5) / STRIPS;
            double off = sgn * (ps[j] * (1 - p) - pe[j] * p);
            double lag = sgn * (amp * std::pow(frac, 1.6) + settle * frac);
            double phi = theta + off + lag;
            double cx_ = px + l / 2 * std::cos(phi), cz_ = pz - l / 2 * std::sin(phi);
            out.push_back(box(cx_, cy, cz_, l + 0.012, HL, 0.04, CREAM, 0.95, phi, 0.0));
            for (auto [tex, back] : {std::pair<const TexP*, bool>{tex_a, false}, {tex_b, true}}) {
                if (!tex) continue;
                double zoff = back ? -0.023 : 0.023;
                UV uv = back ? UV{(float)(1 - (double)j / STRIPS), 0, (float)(1 - (double)(j + 1) / STRIPS), 1} : UV{(float)((double)j / STRIPS), 0, (float)((double)(j + 1) / STRIPS), 1};
                Q(*tex, mx::translate(cx_, cy, cz_) * mx::rot_y(phi) * mx::translate(-l / 2, HL / 2, zoff) * mx::scale(l, -HL, 1), C4(1, 1, 1, 1), uv);
            }
            px += l * std::cos(phi);
            pz -= l * std::sin(phi);
        }
    };

    Side right_side{&std::get<0>(prof_r), gzr}, left_side{&std::get<0>(prof_l), gzl};
    geom.reset();
    int gdir_fwd = rest > 0 ? 1 : -1;  // direction the leaf travels when moving forward
    double show;
    if (!interactive) {  // opening: a few blank leaves riffle across
        for (int k = 0; k < NR; k++) {
            double p = (u - (1.45 + 0.14 * k)) / (0.55 + 0.04 * k);
            if (0 < p && p < 1) {
                double curl = 0.8 + 0.12 * k;
                double settle = p > 0.82 ? 0.16 * std::exp(-9 * std::max(p - 0.82, 0.0)) * std::sin(40 * std::max(p - 0.82, 0.0)) : 0.0;
                if (gdir_fwd > 0) leaf(right_side, left_side, p, -PI * p, 1, nullptr, nullptr, curl, settle);
                else leaf(left_side, right_side, p, -PI * (1 - p), -1, nullptr, nullptr, curl, settle);
            }
        }
        show = ease((u - (TOTAL - 0.5)) / 0.5);
    } else {
        show = 1.0;
    }
    // top page of each stack (carries the text once the book has opened)
    int s = spread;
    PageKey lk, rk;
    if (interactive) {
        if (flip) {
            double p = smooth01((t - flip->t0) / flip->dur);
            int gdir = rest > 0 ? flip->dir : -flip->dir;
            int before = s, after = s + flip->dir;
            double settle = p > 0.82 ? 0.16 * std::exp(-9 * std::max(p - 0.82, 0.0)) * std::sin(40 * std::max(p - 0.82, 0.0)) : 0.0;
            if (gdir > 0) {
                lk = page_key("L", before);
                rk = page_key("R", after);
                TexP a = texture(page_key("R", before)), bb = texture(page_key("L", after));
                leaf(right_side, left_side, p, -PI * p, 1, &a, &bb, 1.0, settle);
            } else {
                lk = page_key("L", after);
                rk = page_key("R", before);
                TexP a = texture(page_key("R", after)), bb = texture(page_key("L", before));
                leaf(left_side, right_side, p, -PI * (1 - p), -1, &a, &bb, 1.0, settle);
            }
        } else {
            lk = page_key("L", s);
            rk = page_key("R", s);
        }
    } else {
        lk = page_key("L", 0);
        rk = page_key("R", 0);
    }
    TexP tl, tr;
    if (show > 0.02) {
        tl = texture(lk);
        tr = texture(rk);
    }
    if (th_left > 0.03) chain(-1, zl, 0.04, 0.95, CREAM, tl ? &tl : nullptr, show, side_rise(th_left), STRIPS);
    if (th_right > 0.03) chain(1, zr, 0.04, 0.95, CREAM, tr ? &tr : nullptr, show, side_rise(th_right), STRIPS);
    if (interactive) {
        geom = Geom{xh, cy, zl, zr, lk, rk};
        auto ray = app.lib ? app.lib->ray : std::nullopt;
        hover = ray ? hit(ray->first, ray->second) : std::nullopt;
        if (hover && !flip) {
            const Hover& hv = *hover;
            double x_left = hv.side == "L" ? xh - 0.03 - LP : xh + 0.03;
            double z = hv.side == "L" ? zl : zr;
            Q(r.white, mx::translate(x_left + hv.rx / 640 * LP, cy + HL / 2 - hv.ry / 1115 * HL, z + 0.02) * mx::scale(hv.rw / 640 * LP, -hv.rh / 1115 * HL, 1),
              C4(0.9, 0.55, 0.1, 0.28));
        }
    }

    // tilt every box about the book centre (the lectern angle), then add the column in world space
    double c = std::cos(tau), s_ = std::sin(tau);
    for (auto& bx : out) {
        double vy = bx.v[1] - cy, vz = bx.v[2] - cz;
        bx.v[1] = (float)(cy + vy * c - vz * s_);
        bx.v[2] = (float)(cz + vy * s_ + vz * c);
        bx.v[12] = (float)tau;
        bx.v[13] = 1.0f;  // the whole book skips posterizing
    }
    double base = cy - HC / 2 - 0.6;
    out.push_back(box(dx, base / 2 - 0.2, cz - 0.6, 1.4 * grow, std::max(base, 0.5), 0.9, th.wood, 0.25, 0.0, 0.3, 0.0, 1.0));
    return out;
}

// remembered book / scroll choice for a series: true, false, or unknown (measure the pages)
std::optional<bool> strip_pref(Book& b) {
    if (!b.strip_pref) {
        auto prefs = themes::load_settings().value("strip_series", nlohmann::json::object());
        if (prefs.is_object() && prefs.contains(b.id) && prefs[b.id].is_boolean()) b.strip_pref = std::optional<bool>(prefs[b.id].get<bool>());
        else b.strip_pref = std::optional<bool>();
    }
    return *b.strip_pref;
}

void set_strip_pref(Book& b, bool on) {
    b.strip_pref = std::optional<bool>(on);
    auto prefs = themes::load_settings().value("strip_series", nlohmann::json::object());
    if (!prefs.is_object()) prefs = nlohmann::json::object();
    prefs[b.id] = on;
    themes::save_settings({{"strip_series", prefs}});
}
