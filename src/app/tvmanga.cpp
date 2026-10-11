// Manga on the TV: the same dashboard lists (categories left / right, titles up / down), a chapter grid per title, and a
// full-screen reader that fits the page - or a two-page spread - to the TV and is driven entirely by the remote:
//   Left / Right  turn the page (the way the book reads: right-to-left manga, or left-to-right)    Up / Down  scroll a long strip
//   Rewind / FF   previous / next chapter      Select  show the page info      Menu  flip the reading direction      Back  close
#include <algorithm>
#include <cmath>
#include <future>

#include "../core/core.hpp"
#include "../util/strutil.hpp"
#include "app.hpp"

namespace {
std::shared_ptr<const Image> decode_thumb(const std::string& path, int w, int h) { return std::make_shared<const Image>(img::open_thumb(path, w, h)); }
std::string S(const nlohmann::json& j, const char* k) {
    return j.is_object() && j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : "";
}
double chapter_num(const std::string& c) {
    try {
        return std::stod(c);
    } catch (...) {
        return 1e9;
    }
}
}  // namespace

void App::mg_enter() {
    tv_manga = true;
    mg_detail = mg_reading = false;
    typing = false;
    scene.close_field();
    scene.selected = -1;
    tv_sec = tv_idx = 0;
    status = "Loading manga...";
    tasks_.run(
        [] {
            json reading = core::mr_list();
            if (reading.size() > 12) reading.erase(reading.begin() + 12, reading.end());
            json plan = core::mp_list();
            json popular = json::array();
            try {
                popular = core::md_popular(18);
            } catch (...) {
            }
            return json{{"reading", reading}, {"plan", plan}, {"popular", popular}};
        },
        tracked("Loading manga", [this](const json& res, const std::string& err) {
            if (!err.empty()) {
                status = "Manga failed: " + err;
                return;
            }
            mg_fill(res);
        }));
}

void App::mg_search(const std::string& q) {
    std::string query = su::strip(q);
    if (query.empty()) return;
    status = "Searching manga for '" + query + "'...";
    tasks_.run([query] { return core::md_search(query, 30); },
               tracked("Searching manga", [this, query](const json& res, const std::string& err) {
                   if (!err.empty()) {
                       status = "Manga search failed: " + err;
                       return;
                   }
                   mg_fill(json{{"results", res}, {"label", "RESULTS FOR " + su::upper(query)}});
               }));
}

void App::mg_fill(const json& g) {
    mg_gen++;
    mg_items.clear();
    mg_groups.clear();
    mg_listed.clear();
    for (auto& m : core::mp_list()) mg_listed.insert(S(m, "id"));
    std::set<std::string> seen;
    auto add_group = [&](const std::string& label, const json& rows, bool reading) {
        std::vector<int> idxs;
        for (auto& it : rows) {
            std::string id = S(it, "id");
            if (id.empty() || !seen.insert(id).second) continue;
            json book = {{"id", id}, {"title", S(it, "title")}, {"title_en", !S(it, "title_en").empty() ? S(it, "title_en") : S(it, "title")},
                         {"title_jp", S(it, "title_jp")}, {"poster_url", S(it, "cover")},
                         {"src", id.find(':') != std::string::npos ? id.substr(0, id.find(':')) : "md"}};
            if (reading && it.contains("chapters")) book["episodes"] = it["chapters"];
            MgItem mi;
            mi.book = book;
            mg_items.push_back(mi);
            idxs.push_back((int)mg_items.size() - 1);
        }
        if (!idxs.empty()) mg_groups.emplace_back(label, idxs);
    };
    if (g.contains("results")) {
        add_group(S(g, "label"), g["results"], false);
    } else {
        add_group("CONTINUE READING", g["reading"], true);
        add_group("MY LIST", g["plan"], false);
        add_group("POPULAR", g["popular"], false);
    }
    tv_sec = tv_idx = 0;
    status = mg_items.empty() ? "No manga found" : std::to_string(mg_items.size()) + " manga";
    int gen = mg_gen;
    for (int i = 0; i < (int)mg_items.size(); i++) {
        std::string url = S(mg_items[i].book, "poster_url");
        if (url.empty()) continue;
        auto box = std::make_shared<std::shared_ptr<const Image>>();
        tasks_.run(
            [url, box] {
                *box = decode_thumb(core::fetch_poster(url), 400, 600);
                return json(true);
            },
            [this, gen, i, box](const json&, const std::string& e) {
                if (e.empty() && gen == mg_gen && i < (int)mg_items.size() && *box)
                    mg_items[i].poster = r.make_texture((*box)->rgba.data(), (*box)->w, (*box)->h);
            });
    }
}

void App::mg_open(int item) {
    if (item < 0 || item >= (int)mg_items.size()) return;
    const json& bj = mg_items[item].book;
    auto b = std::make_shared<Book>();
    b->id = S(bj, "id");
    b->title = S(bj, "title");
    b->title_en = S(bj, "title_en");
    b->title_jp = S(bj, "title_jp");
    b->poster_url = S(bj, "poster_url");
    b->src = S(bj, "src").empty() ? "md" : S(bj, "src");
    mg_read.clear();
    if (bj.contains("episodes") && bj["episodes"].is_array())
        for (auto& c : bj["episodes"])
            if (c.is_string()) mg_read.insert(c.get<std::string>());
    mg_book = b;
    mg_open_item = item;
    status = "Loading chapters of " + b->title + "...";
    ensure_chapters(b, [this, b] {
        if (mg_book != b) return;
        mg_chapters = b->all_eps;
        std::stable_sort(mg_chapters.begin(), mg_chapters.end(), [](const std::string& x, const std::string& y) { return chapter_num(x) < chapter_num(y); });
        mg_chapters.erase(std::unique(mg_chapters.begin(), mg_chapters.end()), mg_chapters.end());
        mg_detail = true;
        tv_ep = -2;
        status = std::to_string(mg_chapters.size()) + " chapters" + (!b->source.empty() ? " (" + b->source + ")" : "");
    });
}

void App::mg_load(const std::string& url) {
    if (mg_tex.count(url) || mg_busy.count(url)) return;
    mg_busy.insert(url);
    auto box = std::make_shared<std::shared_ptr<const Image>>();
    std::string chapter = mg_chapter;
    tasks_.run(
        [url, box] {
            *box = decode_thumb(core::fetch_manga_image(url), 1100, 1600);
            return json(true);
        },
        [this, url, box, chapter](const json&, const std::string& err) {
            mg_busy.erase(url);
            if (!err.empty()) {
                status = "Page failed to load";
                return;
            }
            if (*box && mg_reading && chapter == mg_chapter) mg_tex[url] = r.make_texture((*box)->rgba.data(), (*box)->w, (*box)->h);
        });
}

void App::mg_read_chapter(const std::string& ch) {
    if (!mg_book || !mg_book->chapters) return;
    json chj;
    for (auto& c : *mg_book->chapters)
        if (S(c, "chapter") == ch) {
            chj = c;
            break;
        }
    if (chj.is_null()) {
        status = "That chapter isn't available";
        return;
    }
    status = "Loading chapter " + ch + "...";
    BookP book = mg_book;
    tasks_.run([chj] { return core::chapter_pages(chj); },
               tracked("Loading chapter " + ch, [this, ch, book](const json& urls, const std::string& err) {
                   if (book != mg_book) return;
                   if (!err.empty() || urls.empty()) {
                       status = "Couldn't load chapter " + ch + ": " + (!err.empty() ? err : "no pages");
                       return;
                   }
                   mg_pages.clear();
                   for (auto& u : urls) mg_pages.push_back(u.get<std::string>());
                   mg_tex.clear();
                   mg_busy.clear();
                   mg_chapter = ch;
                   mg_cur = 0;
                   mg_scroll = 0;
                   mg_strip = -1;
                   mg_reading = true;
                   mg_hud = t + 5;
                   mg_read.insert(ch);
                   core::mr_add(book->id, !book->title_en.empty() ? book->title_en : book->title, book->poster_url, ch, book->title_jp);
               }));
}

void App::mg_step_chapter(int dir) {
    auto it = std::find(mg_chapters.begin(), mg_chapters.end(), mg_chapter);
    if (it == mg_chapters.end()) return;
    long i = (it - mg_chapters.begin()) + dir;
    if (i < 0 || i >= (long)mg_chapters.size()) {
        status = dir > 0 ? "That was the last chapter" : "That's the first chapter";
        mg_hud = t + 3;
        return;
    }
    mg_read_chapter(mg_chapters[(size_t)i]);
}

void App::mg_close_reader() {
    mg_reading = false;
    mg_tex.clear();
    mg_busy.clear();
    mg_pages.clear();
}

// pages shown together: the cover alone, then pairs; a landscape page is a spread of its own
static std::vector<std::vector<int>> spreads_of(const std::vector<std::string>& pages, const std::map<std::string, TexP>& tex) {
    std::vector<std::vector<int>> out;
    int n = (int)pages.size();
    auto landscape = [&](int i) {
        auto it = tex.find(pages[(size_t)i]);
        return it != tex.end() && it->second && it->second->h > 0 && (double)it->second->w / it->second->h > 1.05;
    };
    int p = 0;
    if (n) {
        out.push_back({0});
        p = 1;
    }
    while (p < n) {
        if (p + 1 < n && !landscape(p) && !landscape(p + 1)) {
            out.push_back({p, p + 1});
            p += 2;
        } else {
            out.push_back({p});
            p += 1;
        }
    }
    return out;
}

bool App::mg_key(const std::string& key) {
    bool up = key == "ArrowUp", down = key == "ArrowDown", left = key == "ArrowLeft", right = key == "ArrowRight";
    if (key == "n") {
        mg_step_chapter(1);
        return true;
    }
    if (key == "p") {
        mg_step_chapter(-1);
        return true;
    }
    if (key == "Enter") {
        mg_hud = t + 8;
        return true;
    }
    if (!(up || down || left || right)) return false;
    mg_hud = t + 3;
    double H = size.second;
    if (mg_strip == 1) {  // a long vertical strip: scroll
        if (down) mg_scroll += H * 0.6;
        else if (up) mg_scroll = std::max(0.0, mg_scroll - H * 0.6);
        else if (right) mg_step_chapter(manga_rtl ? -1 : 1);
        else if (left) mg_step_chapter(manga_rtl ? 1 : -1);
        return true;
    }
    auto sp = spreads_of(mg_pages, mg_tex);
    int last = (int)sp.size() - 1;
    int dir = down ? 1 : up ? -1 : ((left && manga_rtl) || (right && !manga_rtl)) ? 1 : -1;
    int next = mg_cur + dir;
    if (next > last) {
        mg_step_chapter(1);
    } else if (next < 0) {
        mg_step_chapter(-1);
    } else {
        mg_cur = next;
    }
    return true;
}

void App::mg_draw(double W, double H) {
    const Theme& th = *theme_;
    r.rect(0, 0, W, H, C4(0, 0, 0, 1));
    int n = (int)mg_pages.size();
    auto tex_of = [&](int i) -> TexP {
        auto it = mg_tex.find(mg_pages[(size_t)i]);
        return it != mg_tex.end() ? it->second : nullptr;
    };
    if (mg_strip < 0) {
        for (int i = 0; i < n; i++)
            if (auto tx = tex_of(i)) {
                mg_strip = tx->h > 0 && (double)tx->w / tx->h < 0.5 ? 1 : 0;
                break;
            }
    }
    std::set<std::string> keep;
    if (mg_strip == 1) {
        double wfit = std::min(W * 0.62, 900.0);
        std::vector<double> ys(n + 1, 0.0);
        for (int i = 0; i < n; i++) {
            auto tx = tex_of(i);
            double a = tx && tx->h > 0 ? (double)tx->w / tx->h : 0.35;
            ys[i + 1] = ys[i] + wfit / a;
        }
        mg_scroll = std::clamp(mg_scroll, 0.0, std::max(0.0, ys[n] - H));
        for (int i = 0; i < n; i++) {
            if (ys[i + 1] < mg_scroll - H || ys[i] > mg_scroll + 2.2 * H) continue;
            keep.insert(mg_pages[(size_t)i]);
            mg_load(mg_pages[(size_t)i]);
            auto tx = tex_of(i);
            double y = ys[i] - mg_scroll;
            if (tx && y < H && y + (ys[i + 1] - ys[i]) > 0) r.quad2d(tx, (W - wfit) / 2, y, wfit, ys[i + 1] - ys[i]);
        }
    } else {
        auto sp = spreads_of(mg_pages, mg_tex);
        int last = std::max(0, (int)sp.size() - 1);
        mg_cur = std::clamp(mg_cur, 0, last);
        for (int s = std::max(0, mg_cur - 1); s <= std::min(last, mg_cur + 2); s++)
            for (int pg : sp[(size_t)s]) {
                keep.insert(mg_pages[(size_t)pg]);
                mg_load(mg_pages[(size_t)pg]);
            }
        if (!sp.empty()) {
            std::vector<int> pgs = sp[(size_t)mg_cur];
            if (pgs.size() == 2 && manga_rtl) std::swap(pgs[0], pgs[1]);  // right-to-left: the first page sits on the right
            double total_w = 0, hh = H * 0.985;
            std::vector<double> aspects;
            for (int pg : pgs) {
                auto tx = tex_of(pg);
                double a = tx && tx->h > 0 ? (double)tx->w / tx->h : 0.7;
                aspects.push_back(a);
                total_w += hh * a;
            }
            double k = total_w > W * 0.99 ? W * 0.99 / total_w : 1.0;  // auto-fit: height first, then shrink to the width
            double x = (W - total_w * k) / 2, ph = hh * k, y = (H - ph) / 2;
            for (size_t i = 0; i < pgs.size(); i++) {
                double w = hh * aspects[i] * k;
                if (auto tx = tex_of(pgs[i])) r.quad2d(tx, x, y, w, ph);
                else txt("loading page " + std::to_string(pgs[i] + 1) + "...", x + w / 2, H / 2, 16, th.dim, true);
                x += w;
            }
        }
    }
    for (auto it = mg_tex.begin(); it != mg_tex.end();) it = keep.count(it->first) ? std::next(it) : mg_tex.erase(it);  // free far-away pages
    if (n == 0) txt("loading chapter...", W / 2, H / 2, 18, th.dim, true);
    if (t < mg_hud) {  // page info, fades away on its own
        int shown = mg_strip == 1 ? std::min(n, (int)(mg_scroll / std::max(1.0, H * 1.2)) + 1) : mg_cur + 1;
        std::string info = "CH " + mg_chapter + "    page " + std::to_string(shown) + " / " + std::to_string(n) + "    ";
        info += mg_strip == 1 ? "UP / DOWN scroll" : std::string(manga_rtl ? "LEFT = next page (right to left)" : "RIGHT = next page");
        info += "    REW / FF chapter    MENU flip direction";
        r.rect(0, H - 40, W, 40, C4(0, 0, 0, 0.7));
        txt(info, W / 2, H - 29, 14, th.text, true);
    }
}
