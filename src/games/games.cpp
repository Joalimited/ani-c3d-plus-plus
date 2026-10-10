#include "games.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>

#include "../app/tasks.hpp"
#include "../gfx/renderer.hpp"
#include "../gfx/text.hpp"
#include "../util/http.hpp"
#include "../util/strutil.hpp"
#include "../world/scene.hpp"
#include "../world/themes.hpp"
#include "galaga.hpp"
#include "maze.hpp"

using json = nlohmann::json;

PyRandom& grand() {
    static PyRandom r((long long)std::chrono::system_clock::now().time_since_epoch().count());
    return r;
}

template <class T>
static void shuffle(std::vector<T>& v) {  // random.shuffle
    for (size_t i = v.size(); i > 1; i--) std::swap(v[i - 1], v[(size_t)grand().randbelow((long long)i)]);
}

static const std::map<std::string, std::pair<int, int>> DIRS = {{"ArrowUp", {0, -1}},   {"w", {0, -1}}, {"ArrowDown", {0, 1}},  {"s", {0, 1}},
                                                                {"ArrowLeft", {-1, 0}}, {"a", {-1, 0}}, {"ArrowRight", {1, 0}}, {"d", {1, 0}}};

void flat_label(Renderer& r, TextCache& text, const std::string& s, double x, double y, double z, double height, C3 color, bool center) {
    TextTex tt = text.get(s, 44, color);
    double ww = height * tt.w / tt.h;
    r.quad3d(tt.tex, mx::translate(x - (center ? ww / 2 : 0), y, z - height / 2) * mx::rot_x(PI / 2) * mx::scale(ww, height, 1));
}

std::vector<Box> roller(double cx, double cy, double cz, double yaw, double roll, const std::vector<RollPart>& parts) {
    double c = std::cos(roll), s = std::sin(roll);
    double cy_ = std::cos(yaw), sy_ = std::sin(yaw);
    std::vector<Box> out;
    for (auto& p : parts) {
        double x = p.ox * c - p.oy * s, y = p.ox * s + p.oy * c;
        out.push_back(box(cx + x * cy_ + p.oz * sy_, cy + y, cz - x * sy_ + p.oz * cy_, p.sx, p.sy, p.sz, p.col, p.glow, yaw, 0.6, 0.0, 0.0, roll + p.rz, 0.0));
    }
    return out;
}

// a barrel (or, in the Halloween theme, a jack-o'-lantern) seen end-on, radius ~0.55, with markings that show the roll
std::vector<RollPart> barrel_parts(const std::string& fx, double t) {
    std::vector<RollPart> P;
    if (fx == "halloween") {
        C3 orange{1.0, 0.45, 0.05}, rib{0.75, 0.28, 0.02};
        double glow = 1.3 + 0.3 * std::sin(t * 6);
        for (double a : {0.0, PI / 3, 2 * PI / 3}) P.push_back({0, 0, 0, 1.1, 0.62, 0.95, orange, 0.7, a});
        for (int sz : {-1, 1})
            for (double a : {PI / 2, PI / 6, -PI / 6}) P.push_back({0, 0, sz * 0.48, 1.08, 0.07, 0.04, rib, 0.3, a});
        P.push_back({0, 0.6, 0, 0.14, 0.26, 0.14, {0.25, 0.6, 0.1}, 0.3, 0.2});  // stem
        for (int sz : {-1, 1}) {  // carved face on both ends, glowing from inside
            P.push_back({-0.18, 0.12, sz * 0.49, 0.14, 0.14, 0.03, {1.0, 0.9, 0.3}, glow, PI / 4});
            P.push_back({0.18, 0.12, sz * 0.49, 0.14, 0.14, 0.03, {1.0, 0.9, 0.3}, glow, PI / 4});
            P.push_back({0, -0.17, sz * 0.49, 0.42, 0.09, 0.03, {1.0, 0.9, 0.3}, glow, 0.0});
        }
        return P;
    }
    C3 wood{0.62, 0.36, 0.14}, band{0.25, 0.16, 0.08};
    for (double a : {0.0, PI / 3, 2 * PI / 3}) P.push_back({0, 0, 0, 1.1, 0.5, 0.95, wood, 0.45, a});
    for (int sz : {-1, 1})
        for (double a : {0.0, PI / 2}) P.push_back({0, 0, sz * 0.48, 0.98, 0.1, 0.04, band, 0.2, a});  // cross on the lid
    for (int sz : {-1, 1})
        for (double a : {0.0, PI / 3, 2 * PI / 3}) P.push_back({0, 0, sz * 0.36, 1.13, 0.52, 0.06, band, 0.2, a});  // hoops
    return P;
}

FallBook& tip_book(FallBook& bk, double push_z, double push_x, double spin) {
    bk.phase = "tip";
    bk.tilt = 0.0;
    bk.wt = 2.0 + 2.5 * push_z;
    bk.vx = push_x;
    bk.vy = bk.vz = bk.wz = 0.0;
    bk.spin = spin;
    bk.age = 0.0;
    return bk;
}

// advance a tipping / falling book; false once it has landed and faded out
bool step_book(FallBook& bk, double dt, double floor) {
    bk.age += dt;
    if (bk.phase == "tip") {  // rotating about the front bottom edge, gravity pulling harder as it leans
        bk.wt += (14 * std::sin(std::max(bk.tilt, 0.15))) * dt;
        bk.tilt += bk.wt * dt;
        if (bk.tilt > 1.05) {  // over the edge: fly free, keeping the rotation it had
            double hh = bk.h / 2, dd = bk.d / 2;
            double c = std::cos(bk.tilt), s = std::sin(bk.tilt);
            bk.px = bk.x;
            bk.py = bk.y0 + hh * c + dd * s;
            bk.pz = bk.z + dd + hh * s - dd * c;
            bk.vy = 1.0;
            bk.vz = 2.2 + bk.wt * 0.4;
            bk.wz = grand().uniform(-3, 3) * bk.spin;
            bk.phase = "fall";
        }
        return true;
    }
    if (bk.phase == "fall") {
        bk.vy -= 26 * dt;
        bk.px += bk.vx * dt;
        bk.py += bk.vy * dt;
        bk.pz += bk.vz * dt;
        bk.tilt += bk.wt * dt;
        bk.roll += bk.wz * dt;
        if (bk.py - 0.3 <= floor) {
            if (std::fabs(bk.vy) > 4) {  // one bounce, losing most of the spin
                bk.py = floor + 0.3;
                bk.vy = -bk.vy * 0.28;
                bk.wt *= 0.35;
                bk.wz *= 0.35;
                bk.vx *= 0.5;
                bk.vz *= 0.4;
            } else {  // settle flat on its back and fade
                bk.py = floor + bk.d / 2;
                bk.phase = "rest";
                bk.rest = 0.0;
                bk.tilt = PI / 2;
            }
        }
        return true;
    }
    bk.rest += dt;
    return bk.rest < 1.6;
}

std::vector<Box> book_boxes(const FallBook& bk, double glow) {
    C3 col = bk.color;
    if (bk.phase == "rest" && bk.rest > 0.8) col = col * std::max(0.0, 1 - (bk.rest - 0.8) / 0.8);
    double cx, cy, cz, roll;
    if (bk.phase == "tip") {
        double hh = bk.h / 2, dd = bk.d / 2;
        double c = std::cos(bk.tilt), s = std::sin(bk.tilt);
        cx = bk.x;
        cy = bk.y0 + hh * c + dd * s;
        cz = bk.z + dd + hh * s - dd * c;
        roll = 0.0;
    } else {
        cx = bk.px;
        cy = bk.py;
        cz = bk.pz;
        roll = bk.roll;
    }
    return {box(cx, cy, cz, bk.w, bk.h, bk.d, col, glow, 0.0, 0.6, 0.0, 0.0, roll, bk.tilt)};
}

// the tiny person from the library, as a list of boxes (also the hero of the barrel climb)
std::vector<Box> voxel_person(const Theme& th, double x, double y, double z, double face, double swing, bool arms_up, double sc) {
    C3 skin{0.96, 0.80, 0.66};
    std::vector<Box> out = {box(x - 0.4 * sc, y + 0.45 * sc, z + swing * 0.5, 0.56 * sc, 0.9 * sc, 0.6 * sc, {0.15, 0.15, 0.3}, 0.0),
                            box(x + 0.4 * sc, y + 0.45 * sc, z - swing * 0.5, 0.56 * sc, 0.9 * sc, 0.6 * sc, {0.15, 0.15, 0.3}, 0.0),
                            box(x, y + 1.3 * sc, z, 0.95 * sc, 0.95 * sc, 0.55 * sc, th.ped_sel, 0.2),
                            box(x, y + 2.1 * sc, z, 0.75 * sc, 0.7 * sc, 0.7 * sc, skin, 0.1),
                            box(x, y + 2.5 * sc, z, 0.85 * sc, 0.25 * sc, 0.8 * sc, th.core, 0.3),
                            box(x + face * 0.42 * sc, y + 2.05 * sc, z + 0.3 * sc, 0.12, 0.12, 0.15, skin, 0.0)};
    for (int sx : {-1, 1}) out.push_back(box(x + sx * 0.62 * sc, y + (arms_up ? 1.95 : 1.3) * sc, z, 0.24 * sc, 0.8 * sc, 0.24 * sc, skin, 0.05));
    return out;
}

// ============================================================ SNAKE
namespace {
class Snake : public Game {
public:
    explicit Snake(GameHost& a) : Game(a) {
        name = "SNAKE";
        help = "arrows / WASD steer   R restart   Esc exit";
    }
    static constexpr int GW = 22, GH = 14;
    std::vector<std::pair<int, int>> body;
    std::pair<int, int> dir{1, 0}, food;
    std::vector<std::pair<int, int>> queue;
    double acc = 0, speed = 0.13;
    CamGoal cam() const override { return {{0, 0, 1.0}, 0.0, 62 * PI / 180, 31}; }
    void reset() override {
        body = {{5, 7}, {4, 7}, {3, 7}};
        dir = {1, 0};
        queue.clear();
        score = 0;
        over = false;
        acc = 0;
        speed = 0.13;
        food = spawn();
    }
    std::pair<int, int> spawn() {
        std::vector<std::pair<int, int>> free;
        for (int x = 0; x < GW; x++)
            for (int y = 0; y < GH; y++)
                if (std::find(body.begin(), body.end(), std::make_pair(x, y)) == body.end()) free.push_back({x, y});
        return grand().choice(free);
    }
    void key(const std::string& k) override {
        if (k == "r") reset();
        else if (DIRS.count(k) && queue.size() < 2) queue.push_back(DIRS.at(k));
    }
    void update(double dt, const Keys&) override {
        if (over) return;
        acc += dt;
        while (acc >= speed && !over) {
            acc -= speed;
            while (!queue.empty()) {
                auto d = queue.front();
                queue.erase(queue.begin());
                if (!(d.first + dir.first == 0 && d.second + dir.second == 0)) {
                    dir = d;
                    break;
                }
            }
            std::pair<int, int> head{body[0].first + dir.first, body[0].second + dir.second};
            bool hit_self = std::find(body.begin(), body.end() - 1, head) != body.end() - 1;
            if (!(0 <= head.first && head.first < GW && 0 <= head.second && head.second < GH) || hit_self) {
                over = true;
                return;
            }
            body.insert(body.begin(), head);
            if (head == food) {
                score += 10;
                speed = std::max(0.05, speed * 0.97);
                food = spawn();
            } else {
                body.pop_back();
            }
        }
    }
    std::pair<double, double> cell(int i, int j) const { return {i - GW / 2.0 + 0.5, j - GH / 2.0 + 0.5}; }
    std::vector<Box> build(Renderer& r, TextCache& text, const Theme& th, double t) override {
        std::vector<Box> out = {box(0, -0.2, 0, GW + 0.8, 0.4, GH + 0.8, th.slab, 0.1)};
        for (double x : {-GW / 2.0 - 0.25, GW / 2.0 + 0.25}) out.push_back(box(x, 0.3, 0, 0.3, 0.6, GH + 0.8, th.wire, 0.7));
        for (double z : {-GH / 2.0 - 0.25, GH / 2.0 + 0.25}) out.push_back(box(0, 0.3, z, GW + 0.8, 0.6, 0.3, th.wire, 0.7));
        for (size_t n = 0; n < body.size(); n++) {
            auto [x, z] = cell(body[n].first, body[n].second);
            bool head = n == 0;
            out.push_back(box(x, 0.45, z, 0.88, 0.7, 0.88, (head && !over) ? th.ep_play : th.ep_seen, head ? 0.6 : 0.15));
        }
        auto [fx, fz] = cell(food.first, food.second);
        out.push_back(box(fx, 0.55 + 0.12 * std::sin(t * 6), fz, 0.6, 0.6, 0.6, th.tower, 1.3, t * 2));
        flat_label(r, text, "SCORE " + std::to_string(score), 0, 0.03, -GH / 2.0 - 1.5, 0.9, th.t_field);
        if (over) flat_label(r, text, "GAME OVER  -  press R", 0, 0.5, 0, 1.6, {1, 0.3, 0.3});
        return out;
    }
};

// ============================================================ BREAKOUT
class Breakout : public Game {
public:
    explicit Breakout(GameHost& a) : Game(a) {
        name = "BREAKOUT";
        help = "left/right (A/D) move paddle   Space launch   R restart   Esc exit";
    }
    static constexpr double X = 11.0, Z = 9.0, PZ = 8.2, PW = 3.6;
    double px = 0;
    int lives = 3;
    struct Brick {
        double x, z;
        bool alive;
        int rw;
    };
    std::vector<Brick> bricks;
    double ball[2] = {0, 0}, vel[2] = {0, 0};
    bool stuck = true, won = false;
    CamGoal cam() const override { return {{0, 0, 1.5}, 0.0, 62 * PI / 180, 36}; }
    void reset() override {
        px = 0;
        lives = 3;
        score = 0;
        over = false;
        bricks.clear();
        for (int rw = 0; rw < 5; rw++)
            for (int c = 0; c < 10; c++) bricks.push_back({-X + 1.1 + c * 2.2, -Z + 2.0 + rw * 1.0, true, rw});
        stick();
        won = false;
    }
    void stick() {
        ball[0] = px;
        ball[1] = PZ - 0.7;
        vel[0] = vel[1] = 0;
        stuck = true;
    }
    void key(const std::string& k) override {
        if (k == "r") reset();
        else if (k == " " && stuck && !over) {
            stuck = false;
            vel[0] = grand().choice(std::vector<double>{-4.0, 4.0});
            vel[1] = -11.0;
        }
    }
    void update(double dt, const Keys& keys) override {
        if (over) return;
        int move = (held(keys, {"ArrowRight", "d"}) ? 1 : 0) - (held(keys, {"ArrowLeft", "a"}) ? 1 : 0);
        px = std::max(-X + PW / 2, std::min(X - PW / 2, px + move * 17 * dt));
        if (stuck) {
            ball[0] = px;
            return;
        }
        double speed = std::hypot(vel[0], vel[1]) + dt * 0.2;
        for (int k = 0; k < 3; k++) {  // sub-steps keep the ball from tunnelling
            double h = dt / 3;
            ball[0] += vel[0] * h;
            ball[1] += vel[1] * h;
            double bx = ball[0], bz = ball[1];
            if (bx < -X + 0.3 || bx > X - 0.3) {
                vel[0] *= -1;
                ball[0] = std::max(-X + 0.3, std::min(X - 0.3, bx));
            }
            if (bz < -Z + 0.3) vel[1] = std::fabs(vel[1]);
            if (vel[1] > 0 && PZ - 0.45 <= bz && bz <= PZ + 0.3 && std::fabs(bx - px) <= PW / 2 + 0.3) {
                double off = (bx - px) / (PW / 2);
                vel[0] = off * 9.0;
                vel[1] = -std::sqrt(std::max(speed * speed - (off * 9.0) * (off * 9.0), 36.0));
            }
            if (bz > Z + 1.5) {
                lives -= 1;
                if (lives <= 0) over = true;
                stick();
                return;
            }
            for (auto& b : bricks) {
                if (b.alive && std::fabs(bx - b.x) < 1.1 + 0.3 && std::fabs(bz - b.z) < 0.5 + 0.3) {
                    b.alive = false;
                    score += 10;
                    if ((1.4 - std::fabs(bx - b.x)) < (0.8 - std::fabs(bz - b.z))) vel[0] *= -1;
                    else vel[1] *= -1;
                    break;
                }
            }
        }
        bool any = false;
        for (auto& b : bricks) any = any || b.alive;
        if (!any) {
            over = true;
            won = true;
        }
    }
    std::vector<Box> build(Renderer& r, TextCache& text, const Theme& th, double t) override {
        const C3 cols[5] = {th.ep_play, th.link, th.tower, th.ep_seen, th.ep};
        std::vector<Box> out = {box(0, -0.2, 0, X * 2 + 1, 0.4, Z * 2 + 3, th.slab, 0.1)};
        for (double x : {-X - 0.25, X + 0.25}) out.push_back(box(x, 0.4, 0.75, 0.3, 0.8, Z * 2 + 1.5, th.wire, 0.7));
        out.push_back(box(0, 0.4, -Z - 0.25, X * 2 + 0.8, 0.8, 0.3, th.wire, 0.7));
        for (auto& b : bricks)
            if (b.alive) out.push_back(box(b.x, 0.45, b.z, 2.0, 0.8, 0.86, cols[b.rw % 5], 0.25));
        out.push_back(box(px, 0.4, PZ, PW, 0.6, 0.6, th.core, 0.9));
        out.push_back(box(ball[0], 0.45, ball[1], 0.6, 0.6, 0.6, {1, 1, 1}, 1.5));
        flat_label(r, text, "SCORE " + std::to_string(score) + "    LIVES " + std::to_string(std::max(lives, 0)), 0, 0.03, -Z - 1.7, 0.9, th.t_field);
        if (over) flat_label(r, text, won ? "YOU WIN!  press R" : "GAME OVER  -  press R", 0, 0.5, 1, 1.6, {1, 0.9, 0.3});
        else if (stuck) flat_label(r, text, "press SPACE to launch", 0, 0.3, 3, 1.0, th.t_ped);
        return out;
    }
};

// ============================================================ POSTER QUIZ
class Quiz : public Game {
public:
    explicit Quiz(GameHost& a) : Game(a) {
        name = "POSTER QUIZ";
        help = "press 1-4 or click an answer   R restart   Esc exit";
    }
    static constexpr int ROUNDS = 8;
    struct Card {
        std::string title;
        TexP poster;
    };
    std::vector<Card> pool;
    int round = 0;
    double t0 = 0;
    struct Reveal {
        int i;
        bool ok;
        double t;
    };
    std::optional<Reveal> reveal;
    struct Cur {
        int answer;
        std::vector<int> choices;
        double cu, cv;
    };
    std::optional<Cur> cur;
    CamGoal cam() const override { return {{0, 3.4, 3.0}, 0.0, 17 * PI / 180, 25}; }
    void reset() override {
        pool.clear();
        for (auto& p : app.world_scene().peds)
            if (p.poster) pool.push_back({p.title, p.poster});
        round = 0;
        score = 0;
        over = false;
        t0 = app.now();
        reveal.reset();
        cur.reset();
        if (pool.size() >= 4) next_round();
    }
    void next_round() {
        if (round >= std::min(ROUNDS, (int)pool.size())) {
            over = true;
            return;
        }
        int answer = (int)grand().index(pool.size());
        std::vector<int> rest;
        for (int i = 0; i < (int)pool.size(); i++)
            if (i != answer) rest.push_back(i);
        // random.sample(rest, 3)
        std::vector<int> others;
        for (int k = 0; k < 3; k++) {
            size_t j = grand().index(rest.size());
            others.push_back(rest[j]);
            rest.erase(rest.begin() + (long)j);
        }
        std::vector<int> choices = others;
        choices.push_back(answer);
        shuffle(choices);
        double cu = grand().uniform(0.3, 0.7), cv = grand().uniform(0.3, 0.7);
        cur = Cur{answer, choices, cu, cv};
        round++;
        t0 = app.now();
        reveal.reset();
    }
    void pick(int i) {
        if (over || !cur || reveal) return;
        bool correct = cur->choices[i] == cur->answer;
        if (correct) score += std::max(10, (int)(100 - 7 * (app.now() - t0)));
        reveal = Reveal{i, correct, app.now()};
    }
    void key(const std::string& k) override {
        if (k == "r") reset();
        else if (k.size() == 1 && k[0] >= '1' && k[0] <= '4') pick(k[0] - '1');
    }
    std::vector<std::string> achievements() override { return (over && score >= 600) ? std::vector<std::string>{"quiz"} : std::vector<std::string>{}; }
    void update(double, const Keys&) override {
        if (reveal && app.now() - reveal->t > 1.7) next_round();
    }
    std::vector<Box> build(Renderer& r, TextCache& text, const Theme& th, double t) override {
        std::vector<Box> out = {box(0, -0.2, 3, 26, 0.4, 14, th.slab, 0.1), box(0, 5.2, -0.4, 6.8, 9.8, 0.3, th.wood, 0.3)};
        if (pool.size() < 4 || !cur) {
            flat_label(r, text, pool.size() < 4 ? "Open HISTORY or search first (needs 4+ posters)" : "FINAL SCORE " + std::to_string(score), 0, 0.3, 4, 1.2, th.t_field);
            return out;
        }
        double prog = reveal ? 1.0 : std::min(1.0, (t - t0) / 14);
        double s = 0.2 + 0.8 * prog;
        double u0 = std::min(std::max(cur->cu - s / 2, 0.0), 1 - s);
        double v0 = std::min(std::max(cur->cv - s / 2, 0.0), 1 - s);
        M4 m = mx::translate(-3.0, 10.1, -0.2) * mx::scale(6.0, -9.0, 1);
        r.quad3d(pool[cur->answer].poster, m, C4(1, 1, 1, 1), UV{(float)u0, (float)v0, (float)(u0 + s), (float)(v0 + s)});
        for (int i = 0; i < 4; i++) {
            double x = -7.5 + i * 5.0;
            double glow = 0.1;
            C3 col = th.ped;
            if (reveal) {
                if (cur->choices[i] == cur->answer) {
                    glow = 1.0;
                    col = th.ep_seen;
                } else if (reveal->i == i) {
                    glow = 1.0;
                    col = C3(1.0, 0.2, 0.2);
                }
            }
            out.push_back(box(x, 0.3, 6.5, 4.2, 0.6, 2.4, col, glow));
            flat_label(r, text, std::to_string(i + 1), x, 0.62, 6.5, 1.5, {1, 1, 1});
        }
        return out;
    }
    void hud(double W, double H) override {
        if (over || !cur || pool.size() < 4) return;
        const Theme& th = app.theme();
        app.txt("ROUND " + std::to_string(round) + "/" + std::to_string(std::min(ROUNDS, (int)pool.size())) + "    SCORE " + std::to_string(score), W / 2, 92, 18,
                th.accent, true);
        if (reveal)
            app.txt(reveal->ok ? "CORRECT!" : "It was: " + su::utf8_substr(pool[cur->answer].title, 0, 40), W / 2, 120, 16, reveal->ok ? th.good : C3(1, 0.4, 0.4), true);
        double bw = std::min(330.0, (W - 80) / 4);
        for (int i = 0; i < 4; i++) {
            double x = W / 2 - 2 * bw - 12 + i * (bw + 8);
            app.button(std::to_string(i + 1) + "  " + su::utf8_substr(pool[cur->choices[i]].title, 0, 26), x, H - 190, bw, 38, [this, i] { pick(i); });
        }
    }
};

// ============================================================ BARREL CLIMB
const C3 GOLD_{1.0, 0.82, 0.2};

// Donkey Kong on the bookshelf: the shelves are the girders. Climb to the top, jump the barrels, grab a hammer to smash
// them. Five stages (more shelves, more barrels, fireballs), then the loop starts again faster.
class KongClimb : public Game {
public:
    explicit KongClimb(GameHost& a) : Game(a) {
        name = "BARREL CLIMB";
        help = "A/D move   W/S climb ladders   Space jump   grab the hammer to smash barrels   R restart   Esc exit";
    }
    static constexpr double HW = 14.0, GAP = 4.4, HAMMER_TIME = 9.0;
    struct Layout {
        std::string name;
        int N;
        std::map<int, std::vector<double>> ladders;
        std::vector<std::pair<int, double>> hammers;
        double spawn, speed;
        int fire;
    };
    static const std::vector<Layout>& layouts() {
        static const std::vector<Layout> L = {
            {"THE SHELVES", 5, {{0, {9.0}}, {1, {-9.0, -1.0}}, {2, {9.0, 1.5}}, {3, {-9.0}}}, {{1, -4.5}, {3, 3.0}}, 2.6, 5.5, 0},
            {"STACKS AND STAIRS", 5, {{0, {-9.0, 5.0}}, {1, {9.0, -3.0}}, {2, {-8.0, 2.0}}, {3, {10.0}}}, {{1, 1.0}, {3, -4.0}}, 2.1, 6.2, 0},
            {"FIREBALL ALLEY", 6, {{0, {9.0}}, {1, {-9.0, 2.0}}, {2, {9.0, -2.0}}, {3, {-9.0, 4.0}}, {4, {8.0}}}, {{2, -5.0}, {4, 5.0}}, 2.0, 6.4, 1},
            {"THE LONG CLIMB", 6, {{0, {-10.0}}, {1, {10.0}}, {2, {-10.0, 0.0}}, {3, {10.0}}, {4, {-9.0}}}, {{1, -6.0}, {3, 6.0}, {4, 0.0}}, 1.5, 7.2, 2},
            {"KONG'S PENTHOUSE", 7, {{0, {9.0}}, {1, {-9.0, 3.0}}, {2, {9.0, -4.0}}, {3, {-9.0, 1.0}}, {4, {9.0, -2.0}}, {5, {-8.0}}},
             {{2, -6.0}, {4, 5.0}, {5, 0.0}}, 1.3, 7.6, 3},
        };
        return L;
    }
    int stage = 1, cycle = 0, smashes = 0, lives = 3, N = 5;
    bool won = false, keep_stage = false;
    const Layout* lay = nullptr;
    std::vector<double> ly;
    struct Barrel {
        double x, y;
        int level, dir;
        std::string mode;
        double vy, spin;
        bool scored;
        std::set<std::pair<int, double>> ladder_checked;
        double roll, hop, hv;
    };
    std::vector<Barrel> barrels;
    struct Fire {
        double x, y;
        int level, dir;
        std::string mode;
        std::pair<int, int> lad;
        double think, spin;
    };
    std::vector<Fire> fires;
    struct Spark {
        double x, y, vx, vy, life;
    };
    std::vector<Spark> sparks;
    struct Hammer {
        int level;
        double x;
        bool taken;
    };
    std::vector<Hammer> hammers;
    std::vector<FallBook> shelf, falling;
    double chop = 0, spawn_t = 1.5, throw_ = 0, banner = 2.6, bonus = 4000, fire_t = 4.0;
    struct Player {
        double x, y, vy;
        std::string mode;
        int level, lad;
        double face, run, hammer;
    } p{};

    CamGoal cam() const override {
        double top = 0.3 + GAP * (N - 1);
        return {{0, top / 2 + 1.4, 0}, 0.0, 5 * PI / 180, 34 + (N - 5) * 5.5};
    }
    void reset() override {
        if (!keep_stage) {
            stage = 1;
            cycle = 0;
            smashes = 0;
        }
        keep_stage = false;
        lives = 3;
        over = false;
        won = false;
        if (stage == 1) score = 0;
        load_stage();
    }
    void load_stage() {
        const auto& L = layouts();
        lay = &L[(stage - 1) % L.size()];
        N = lay->N;
        ly.clear();
        for (int i = 0; i < N; i++) ly.push_back(0.3 + GAP * i);
        cycle = (stage - 1) / (int)L.size();
        barrels.clear();
        fires.clear();
        sparks.clear();
        hammers.clear();
        for (auto& [lv, x] : lay->hammers) hammers.push_back({lv, x, false});
        shelf.clear();
        falling.clear();  // the books standing behind each girder, and the ones knocked off it
        for (int lv = 0; lv < N; lv++) {
            double y = ly[lv];
            for (int i = 0; i < 26; i++) {
                double hh = 2.2 + ((i * 37 + (int)(y * 10)) % 9) * 0.14;
                FallBook b;
                b.level = lv;
                b.x = -HW + 0.6 + i * 1.08;
                b.y0 = y + 0.02;
                b.z = -0.95;
                b.w = 0.9;
                b.h = hh;
                b.d = 1.2;
                b.ci = i + (int)y;
                b.gone = false;
                shelf.push_back(b);
            }
        }
        chop = 0.0;
        spawn_t = 1.5;
        throw_ = 0.0;
        banner = 2.6;  // seconds the stage title is shown (the barrels wait)
        bonus = 4000 + 500 * cycle;
        fire_t = 4.0;
        respawn();
        if (app.is_current_game(this)) {
            CamGoal c = cam();
            app.world_scene().cam.goto_(c.target, c.yaw, c.pitch, c.dist);
        }
    }
    void respawn() {
        p = Player{-12.0, ly[0], 0.0, "ground", 0, -1, 1, 0.0, 0.0};
        barrels.erase(std::remove_if(barrels.begin(), barrels.end(), [](const Barrel& b) { return !(b.level >= 2); }), barrels.end());
        fires.clear();
    }
    void key(const std::string& k) override {
        if (k == "r") {
            stage = 1;
            cycle = 0;
            keep_stage = false;
            reset();
        } else if (k == " " && p.mode == "ground" && !over && p.hammer <= 0 && banner <= 0) {
            p.mode = "air";
            p.vy = 9.5;
        }
    }
    std::optional<double> ladder_at(int gap, double x, double tol = 0.7) const {
        auto it = lay->ladders.find(gap);
        if (it == lay->ladders.end()) return std::nullopt;
        for (double lx : it->second)
            if (std::fabs(lx - x) < tol) return lx;
        return std::nullopt;
    }
    double speed() const { return lay->speed + 0.9 * cycle; }
    std::vector<std::string> achievements() override {
        std::vector<std::string> out;
        if (stage >= 3) out.push_back("kong3");
        if (smashes >= 5) out.push_back("kong_hammer");
        return out;
    }
    // books on that shelf near x topple forward off the girder
    void knock(int level, double x, double rad, double push = 1.0) {
        const auto& cols = app.theme().books;
        for (auto& bk : shelf) {
            if (!bk.gone && bk.level == level && std::fabs(bk.x - x) < rad) {
                bk.gone = true;
                FallBook fb = bk;
                fb.color = cols[bk.ci % cols.size()];
                double pz = push * grand().uniform(0.7, 1.3);
                double pxv = (bk.x - x) * grand().uniform(0.5, 1.5);
                falling.push_back(tip_book(fb, pz, pxv));
            }
        }
    }
    void smash(double x, double y, int pts) {
        score += pts;
        smashes++;
        int lv = 0;
        for (int i = 1; i < N; i++)
            if (std::fabs(ly[i] - y) < std::fabs(ly[lv] - y)) lv = i;
        knock(lv, x, 1.6, 1.6);
        for (int i = 0; i < 10; i++) {
            double a = i * TAU / 10;
            sparks.push_back({x, y, std::cos(a) * 5, 3 + std::sin(a) * 4, 0.6});
        }
    }
    void lose_life() {
        lives--;
        if (lives <= 0) over = true;
        else {
            respawn();
            banner = 1.2;
        }
    }
    void update(double dt, const Keys& keys) override {
        for (auto& sp : sparks) {
            sp.life -= dt;
            sp.x += sp.vx * dt;
            sp.vy -= 14 * dt;
            sp.y += sp.vy * dt;
        }
        sparks.erase(std::remove_if(sparks.begin(), sparks.end(), [](const Spark& s) { return !(s.life > 0); }), sparks.end());
        falling.erase(std::remove_if(falling.begin(), falling.end(), [&](FallBook& b) { return !step_book(b, dt, 0.0); }), falling.end());
        for (auto& b : barrels) {  // the little landing hop (visual only)
            b.hv -= 30 * dt;
            b.hop = std::max(0.0, b.hop + b.hv * dt);
        }
        if (over) return;
        if (banner > 0) {
            banner -= dt;
            return;
        }
        bonus = std::max(0.0, bonus - 40 * dt);
        int move = (held(keys, {"ArrowRight", "d"}) ? 1 : 0) - (held(keys, {"ArrowLeft", "a"}) ? 1 : 0);
        bool up = held(keys, {"ArrowUp", "w"}), down = held(keys, {"ArrowDown", "s"});
        p.hammer = std::max(0.0, p.hammer - dt);
        bool armed = p.hammer > 0;
        if (armed) {  // the chop: raise, slam down in front, recoil; the slam knocks books off the shelf
            double before = pmod(chop, 1.0);
            chop += dt * 2.3;
            double now_ = pmod(chop, 1.0);
            if ((before < 0.68 && 0.68 <= now_) || (before > now_ && before < 0.68)) {
                double hx = p.x + p.face * 1.4;
                knock(p.level, hx, 0.75, 1.2);
                for (int i = 0; i < 5; i++) {
                    double a = PI * (0.15 + 0.7 * i / 4);
                    sparks.push_back({hx, p.y - 0.3, std::cos(a) * 3, 2 + std::sin(a) * 2, 0.3});
                }
            }
        } else {
            chop = 0.0;
        }
        if (p.mode == "ground") {
            p.x = std::max(-HW, std::min(HW, p.x + move * (armed ? 5.2 : 7) * dt));
            if (move) {
                p.face = move;
                p.run += dt * 12;
            }
            if (!armed) {  // a hammer needs both hands: no ladders while swinging it
                std::optional<double> lx;
                if (up && (lx = ladder_at(p.level, p.x))) {
                    p.mode = "climb";
                    p.lad = p.level;
                    p.x = *lx;
                } else if (down && p.level > 0 && (lx = ladder_at(p.level - 1, p.x))) {
                    p.mode = "climb";
                    p.lad = p.level - 1;
                    p.x = *lx;
                }
            }
        } else if (p.mode == "climb") {
            int g = p.lad;
            p.y += ((up ? 1 : 0) - (down ? 1 : 0)) * 5.0 * dt;
            p.run += std::abs((up ? 1 : 0) - (down ? 1 : 0)) * dt * 10;
            if (p.y >= ly[g + 1]) {
                p.y = ly[g + 1];
                p.level = g + 1;
                p.mode = "ground";
            } else if (p.y <= ly[g]) {
                p.y = ly[g];
                p.level = g;
                p.mode = "ground";
            }
        } else {  // airborne
            p.x = std::max(-HW, std::min(HW, p.x + move * 5 * dt));
            p.vy -= 28 * dt;
            p.y += p.vy * dt;
            if (p.y <= ly[p.level] && p.vy < 0) {
                p.y = ly[p.level];
                p.vy = 0.0;
                p.mode = "ground";
            }
        }
        for (auto& h : hammers) {  // hammer pickups
            if (!h.taken && h.level == p.level && std::fabs(h.x - p.x) < 1.0 && p.mode == "ground") {
                h.taken = true;
                p.hammer = HAMMER_TIME;
                score += 100;
            }
        }
        // Kong throws barrels
        spawn_t -= dt;
        throw_ = std::max(0.0, throw_ - dt);
        if (spawn_t <= 0) {
            spawn_t = std::max(0.9, lay->spawn * std::pow(0.85, cycle)) + grand().uniform(0, 0.8);
            throw_ = 0.5;
            barrels.push_back({-9.0, ly.back() + 0.5, N - 1, 1, "roll", 0.0, 0.0, false, {}, 0.0, 0.0, 0.0});
        }
        double spd = speed();
        for (auto& b : barrels) {
            if (b.mode == "roll") {
                b.x += b.dir * spd * dt;
                b.roll -= b.dir * spd * dt / 0.5;  // rolls without slipping (radius 0.5)
                for (int g = b.level - 1; g < b.level; g++) {
                    std::optional<double> lx;
                    if (g >= 0) lx = ladder_at(g, b.x, 0.25);
                    if (lx && !b.ladder_checked.count({g, *lx})) {
                        b.ladder_checked.insert({g, *lx});
                        if (grand().random() < 0.3) {
                            b.mode = "drop";
                            b.x = *lx;
                            b.vy = 0.0;
                        }
                    }
                }
                if (std::fabs(b.x) > HW + 0.6) {
                    b.mode = "fall";
                    b.vy = 0.0;
                }
            } else {
                b.vy -= 30 * dt;
                b.y += b.vy * dt;
                b.roll -= b.dir * (b.mode == "drop" ? 3.0 : 7.0) * dt;  // keeps tumbling in the air
                if (b.mode == "fall") b.x += b.dir * spd * 0.35 * dt;  // off the end of a shelf: carries on outward as it drops
                if (b.level > 0 && b.y <= ly[b.level - 1] + 0.5) {
                    b.level--;
                    b.hv = std::min(6.0, -b.vy * 0.3);
                    b.y = ly[b.level] + 0.5;
                    b.vy = 0.0;
                    knock(b.level, b.x, 0.9, 0.9);
                    if (b.mode == "fall") {
                        b.dir *= -1;
                        b.x = std::max(-HW + 0.2, std::min(HW - 0.2, b.x));
                    } else {
                        b.dir = grand().choice(std::vector<int>{-1, 1});
                    }
                    b.mode = "roll";
                }
            }
        }
        barrels.erase(std::remove_if(barrels.begin(), barrels.end(), [](const Barrel& b) { return b.level == 0 && std::fabs(b.x) > HW + 1.5; }), barrels.end());
        // fireballs: wander the girders and climb toward the player
        fire_t -= dt;
        if (lay->fire && fire_t <= 0 && (int)fires.size() < lay->fire + cycle) {
            fire_t = 7.0;
            fires.push_back({12.5, ly[0], 0, -1, "walk", {0, 0}, 1.0, 0.0});
        }
        for (auto& f : fires) {
            f.spin += dt * 5;
            f.think -= dt;
            if (f.mode == "walk") {
                f.x += f.dir * (2.6 + 0.5 * cycle) * dt;
                if (std::fabs(f.x) > HW) {
                    f.x = std::max(-HW, std::min(HW, f.x));
                    f.dir *= -1;
                }
                if (f.think <= 0) {
                    f.think = 1.2;
                    int want = p.level;
                    std::optional<double> lx;
                    if (want > f.level && (lx = ladder_at(f.level, f.x, 0.5))) {
                        f.mode = "climb";
                        f.lad = {f.level, 1};
                        f.x = *lx;
                    } else if (want < f.level && f.level > 0 && (lx = ladder_at(f.level - 1, f.x, 0.5))) {
                        f.mode = "climb";
                        f.lad = {f.level - 1, -1};
                        f.x = *lx;
                    } else if (grand().random() < 0.3) {
                        f.dir *= -1;
                    } else {
                        f.dir = p.x > f.x ? 1 : -1;
                    }
                }
            } else {
                auto [g, d] = f.lad;
                f.y += d * 2.6 * dt;
                if (d > 0 && f.y >= ly[g + 1]) {
                    f.y = ly[g + 1];
                    f.level = g + 1;
                    f.mode = "walk";
                } else if (d < 0 && f.y <= ly[g]) {
                    f.y = ly[g];
                    f.level = g;
                    f.mode = "walk";
                }
            }
        }
        // collisions: the hammer smashes whatever is in front of you, anything else hurts
        armed = p.hammer > 0;
        const double reach = 2.1;
        for (size_t i = 0; i < barrels.size();) {
            Barrel& b = barrels[i];
            double dx = b.x - p.x;
            if (armed && std::fabs(b.y - (p.y + 0.5)) < 1.5 && ((dx * p.face > -0.4 && std::fabs(dx) < reach) || std::fabs(dx) < 0.8)) {
                double bx = b.x, by = b.y;
                barrels.erase(barrels.begin() + (long)i);
                smash(bx, by, 300);
                continue;
            }
            if (std::fabs(dx) < 0.9 && std::fabs(b.y - (p.y + 0.95)) < 1.1 && p.mode != "climb") {
                lose_life();
                return;
            }
            if (std::fabs(dx) < 0.8 && p.mode == "air" && p.y > b.y + 0.2 && b.level == p.level && !b.scored) {
                b.scored = true;
                score += 100;
            }
            i++;
        }
        for (size_t i = 0; i < fires.size();) {
            Fire& f = fires[i];
            double dx = f.x - p.x;
            if (armed && std::fabs(f.y - p.y) < 1.6 && std::fabs(dx) < reach) {
                double fx = f.x, fy = f.y;
                fires.erase(fires.begin() + (long)i);
                smash(fx, fy + 0.6, 500);
                continue;
            }
            if (std::fabs(dx) < 0.8 && std::fabs(f.y + 0.5 - (p.y + 0.9)) < 1.2 && p.mode != "climb") {
                lose_life();
                return;
            }
            i++;
        }
        if (p.level == N - 1 && p.x > 10.5 && p.mode == "ground") {
            score += 1000 + (int)bonus;
            stage++;
            keep_stage = true;
            load_stage();
            banner = 3.0;
        }
    }
    // a mallet whose handle starts at (x, y) and points along angle `a` (0 = straight up, rotating like rollz)
    static std::vector<Box> hammer_boxes(double x, double y, double a, double sc = 1.0, double glow = 1.2) {
        double dx = -std::sin(a), dy = std::cos(a);
        double L = 1.3 * sc;
        C3 wood{0.55, 0.36, 0.16}, steel{0.82, 0.84, 0.92};
        return {box(x + dx * L / 2, y + dy * L / 2, 0.6, 0.16 * sc, L, 0.16 * sc, wood, 0.3, 0.0, 0.6, 0.0, 0.0, a, 0.0),
                box(x + dx * (L + 0.22 * sc), y + dy * (L + 0.22 * sc), 0.6, 1.0 * sc, 0.55 * sc, 0.6 * sc, steel, glow, 0.0, 0.6, 0.0, 0.0, a, 0.0),
                box(x + dx * (L + 0.22 * sc), y + dy * (L + 0.22 * sc), 0.6, 1.06 * sc, 0.14 * sc, 0.66 * sc, {0.35, 0.35, 0.4}, 0.4, 0.0, 0.6, 0.0, 0.0, a, 0.0)};
    }
    std::vector<Box> build(Renderer& r, TextCache& text, const Theme& th, double t) override {
        std::vector<Box> out;
        auto add = [&](const std::vector<Box>& v) { out.insert(out.end(), v.begin(), v.end()); };
        double top = ly.back() + 4.2;
        out.push_back(box(0, top / 2, -1.5, 2 * HW + 3.6, top, 0.3, th.wood * 0.55, 0.05));
        for (double sx : {-HW - 1.4, HW + 1.4}) out.push_back(box(sx, top / 2, 0, 0.5, top, 2.6, th.wood, 0.15));
        for (double y : ly) out.push_back(box(0, y - 0.15, 0, 2 * HW + 2.4, 0.3, 2.4, th.wood, 0.25));
        for (auto& bk : shelf)  // books standing behind the girders
            if (!bk.gone) out.push_back(box(bk.x, bk.y0 + bk.h / 2, bk.z, bk.w, bk.h, bk.d, th.books[bk.ci % th.books.size()], 0.1, 0.0, 0.5));
        for (auto& bk : falling) add(book_boxes(bk));
        for (auto& [g, xs] : lay->ladders)
            for (double lx : xs) {
                double h = GAP;
                out.push_back(box(lx - 0.45, ly[g] + h / 2, 0.45, 0.12, h, 0.12, {0.9, 0.7, 0.2}, 0.6));
                out.push_back(box(lx + 0.45, ly[g] + h / 2, 0.45, 0.12, h, 0.12, {0.9, 0.7, 0.2}, 0.6));
                for (int k = 1; k < (int)(h / 0.6); k++) out.push_back(box(lx, ly[g] + k * 0.6, 0.45, 0.9, 0.08, 0.1, {0.9, 0.7, 0.2}, 0.5));
            }
        // Kong (top left) and the goal
        double ty = ly.back();
        double arm = throw_ > 0 ? 0.4 + 1.3 * std::sin(PI * (1 - throw_ / 0.5)) : 0.4;  // wind up and throw, not a snap
        add({box(-12, ty + 1.6, 0, 3.0, 3.0, 2.2, {0.45, 0.28, 0.16}, 0.3), box(-12, ty + 3.6, 0.2, 1.8, 1.5, 1.6, {0.55, 0.35, 0.2}, 0.3),
             box(-12, ty + 3.4, 1.05, 1.0, 0.8, 0.3, {0.9, 0.75, 0.55}, 0.3), box(-10.2, ty + 1.8 + arm, 0.2, 0.9, 1.8, 0.9, {0.45, 0.28, 0.16}, 0.3),
             box(-13.8, ty + 1.8, 0.2, 0.9, 1.8, 0.9, {0.45, 0.28, 0.16}, 0.3)});
        add({box(12.2, ty + 0.9, 0, 0.9, 1.6, 0.7, {1.0, 0.4, 0.7}, 0.6), box(12.2, ty + 1.95, 0, 0.7, 0.6, 0.6, {0.96, 0.8, 0.66}, 0.3),
             box(12.2, ty + 2.4 + 0.1 * std::sin(t * 5), 0, 0.6, 0.25, 0.7, GOLD_, 1.2)});
        for (auto& h : hammers) {  // hammer pickups: standing on their handle, rocking gently, with a glow ring under them
            if (!h.taken) {
                double hy = ly[h.level];
                double rock = 0.18 * std::sin(t * 2.5 + h.x);
                double lift = 0.25 + 0.12 * std::sin(t * 4 + h.x);
                add(hammer_boxes(h.x, hy + lift, rock, 1.0, 0.9 + 0.5 * (0.5 + 0.5 * std::sin(t * 5))));
                out.push_back(box(h.x, hy + 0.03, 0.6, 1.4, 0.05, 1.4, GOLD_, 0.8 + 0.6 * (0.5 + 0.5 * std::sin(t * 5)), t * 0.8));
            }
        }
        auto parts = barrel_parts(th.fx, t);
        for (auto& b : barrels) add(roller(b.x, b.y + b.hop, 0.3, 0.0, b.roll, parts));
        for (auto& f : fires) {  // living flames
            double fl = 0.8 + 0.4 * std::sin(t * 14 + f.x);
            add({box(f.x, f.y + 0.55, 0.3, 0.9, 1.0 * fl, 0.9, {1.0, 0.45, 0.05}, 1.6), box(f.x, f.y + 1.1, 0.3, 0.5, 0.8 * fl, 0.5, {1.0, 0.9, 0.2}, 1.9, f.spin)});
        }
        if (p.hammer > 0) {  // the chop pivots about the hands: raise over the head, slam down in front, small recoil
            double u = pmod(chop, 1.0);
            double up_a = -15 * PI / 180, down_a = 125 * PI / 180, phi;
            if (u < 0.55) {
                double k = 1 - std::pow(1 - u / 0.55, 2);
                phi = down_a + (up_a - down_a) * k;
            } else if (u < 0.68) {
                double k = std::pow((u - 0.55) / 0.13, 2);
                phi = up_a + (down_a - up_a) * k;
            } else {
                phi = down_a - 10 * PI / 180 * std::sin(PI * (u - 0.68) / 0.32);
            }
            bool blink = p.hammer < 2.0 && ((long long)(t * 8)) % 2 == 0;
            add(voxel_person(th, p.x, p.y, 0.5, p.face, std::sin(p.run) * 0.35, phi < 60 * PI / 180));
            double hx = p.x + p.face * 0.35, hy = p.y + 1.45;
            add(hammer_boxes(hx, hy, -p.face * phi, 1.0, blink ? 1.9 : 1.2));
        } else {
            add(voxel_person(th, p.x, p.y, 0.5, p.face, std::sin(p.run) * 0.35, p.mode == "climb" || p.mode == "air"));
        }
        for (auto& sp : sparks) out.push_back(box(sp.x, sp.y + 0.5, 0.8, 0.28, 0.28, 0.28, {1.0, 0.85, 0.2}, 1.8));
        return out;
    }
    void hud(double W, double H) override {
        const Theme& th = app.theme();
        app.txt("SCORE " + std::to_string(score) + "    LIVES " + std::to_string(std::max(lives, 0)) + "    STAGE " + std::to_string(stage) + ": " + lay->name +
                    "    BONUS " + std::to_string((int)bonus),
                W / 2, 92, 16, th.accent, true);
        if (p.hammer > 0) {
            double frac = p.hammer / HAMMER_TIME;
            app.renderer().rect(W / 2 - 110, 120, 220, 12, C4(0, 0, 0, 0.6));
            app.renderer().rect(W / 2 - 108, 122, 216 * frac, 8, C4(1.0, 0.85, 0.2, 1.0));
            app.txt("HAMMER!  swing into barrels and fireballs", W / 2, 136, 12, th.dim, true);
        }
        if (banner > 0 && !over) {
            app.txt("STAGE " + std::to_string(stage), W / 2, H * 0.30, 40, th.logo, true);
            app.txt(lay->name + (cycle ? "   (lap " + std::to_string(cycle + 1) + ": faster)" : ""), W / 2, H * 0.30 + 52, 22, th.accent, true);
            std::string hint = stage == 1 ? "grab the hammer to smash barrels" : (lay->fire ? "watch out for fireballs" : "the barrels are faster now");
            app.txt(hint, W / 2, H * 0.30 + 84, 14, th.dim, true);
        }
        if (over) {
            app.txt("GAME OVER  -  press R", W / 2, H * 0.32, 34, {1, 0.3, 0.3}, true);
            app.txt("You reached stage " + std::to_string(stage) + " with " + std::to_string(score) + " points", W / 2, H * 0.32 + 46, 16, th.text, true);
        }
    }
};

// ============================================================ TRIVIA
struct Q {
    std::string q, correct;
    std::vector<std::string> wrong;
};
const std::vector<Q>& bank() {
    static const std::vector<Q> B = {
        {"Who is the main protagonist of Naruto?", "Naruto Uzumaki", {"Sasuke Uchiha", "Kakashi Hatake", "Rock Lee"}},
        {"What is Goku's birth name as a Saiyan?", "Kakarot", {"Vegeta", "Raditz", "Bardock"}},
        {"Which studio made Spirited Away?", "Studio Ghibli", {"Madhouse", "Kyoto Animation", "Bones"}},
        {"What is Luffy's pirate crew called?", "Straw Hat Pirates", {"Red Hair Pirates", "Heart Pirates", "Whitebeard Pirates"}},
        {"What is the name of Light Yagami's Shinigami in Death Note?", "Ryuk", {"Rem", "L", "Gelus"}},
        {"Who wrote the manga Fullmetal Alchemist?", "Hiromu Arakawa", {"Eiichiro Oda", "Masashi Kishimoto", "Tite Kubo"}},
        {"What is the Survey Corps fighting in Attack on Titan?", "Titans", {"Demons", "Hollows", "Curses"}},
        {"In Cowboy Bebop, what is the name of the dog?", "Ein", {"Pochi", "Max", "Cosmo"}},
        {"What Devil Fruit did Luffy eat?", "Gomu Gomu no Mi", {"Mera Mera no Mi", "Hito Hito no Mi", "Yami Yami no Mi"}},
        {"What is Tanjiro's sister called in Demon Slayer?", "Nezuko", {"Kanao", "Shinobu", "Mitsuri"}},
        {"Which anime has the Hunter Exam?", "Hunter x Hunter", {"Fairy Tail", "Black Clover", "Bleach"}},
        {"Who is the author of One Piece?", "Eiichiro Oda", {"Akira Toriyama", "Hajime Isayama", "Yoshihiro Togashi"}},
        {"What is Izuku Midoriya's hero name in My Hero Academia?", "Deku", {"Dynamight", "Shoto", "Ingenium"}},
        {"Which technique is Naruto famous for?", "Rasengan", {"Chidori", "Kamehameha", "Getsuga Tensho"}},
        {"Who is the vessel of Sukuna in Jujutsu Kaisen?", "Yuji Itadori", {"Megumi Fushiguro", "Satoru Gojo", "Nobara Kugisaki"}},
        {"Who directed My Neighbor Totoro?", "Hayao Miyazaki", {"Makoto Shinkai", "Mamoru Hosoda", "Satoshi Kon"}},
        {"What is the principle at the heart of alchemy in Fullmetal Alchemist?", "Equivalent exchange",
         {"Conservation of soul", "Elemental balance", "Transmutation of life"}},
        {"Who is the protagonist of Cowboy Bebop?", "Spike Spiegel", {"Jet Black", "Vash the Stampede", "Edward Wong"}},
        {"What does Okabe Rintaro call himself in Steins;Gate?", "Hououin Kyouma", {"Okarin Prime", "Mad Chronicler", "Time Lord Zero"}},
        {"What are Soul Reapers' swords called in Bleach?", "Zanpakuto", {"Shikai", "Bankai", "Nodachi"}},
        {"Which anime is about high school volleyball?", "Haikyu!!", {"Kuroko's Basketball", "Slam Dunk", "Yowamushi Pedal"}},
        {"What race is Frieren?", "Elf", {"Human", "Dwarf", "Demon"}},
        {"Who directed Your Name?", "Makoto Shinkai", {"Hayao Miyazaki", "Isao Takahata", "Mamoru Oshii"}},
        {"What is Kirito's real first name in Sword Art Online?", "Kazuto", {"Kirigaya", "Akihiko", "Eiji"}},
        {"Which is NOT one of the three walls in Attack on Titan?", "Shiganshina", {"Maria", "Rose", "Sina"}},
        {"What breathing style does Zenitsu use?", "Thunder Breathing", {"Water Breathing", "Flame Breathing", "Wind Breathing"}},
        {"Who is the Fourth Hokage?", "Minato Namikaze", {"Hiruzen Sarutobi", "Tobirama Senju", "Jiraiya"}},
        {"Who is the swordsman of the Straw Hat crew?", "Roronoa Zoro", {"Sanji", "Usopp", "Franky"}},
        {"What is the name of Goku's first son?", "Gohan", {"Goten", "Trunks", "Pan"}},
        {"What is Chihiro renamed in Spirited Away?", "Sen", {"Yubaba", "Haku", "Lin"}},
        {"What type is Charmander in Pokemon?", "Fire", {"Water", "Grass", "Dragon"}},
        {"Who is the Flame Alchemist?", "Roy Mustang", {"Edward Elric", "Alex Armstrong", "Scar"}},
        {"What position does Hinata play in Haikyu!!?", "Middle blocker", {"Setter", "Libero", "Wing spiker"}},
        {"What is Gojo's famous technique called?", "Limitless", {"Ten Shadows", "Cursed Speech", "Boogie Woogie"}},
        {"Which year did Astro Boy first air on TV?", "1963", {"1975", "1952", "1989"}},
        {"Which anime features the mecha 'EVA' units?", "Neon Genesis Evangelion", {"Mobile Suit Gundam", "Code Geass", "Gurren Lagann"}},
        {"What does 'senpai' usually mean?", "Upperclassman / senior", {"Teacher", "Younger sibling", "Rival"}},
        {"Who is Saitama's disciple in One Punch Man?", "Genos", {"Mumen Rider", "King", "Tatsumaki"}},
        {"In Death Note, what is L's real first name?", "L Lawliet", {"Light Yagami", "Near", "Mello"}},
        {"Which anime is set on the floating castle-airship world of 'Laputa'?", "Castle in the Sky", {"Howl's Moving Castle", "Princess Mononoke", "Ponyo"}},
    };
    return B;
}

class Trivia : public Game {
public:
    explicit Trivia(GameHost& a) : Game(a) {
        name = "ANIME TRIVIA";
        help = "press 1-4 or click an answer   R restart   Esc exit";
    }
    static constexpr int ROUNDS = 10;
    static constexpr double TIME = 15.0;
    std::vector<Q> queue;
    int round = 0, streak = 0, best = 0;
    struct Cur {
        std::string q, answer;
        std::vector<std::string> opts;
    };
    std::optional<Cur> cur;
    struct Reveal {
        int i;
        bool ok;
        double t;
    };
    std::optional<Reveal> reveal;
    double t0 = 0;
    std::string source;
    std::shared_ptr<int> alive_ = std::make_shared<int>(0);
    CamGoal cam() const override { return {{0, 6.5, 0}, 0.0, 10 * PI / 180, 25}; }
    void reset() override {
        std::vector<Q> pool = bank();
        shuffle(pool);
        queue.assign(pool.begin(), pool.begin() + std::min(pool.size(), (size_t)ROUNDS + 2));
        round = score = streak = best = 0;
        over = false;
        cur.reset();
        reveal.reset();
        t0 = app.now();
        source = "bundled questions";
        std::weak_ptr<int> alive = alive_;
        app.tasks().run(  // fresh questions from OpenTDB if reachable
            [] {
                http::Req r;
                r.url = "https://opentdb.com/api.php";
                r.params = {{"amount", "12"}, {"category", "31"}, {"type", "multiple"}};
                r.timeout = 10;
                return json::parse(http::request(r).body);
            },
            [this, alive](const json& res, const std::string& err) {
                if (alive.expired() || !err.empty() || !app.is_current_game(this)) return;
                std::vector<Q> qs;
                if (res.contains("results"))
                    for (auto& q : res["results"]) {
                        Q x{su::html_unescape(q.value("question", "")), su::html_unescape(q.value("correct_answer", "")), {}};
                        for (auto& w : q["incorrect_answers"]) x.wrong.push_back(su::html_unescape(w.get<std::string>()));
                        qs.push_back(x);
                    }
                if (qs.empty()) return;
                std::vector<Q> rest(queue.begin() + (queue.empty() ? 0 : 1), queue.end());
                rest.insert(rest.end(), qs.begin(), qs.end());
                shuffle(rest);
                std::vector<Q> nq;
                if (!queue.empty()) nq.push_back(queue[0]);  // the question already on screen stays
                nq.insert(nq.end(), rest.begin(), rest.end());
                queue = nq;
                source = "fresh questions (OpenTDB) + bundled";
            });
        next_round();
    }
    void next_round() {
        if (round >= ROUNDS || queue.empty()) {
            over = true;
            cur.reset();
            return;
        }
        Q q = queue.front();
        queue.erase(queue.begin());
        std::vector<std::string> opts(q.wrong.begin(), q.wrong.begin() + std::min<size_t>(3, q.wrong.size()));
        opts.push_back(q.correct);
        shuffle(opts);
        cur = Cur{q.q, q.correct, opts};
        round++;
        t0 = app.now();
        reveal.reset();
    }
    void pick(int i) {
        if (over || !cur || reveal || i >= (int)cur->opts.size()) return;
        bool ok = cur->opts[i] == cur->answer;
        if (ok) {
            double left = std::max(0.0, TIME - (app.now() - t0));
            streak++;
            best = std::max(best, streak);
            score += 50 + (int)(100 * left / TIME) + 10 * std::min(streak - 1, 5);
        } else {
            streak = 0;
        }
        reveal = Reveal{i, ok, app.now()};
    }
    void key(const std::string& k) override {
        if (k == "r") reset();
        else if (k.size() == 1 && k[0] >= '1' && k[0] <= '4') pick(k[0] - '1');
    }
    std::vector<std::string> achievements() override { return (over && best >= 7) ? std::vector<std::string>{"trivia"} : std::vector<std::string>{}; }
    void update(double, const Keys&) override {
        if (cur && !reveal && app.now() - t0 > TIME) {
            streak = 0;
            reveal = Reveal{-1, false, app.now()};  // out of time
        }
        if (reveal && app.now() - reveal->t > 1.9) next_round();
    }
    static void wall_text(Renderer& r, TextCache& text, const std::string& s, double y, double height, C3 color) {
        if (s.empty()) return;
        TextTex tt = text.get(s, 44, color);
        double ww = std::min(height * tt.w / tt.h, 23.5);
        double hh = ww * tt.h / tt.w;
        r.quad3d(tt.tex, mx::translate(-ww / 2, y + hh / 2, 0.36) * mx::scale(ww, -hh, 1));
    }
    std::vector<Box> build(Renderer& r, TextCache& text, const Theme& th, double t) override {
        std::vector<Box> out = {box(0, -0.2, 2, 30, 0.4, 12, th.slab, 0.1)};
        double glow = reveal ? 1.0 : 0.2;
        C3 col = (reveal && reveal->ok) ? th.ep_seen : (reveal ? C3(1.0, 0.25, 0.2) : th.wire);
        out.push_back(box(0, 7.0, 0, 25, 9.5, 0.6, th.slab, 0.15));
        const double fr[4][4] = {{0, 11.9, 25.4, 0.3}, {0, 2.1, 25.4, 0.3}, {-12.7, 7.0, 0.3, 9.8}, {12.7, 7.0, 0.3, 9.8}};
        for (auto& f : fr) out.push_back(box(f[0], f[1], 0.1, f[2], f[3], 0.8, col, glow));
        if (over) {
            wall_text(r, text, "TRIVIA COMPLETE", 8.8, 1.6, th.t_field);
            wall_text(r, text, "SCORE " + std::to_string(score), 8.8 - 1.9, 1.3, th.accent);
            wall_text(r, text, "BEST STREAK " + std::to_string(best) + "   -   press R to play again", 8.8 - 3.8, 0.8, th.t_ped);
            return out;
        }
        if (!cur) return out;
        double frac = !reveal ? std::max(0.0, 1 - (t - t0) / TIME) : 0.0;
        out.push_back(box(-12.5 + 25 * frac / 2, 12.6, 0.2, std::max(25 * frac, 0.01), 0.35, 0.4, frac > 0.35 ? th.ep_seen : C3(1.0, 0.3, 0.2), 1.0));
        std::vector<std::string> lines;
        std::string curl;
        for (auto& w : su::split_ws(cur->q)) {
            if (su::utf8_len(curl) + su::utf8_len(w) > 36 && !curl.empty()) {
                lines.push_back(curl);
                curl = w;
            } else {
                curl = su::strip(curl + " " + w);
            }
        }
        lines.push_back(curl);
        double y = 9.4 + (lines.size() - 1) * 0.55;
        for (auto& ln : lines) {
            wall_text(r, text, ln, y, 1.05, {1, 1, 1});
            y -= 1.35;
        }
        wall_text(r, text, "QUESTION " + std::to_string(round) + "/" + std::to_string(ROUNDS) + "     SCORE " + std::to_string(score) + "     STREAK " + std::to_string(streak), 3.0,
                  0.7, th.t_field);
        if (reveal) {
            std::string msg = reveal->ok ? "CORRECT!" : (reveal->i < 0 ? "TIME'S UP!" : "WRONG!");
            wall_text(r, text, msg + "   " + cur->answer, 5.0, 1.1, reveal->ok ? C3(0.4, 1, 0.5) : C3(1, 0.45, 0.35));
        }
        return out;
    }
    void hud(double W, double H) override {
        if (over || !cur) return;
        app.txt("questions: " + source, W / 2, 92, 12, app.theme().dim, true);
        double bw = std::min(360.0, (W - 80) / 2);
        for (int i = 0; i < (int)cur->opts.size(); i++) {
            double x = W / 2 - bw - 6 + (i % 2) * (bw + 12);
            double y = H - 244 + (i / 2) * 46;
            bool state = reveal && cur->opts[i] == cur->answer;
            app.button(std::to_string(i + 1) + "  " + su::utf8_substr(cur->opts[i], 0, 34), x, y, bw, 40, [this, i] { pick(i); }, state);
        }
    }
};
}  // namespace

std::vector<std::string> game_keys() { return {"snake", "breakout", "quiz", "kong", "trivia", "maze", "galaga"}; }

std::unique_ptr<Game> make_game(const std::string& key, GameHost& app) {
    std::unique_ptr<Game> g;
    if (key == "snake") g = std::make_unique<Snake>(app);
    else if (key == "breakout") g = std::make_unique<Breakout>(app);
    else if (key == "quiz") g = std::make_unique<Quiz>(app);
    else if (key == "kong") g = std::make_unique<KongClimb>(app);
    else if (key == "trivia") g = std::make_unique<Trivia>(app);
    else if (key == "maze") g = make_maze(app);
    else if (key == "galaga") g = make_galaga(app);
    if (g) g->reset();  // Game.__init__ calls reset() in the Python version
    return g;
}
