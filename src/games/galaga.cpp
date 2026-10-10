#include "galaga.hpp"

#include <algorithm>
#include <cmath>
#include <set>

#include "../gfx/renderer.hpp"
#include "../gfx/text.hpp"
#include "../util/strutil.hpp"
#include "../world/dinos.hpp"
#include "../world/themes.hpp"

namespace {
const double X = 11.0, Z = 9.5;  // half extents of the playfield

struct Ship {
    const char *kind, *name, *blurb;
    double speed, rate;
    int hp;
    const char* shot;
    double scale;
};
const Ship SHIPS[4] = {
    {"trex", "REX", "twin cannons, strong hull", 13.0, 0.20, 4, "twin", 0.34},
    {"raptor", "RAPTOR", "fastest, rapid fire", 19.0, 0.11, 3, "single", 0.55},
    {"trike", "TRICERATOPS", "triple horn spread, tanky", 10.0, 0.26, 6, "spread", 0.38},
    {"ptero", "PTERANODON", "piercing shots", 15.0, 0.24, 3, "pierce", 0.42},
};

// the herd's rigs reused as a ship: the dinosaur nose points up the screen (-z)
std::vector<Box> dino_boxes(const std::string& kind, double x, double y, double z, double t, double scale, double heading = PI / 2, double mv = 1.0, double roar = 0.0) {
    auto parts = dino_pose(kind, t, t * 9, mv, roar, 14.0);
    double c = std::cos(heading), s = std::sin(heading);
    std::vector<Box> out;
    for (auto& p : parts)
        out.push_back(box(x + (p.x * c + p.z * s) * scale, y + p.y * scale, z + (-p.x * s + p.z * c) * scale, p.sx * scale, p.sy * scale, p.sz * scale, p.col,
                          p.glow + 0.2, heading + p.yl, 0.5, 0.0, 0.0, p.rz, p.rx));
    return out;
}

struct Boss {
    const char *name, *anime;
    C3 skin, hair, outfit, accent;
    const char *attack, *style;
};
const Boss BOSSES[6] = {
    {"GOKU", "Dragon Ball", {0.96, 0.8, 0.65}, {0.05, 0.05, 0.08}, {1.0, 0.5, 0.05}, {0.1, 0.3, 0.9}, "beam", "spiky"},
    {"LUFFY", "One Piece", {0.96, 0.8, 0.65}, {0.05, 0.05, 0.08}, {0.85, 0.1, 0.1}, {0.95, 0.8, 0.3}, "pistol", "hat"},
    {"NARUTO", "Naruto", {0.96, 0.8, 0.65}, {1.0, 0.85, 0.2}, {1.0, 0.55, 0.1}, {0.1, 0.2, 0.7}, "clones", "spiky"},
    {"SAITAMA", "One Punch Man", {0.96, 0.8, 0.65}, {0.96, 0.8, 0.65}, {1.0, 0.9, 0.1}, {1.0, 1.0, 1.0}, "punch", "bald"},
    {"RYUK", "Death Note", {0.9, 0.9, 0.95}, {0.05, 0.05, 0.1}, {0.1, 0.1, 0.14}, {0.9, 0.1, 0.1}, "apples", "wild"},
    {"LELOUCH", "Code Geass", {0.96, 0.85, 0.75}, {0.05, 0.03, 0.1}, {0.35, 0.1, 0.5}, {0.95, 0.15, 0.55}, "geass", "side"},
};
const C3 ENEMY_COLORS[4] = {{0.2, 0.9, 0.5}, {0.9, 0.3, 0.7}, {0.3, 0.6, 1.0}, {1.0, 0.7, 0.2}};

// a blocky character standing on the field facing the player (+z)
std::vector<Box> figure(double x, double z, double s, const Boss& b, double t, double glow = 0.3) {
    double y = 0.12 * std::sin(t * 3);
    std::vector<Box> out = {box(x - 0.45 * s, y + 0.8 * s, z, 0.6 * s, 1.6 * s, 0.7 * s, b.accent, glow), box(x + 0.45 * s, y + 0.8 * s, z, 0.6 * s, 1.6 * s, 0.7 * s, b.accent, glow),
                            box(x, y + 2.3 * s, z, 1.7 * s, 1.5 * s, 0.9 * s, b.outfit, glow + 0.2), box(x - 1.15 * s, y + 2.4 * s, z, 0.5 * s, 1.4 * s, 0.5 * s, b.skin, glow),
                            box(x + 1.15 * s, y + 2.4 * s, z, 0.5 * s, 1.4 * s, 0.5 * s, b.skin, glow), box(x, y + 3.65 * s, z, 1.2 * s, 1.1 * s, 1.0 * s, b.skin, glow + 0.1),
                            box(x - 0.28 * s, y + 3.7 * s, z + 0.52 * s, 0.22 * s, 0.22 * s, 0.1 * s, {0.05, 0.05, 0.05}, 0.0),
                            box(x + 0.28 * s, y + 3.7 * s, z + 0.52 * s, 0.22 * s, 0.22 * s, 0.1 * s, {0.05, 0.05, 0.05}, 0.0)};
    std::string style = b.style;
    if (style == "spiky") {
        for (int k : {-1, 0, 1}) out.push_back(box(x + k * 0.4 * s, y + 4.5 * s + (k == 0 ? 0.3 * s : 0), z, 0.4 * s, 0.9 * s, 0.5 * s, b.hair, glow + 0.3, 0.0, 1.0, 0.0, 0.0, k * 0.4));
    } else if (style == "hat") {
        out.push_back(box(x, y + 4.35 * s, z, 2.0 * s, 0.18 * s, 2.0 * s, {0.95, 0.85, 0.4}, glow + 0.2));
        out.push_back(box(x, y + 4.65 * s, z, 1.2 * s, 0.6 * s, 1.2 * s, {0.95, 0.85, 0.4}, glow + 0.2));
        out.push_back(box(x, y + 4.5 * s, z + 0.1 * s, 1.25 * s, 0.14 * s, 1.25 * s, {0.85, 0.1, 0.1}, glow));
    } else if (style == "wild") {
        for (int k : {-2, -1, 0, 1, 2}) out.push_back(box(x + k * 0.3 * s, y + 4.4 * s, z, 0.3 * s, 0.7 * s, 0.4 * s, b.hair, glow, 0.0, 1.0, 0.0, 0.0, k * 0.35));
    } else if (style == "side") {
        out.push_back(box(x, y + 4.35 * s, z, 1.3 * s, 0.4 * s, 1.1 * s, b.hair, glow));
        out.push_back(box(x - 0.7 * s, y + 3.9 * s, z, 0.25 * s, 0.9 * s, 0.9 * s, b.hair, glow));
    } else if (style == "bald") {
        out.push_back(box(x, y + 4.0 * s, z + 0.3 * s, 0.3 * s, 0.25 * s, 0.1 * s, {1, 1, 1}, 0.6));  // a shine on the head
        out.push_back(box(x, y + 3.2 * s, z - 0.65 * s, 2.0 * s, 2.4 * s, 0.2 * s, {1, 1, 1}, glow));  // the cape
    }
    return out;
}

class Galaga : public Game {
public:
    explicit Galaga(GameHost& a) : Game(a) {
        name = "DINO GALAGA";
        help = "A/D or arrows move   Space fire   B roar bomb   R restart   Esc exit";
    }
    CamGoal cam() const override { return {{0, 0, 0.5}, 0.0, 64 * PI / 180, 33}; }
    std::string stage = "select";  // select | play | over
    int sel = 0, wave = 0, bosses_down = 0, hp = 3, max_hp = 3, bombs = 1;
    const Ship* ship = &SHIPS[0];
    double px = 0, cool = 0, rapid = 0, shield = 0, invuln = 0, reverse = 0;
    int next_id = 1;
    struct Bullet {
        double x, z, vx, vz;
        bool pierce;
        std::set<int> hit;
    };
    std::vector<Bullet> bullets;
    struct EBullet {
        double x, z, vx, vz;
        bool big;
        std::string tag;
    };
    std::vector<EBullet> ebullets;
    struct Enemy {
        int id;
        double fx, fz, x, z;
        std::string mode;
        double u;
        int kind, hp;
        double dive_t, sx, shoot;
    };
    std::vector<Enemy> enemies;
    struct Drop {
        double x, z;
        std::string kind;
    };
    std::vector<Drop> drops;
    struct Spark {
        double x, z, vx, vz, life;
        C3 col;
    };
    std::vector<Spark> sparks;
    struct Beam {
        double x, w, t_on, life;
    };
    std::vector<Beam> beams;
    struct Clone {
        double x, z;
        int dir, hp;
        double shoot;
    };
    struct BossState {
        int id;
        const Boss* def;
        double x, z;
        int hp, max, dir;
        double t, atk, charge;
        bool enter;
        std::vector<Clone> clones;
    };
    std::optional<BossState> boss;
    std::pair<std::string, double> msg{"", 0.0};

    void reset() override {
        stage = "select";
        sel = 0;
        score = 0;
        wave = 0;
        over = false;
        bullets.clear();
        ebullets.clear();
        enemies.clear();
        drops.clear();
        sparks.clear();
        beams.clear();
        boss.reset();
        bosses_down = 0;
        msg = {"", 0.0};
    }
    std::vector<std::string> achievements() override {
        std::vector<std::string> out;
        if (bosses_down) out.push_back("galaga_boss");
        if (wave >= 3 && stage == "play") out.push_back("galaga_w3");
        return out;
    }
    void start(int i) {
        ship = &SHIPS[i];
        px = 0.0;
        hp = max_hp = ship->hp;
        cool = rapid = shield = 0.0;
        bombs = 1;
        invuln = 1.5;
        reverse = 0.0;
        stage = "play";
        wave = 0;
        next_wave();
    }
    void next_wave() {
        wave++;
        bullets.clear();
        ebullets.clear();
        beams.clear();
        bombs = std::min(bombs + 1, 3);
        int cols = 8, rows = 3 + std::min(wave / 2, 2);
        enemies.clear();
        for (int r_ = 0; r_ < rows; r_++)
            for (int c = 0; c < cols; c++) {
                Enemy e;
                e.id = next_id++;
                e.fx = (c - (cols - 1) / 2.0) * 2.2;
                e.fz = -Z + 1.6 + r_ * 1.7;
                e.x = grand().uniform(-X, X);
                e.z = -Z - 3 - r_ * 2 - c * 0.5;
                e.mode = "enter";
                e.u = 0.0;
                e.kind = r_ % 4;
                e.hp = 1 + ((r_ == 0 && wave > 3) ? 1 : 0);
                e.dive_t = grand().uniform(2, 9);
                e.sx = 0;
                e.shoot = grand().uniform(1, 4);
                enemies.push_back(e);
            }
        boss.reset();
        msg = {"WAVE " + std::to_string(wave), 2.0};
    }
    void spawn_boss() {
        const Boss* b = &BOSSES[(wave - 1) % 6];
        int lap = (wave - 1) / 6;
        int bhp = 34 + 10 * wave + 12 * lap;
        boss = BossState{next_id++, b, 0.0, -Z - 5.0, bhp, bhp, 1, 0.0, 2.5, 0.0, true, {}};
        msg = {std::string("BOSS: ") + b->name + "  (" + b->anime + ")", 2.8};
    }
    void key(const std::string& k) override {
        if (k == "r") {
            reset();
            return;
        }
        if (stage == "select") {
            if (k == "1" || k == "2" || k == "3" || k == "4") start(k[0] - '1');
            else if (k == "ArrowLeft" || k == "a") sel = (int)pmod(sel - 1, 4);
            else if (k == "ArrowRight" || k == "d") sel = (sel + 1) % 4;
            else if (k == "Enter" || k == " ") start(sel);
        } else if (stage == "play" && (k == "b" || k == "Shift") && bombs > 0) {
            bombs--;
            ebullets.clear();
            for (auto& e : enemies) e.hp -= 2;
            if (boss) boss->hp -= 12;
            msg = {"ROOOAR!", 1.0};
            for (int i = 0; i < 24; i++) sparks.push_back({px, Z - 2, std::cos(i * TAU / 24) * 14, std::sin(i * TAU / 24) * 14, 0.7, {1, 0.8, 0.2}});
        }
    }
    void fire() {
        std::string s = ship->shot;
        double z = Z - 1.5;
        bool pierce = s == "pierce";
        std::vector<double> shots = s == "single" ? std::vector<double>{0.0} : s == "twin" ? std::vector<double>{-0.5, 0.5} : s == "spread" ? std::vector<double>{-0.18, 0.0, 0.18} : std::vector<double>{0.0};
        if (rapid > 0 && shots.size() < 3) {
            shots.push_back(-0.9);
            shots.push_back(0.9);
        }
        for (double k : shots) {
            double vx = s == "spread" ? k * 22 : 0.0;
            double ox = (s == "twin" || std::fabs(k) > 0.8) ? k : 0.0;
            bullets.push_back({px + ox, z, vx, -26.0, pierce, {}});
        }
    }
    void hurt() {
        if (invuln > 0) return;
        if (shield > 0) {
            shield = 0.0;
            invuln = 0.8;
            return;
        }
        hp--;
        invuln = 1.6;
        for (int i = 0; i < 14; i++) {
            double vx = grand().uniform(-8, 8), vz = grand().uniform(-9, 3);
            sparks.push_back({px, Z - 1, vx, vz, 0.7, {1, 0.3, 0.2}});
        }
        if (hp <= 0) {
            stage = "over";
            over = true;
        }
    }
    void kill_enemy(const Enemy& e) {
        score += 100 + 20 * e.kind;
        for (int i = 0; i < 8; i++) {
            double a = i * TAU / 8;
            sparks.push_back({e.x, e.z, std::cos(a) * 6, std::sin(a) * 6, 0.5, ENEMY_COLORS[e.kind]});
        }
        if (grand().random() < 0.12) drops.push_back({e.x, e.z, grand().choice(std::vector<std::string>{"rapid", "shield", "heal"})});
    }
    void eshot(double x, double z, double vx, double vz, bool big = false, const std::string& tag = "") { ebullets.push_back({x, z, vx, vz, big, tag}); }

    void update(double dt, const Keys& keys) override {
        double t = app.now();
        msg.second = std::max(0.0, msg.second - dt);
        for (auto& sp : sparks) {
            sp.life -= dt;
            sp.x += sp.vx * dt;
            sp.z += sp.vz * dt;
        }
        sparks.erase(std::remove_if(sparks.begin(), sparks.end(), [](const Spark& s) { return !(s.life > 0); }), sparks.end());
        if (stage != "play") return;
        cool = std::max(0.0, cool - dt);
        rapid = std::max(0.0, rapid - dt);
        shield = std::max(0.0, shield - dt);
        invuln = std::max(0.0, invuln - dt);
        reverse = std::max(0.0, reverse - dt);
        int move = (held(keys, {"ArrowRight", "d"}) ? 1 : 0) - (held(keys, {"ArrowLeft", "a"}) ? 1 : 0);
        if (reverse > 0) move = -move;
        px = std::max(-X + 0.8, std::min(X - 0.8, px + move * ship->speed * dt));
        if (keys.count(" ") && cool <= 0) {
            cool = ship->rate * (rapid > 0 ? 0.5 : 1.0);
            fire();
        }
        for (auto& b : bullets) {  // player bullets
            b.x += b.vx * dt;
            b.z += b.vz * dt;
        }
        bullets.erase(std::remove_if(bullets.begin(), bullets.end(), [](const Bullet& b) { return !(b.z > -Z - 4 && std::fabs(b.x) < X + 3); }), bullets.end());
        for (auto& e : enemies) {
            double sway = std::sin(t * 0.8) * 2.6;
            if (e.mode == "enter") {
                double tx = e.fx + sway, tz = e.fz;
                double dx = tx - e.x, dz = tz - e.z;
                double d = std::hypot(dx, dz);
                if (d == 0) d = 1.0;
                double stepl = std::min(d, 14 * dt);
                e.x += dx / d * stepl;
                e.z += dz / d * stepl;
                if (d < 0.3) e.mode = "form";
            } else if (e.mode == "form") {
                e.x = e.fx + sway;
                e.z = e.fz;
                e.dive_t -= dt;
                if (e.dive_t <= 0 && !boss) {
                    e.mode = "dive";
                    e.u = 0.0;
                    e.sx = e.x;
                }
            } else {  // dive: a looping swoop toward the player's side of the field, then back in from the top
                e.u += dt * 0.55;
                double u = e.u;
                e.x = e.sx + std::sin(u * PI * 2) * 5 * (e.fx > 0 ? 1 : -1) + (px - e.sx) * 0.4 * std::min(u * 2, 1.0);
                e.z = e.fz + (Z + 3 - e.fz) * std::min(u * 1.25, 1.0);
                if (u >= 1.0) {
                    e.mode = "enter";
                    e.x = grand().uniform(-X, X);
                    e.z = -Z - 3;
                    e.dive_t = grand().uniform(5, 12);
                }
            }
            e.shoot -= dt;
            if (e.shoot <= 0 && e.mode != "enter") {
                e.shoot = grand().uniform(2.0, 5.0) / (1 + 0.1 * wave) + (e.mode == "form" ? 1.5 : 0);
                if (std::fabs(e.x) < X) eshot(e.x, e.z + 0.6, e.mode == "dive" ? (px - e.x) * 0.12 : 0.0, 8.5 + 0.4 * wave);
            }
            for (auto& b : bullets) {
                if (std::fabs(b.x - e.x) < 0.9 && std::fabs(b.z - e.z) < 0.9 && !b.hit.count(e.id)) {
                    e.hp--;
                    b.hit.insert(e.id);
                    if (!b.pierce) b.z = -Z - 99;
                }
            }
            if (std::fabs(e.x - px) < 1.0 && std::fabs(e.z - (Z - 1)) < 1.0) {
                e.hp = 0;
                hurt();
            }
        }
        for (auto& e : enemies)
            if (e.hp <= 0) kill_enemy(e);
        enemies.erase(std::remove_if(enemies.begin(), enemies.end(), [](const Enemy& e) { return !(e.hp > 0); }), enemies.end());
        if (enemies.empty() && !boss && stage == "play") spawn_boss();
        if (boss) update_boss(dt);
        for (auto& eb : ebullets) {  // enemy bullets, drops
            eb.x += eb.vx * dt;
            eb.z += eb.vz * dt;
            if (std::fabs(eb.x - px) < (eb.big ? 0.9 : 0.6) && std::fabs(eb.z - (Z - 1)) < (eb.big ? 1.0 : 0.7)) {
                eb.z = Z + 99;
                hurt();
                if (eb.tag == "geass") reverse = 3.0;
            }
        }
        ebullets.erase(std::remove_if(ebullets.begin(), ebullets.end(), [](const EBullet& b) { return !(-Z - 6 < b.z && b.z < Z + 4 && std::fabs(b.x) < X + 6); }), ebullets.end());
        for (auto& d : drops) {
            d.z += 5 * dt;
            if (std::fabs(d.x - px) < 1.3 && std::fabs(d.z - (Z - 1)) < 1.3) {
                d.z = Z + 99;
                if (d.kind == "rapid") rapid = 9.0;
                else if (d.kind == "shield") shield = 12.0;
                else hp = std::min(max_hp, hp + 1);
                score += 50;
            }
        }
        drops.erase(std::remove_if(drops.begin(), drops.end(), [](const Drop& d) { return !(d.z < Z + 3); }), drops.end());
    }

    void update_boss(double dt) {
        BossState& b = *boss;
        std::string kind = b.def->attack;
        b.t += dt;
        if (b.enter) {
            b.z += 5 * dt;
            if (b.z >= -Z + 3.2) b.enter = false;
            return;
        }
        b.x += b.dir * (3.0 + 0.2 * wave) * dt;
        if (std::fabs(b.x) > X - 3) b.dir *= -1;
        for (auto& sh : bullets) {
            if (std::fabs(sh.x - b.x) < 2.6 && std::fabs(sh.z - (b.z - 0.5)) < 3.4 && !sh.hit.count(b.id)) {
                b.hp--;
                score += 10;
                sh.hit.insert(b.id);
                if (!sh.pierce) sh.z = -Z - 99;
            }
        }
        for (auto& c : b.clones) {
            c.x += c.dir * 5 * dt;
            if (std::fabs(c.x) > X - 1) c.dir *= -1;
            c.shoot -= dt;
            if (c.shoot <= 0) {
                c.shoot = 1.4;
                eshot(c.x, c.z + 1, (px - c.x) * 0.2, 9);
            }
            for (auto& sh : bullets) {
                if (std::fabs(sh.x - c.x) < 1.2 && std::fabs(sh.z - c.z) < 1.4) {
                    c.hp--;
                    sh.z = -Z - 99;
                }
            }
        }
        b.clones.erase(std::remove_if(b.clones.begin(), b.clones.end(), [](const Clone& c) { return !(c.hp > 0); }), b.clones.end());
        b.atk -= dt;
        for (auto& bm : beams) {
            bm.life -= dt;
            if (bm.t_on <= 0 && std::fabs(px - bm.x) < bm.w / 2) hurt();
            bm.t_on -= dt;
        }
        beams.erase(std::remove_if(beams.begin(), beams.end(), [](const Beam& bm) { return !(bm.life > 0); }), beams.end());
        if (b.atk <= 0) {
            double lap = std::pow(0.92, std::min(wave, 8));
            if (kind == "beam") {
                b.atk = 3.6 * lap;
                beams.push_back({px, 2.4, 1.0, 1.7});
                msg = {"KA-ME-HA-ME-HA!", 1.0};
            } else if (kind == "pistol") {
                b.atk = 2.2 * lap;
                for (int k : {-1, 0, 1}) eshot(b.x, b.z + 1.5, (px - b.x) * 0.25 + k * 2.5, 13);
                msg = {"GUM-GUM PISTOL!", 0.8};
            } else if (kind == "clones") {
                b.atk = 5.5 * lap;
                if (b.clones.size() < 3) {
                    for (int k : {-1, 1}) b.clones.push_back({b.x + k * 3, b.z + 1.5, k, 4, 0.6});
                    msg = {"SHADOW CLONE JUTSU!", 1.0};
                }
                for (double k : {-0.3, 0.0, 0.3}) eshot(b.x, b.z + 1.5, k * 8, 10);
            } else if (kind == "punch") {
                b.atk = 3.2 * lap;
                eshot(b.x, b.z + 1.5, (px - b.x) * 0.18, 7.5, true);
                for (int k : {-4, 4}) eshot(b.x + k * 0.4, b.z + 1.5, k * 0.4, 9);
                msg = {"SERIOUS PUNCH!", 0.8};
            } else if (kind == "apples") {
                b.atk = 0.55 * lap;
                double ax = grand().uniform(-X + 1, X - 1);
                double vz = grand().uniform(7, 11);
                eshot(ax, -Z - 1, 0.0, vz, false, "apple");
            } else {  // geass
                b.atk = 2.6 * lap;
                eshot(b.x, b.z + 1.5, (px - b.x) * 0.2, 9, true, "geass");
                eshot(b.x + 1, b.z + 1.5, 1.5, 9);
                eshot(b.x - 1, b.z + 1.5, -1.5, 9);
            }
        }
        if (b.hp <= 0) {
            score += 1500 + 250 * wave;
            for (int i = 0; i < 30; i++) {
                double a = i * TAU / 30;
                sparks.push_back({b.x, b.z, std::cos(a) * 9, std::sin(a) * 9, 0.9, b.def->outfit});
            }
            msg = {std::string(b.def->name) + " DEFEATED!  +" + std::to_string(1500 + 250 * wave), 2.2};
            boss.reset();
            bosses_down++;
            hp = std::min(max_hp, hp + 1);
            next_wave();
        }
    }

    std::vector<Box> build(Renderer& r, TextCache& text, const Theme& th, double t) override {
        std::vector<Box> out = {box(0, -0.2, 0, 2 * X + 2, 0.4, 2 * Z + 4, th.slab, 0.1)};
        auto add = [&](const std::vector<Box>& v) { out.insert(out.end(), v.begin(), v.end()); };
        for (double x : {-X - 0.4, X + 0.4}) out.push_back(box(x, 0.3, 0, 0.3, 0.6, 2 * Z + 4, th.wire, 0.7));
        for (int i = 0; i < 36; i++) {  // scrolling star field
            double sx = ((i * 7919) % 100) / 100.0 * 2 * X - X;
            double sz = pmod(((i * 104729) % 100) / 100.0 * 2 * Z + t * (2 + i % 4), 2 * Z) - Z;
            out.push_back(box(sx, 0.05, sz, 0.12, 0.05, 0.12, {0.8, 0.9, 1.0}, 0.9, 0.0, 0.0));
        }
        if (stage == "select") {
            for (int i = 0; i < 4; i++) {
                const Ship& sh = SHIPS[i];
                double x = (i - 1.5) * 6.4;
                bool on = i == sel;
                out.push_back(box(x, 0.05, 1.0, 5.4, 0.2, 5.4, on ? th.ped_sel : th.ped, on ? 0.9 : 0.2));
                add(dino_boxes(sh.kind, x, 0.4, 1.6, t, sh.scale * 1.9, PI / 2 + (on ? 0.4 * std::sin(t) : 0), 0.6, 0.0));
                flat_label(r, text, std::to_string(i + 1) + "  " + sh.name, x, 0.3, 4.9, 0.7, on ? C3(1, 1, 1) : th.t_ped);
            }
            return out;
        }
        for (auto& e : enemies) {
            C3 c = ENEMY_COLORS[e.kind];
            double wob = std::sin(t * 5 + e.fx);
            add({box(e.x, 0.6, e.z, 1.3, 0.9, 1.0, c, 0.7), box(e.x - 0.25, 0.65, e.z + 0.52, 0.22, 0.22, 0.1, {1, 1, 1}, 0.8),
                 box(e.x + 0.25, 0.65, e.z + 0.52, 0.22, 0.22, 0.1, {1, 1, 1}, 0.8), box(e.x - 0.7 - 0.1 * wob, 0.4, e.z, 0.3, 0.3, 0.8, c, 0.4),
                 box(e.x + 0.7 + 0.1 * wob, 0.4, e.z, 0.3, 0.3, 0.8, c, 0.4)});
            if (e.kind % 2 == 0) add({box(e.x - 0.4, 1.25, e.z, 0.3, 0.5, 0.3, c, 0.9), box(e.x + 0.4, 1.25, e.z, 0.3, 0.5, 0.3, c, 0.9)});  // ears / horns
        }
        if (boss) {
            const BossState& b = *boss;
            add(figure(b.x, b.z, 1.5, *b.def, t));
            if (std::string(b.def->attack) == "beam" && b.atk < 1.0)  // charging orb
                out.push_back(box(b.x, 3.0, b.z + 1.2, 1.2 + (1 - b.atk) * 1.4, 1.2 + (1 - b.atk) * 1.4, 1.2, {0.4, 0.7, 1.0}, 2.0, t * 3));
            for (auto& c : b.clones) add(figure(c.x, c.z, 0.8, *b.def, t, 0.5));
            flat_label(r, text, std::string(b.def->name) + "  (" + b.def->anime + ")", b.x, 0.2, b.z + 3.0, 0.8, {1, 0.9, 0.5});
        }
        for (auto& bm : beams) {
            bool on = bm.t_on <= 0;
            out.push_back(box(bm.x, 0.5, 0.0, on ? bm.w : 0.25, on ? 0.7 : 0.2, 2 * Z + 4, on ? C3(0.4, 0.8, 1.0) : C3(1, 0.3, 0.3), on ? 2.0 : 1.0, 0.0, 0.0));
        }
        for (auto& sh : bullets) out.push_back(box(sh.x, 0.5, sh.z, 0.3, 0.3, 0.9, sh.pierce ? C3(0.5, 1.0, 0.7) : C3(1.0, 0.9, 0.3), 1.8));
        for (auto& eb : ebullets) {
            double s = eb.big ? 1.1 : 0.5;
            C3 col = eb.tag != "apple" ? C3(1, 0.15, 0.2) : C3(0.9, 0.05, 0.05);
            out.push_back(box(eb.x, 0.5, eb.z, s, s, s, col, 1.7, t * 4));
        }
        for (auto& d : drops) {
            C3 col = d.kind == "rapid" ? C3(1, 0.8, 0.1) : d.kind == "shield" ? C3(0.3, 0.7, 1) : C3(0.3, 1, 0.4);
            out.push_back(box(d.x, 0.6, d.z, 0.9, 0.9, 0.9, col, 1.8, t * 3));
        }
        for (auto& sp : sparks) out.push_back(box(sp.x, 0.6, sp.z, 0.28, 0.28, 0.28, sp.col, 1.8));
        if (stage == "play" && !(invuln > 0 && ((long long)(t * 14)) % 2)) {
            add(dino_boxes(ship->kind, px, 0.35, Z - 2.1, t, ship->scale, PI / 2, 1.0));
            out.push_back(box(px, 0.1, Z - 1.9, 1.5, 0.2, 2.4, th.ped_sel, 0.9));  // the ship's hull plate
            for (int sx : {-1, 1}) {  // wings and an engine glow, so the dinosaur reads as a ship from above
                out.push_back(box(px + sx * 1.3, 0.12, Z - 1.4, 1.7, 0.14, 1.0, th.ped_sel, 0.8, sx * 0.0));
                out.push_back(box(px + sx * 0.45, 0.2, Z - 0.4 + 0.15 * std::sin(t * 30), 0.4, 0.3, 0.7, {1.0, 0.6, 0.15}, 1.9));
            }
            if (shield > 0) out.push_back(box(px, 0.9, Z - 1.6, 3.0, 1.2, 3.0, {0.3, 0.7, 1.0}, 1.0, t * 2, 1.0));
        }
        return out;
    }

    void hud(double W, double H) override {
        const Theme& th = app.theme();
        if (stage == "select") {
            app.txt("CHOOSE YOUR DINOSAUR", W / 2, 92, 24, th.accent, true);
            const Ship& sh = SHIPS[sel];
            app.txt(std::string(sh.name) + ": " + sh.blurb + "   (lives " + std::to_string(sh.hp) + ")", W / 2, 124, 15, th.text, true);
            app.txt("1-4 or arrows + Enter   -   every wave ends with an anime-character boss", W / 2, 148, 12, th.dim, true);
            return;
        }
        app.txt("SCORE " + std::to_string(score) + "    WAVE " + std::to_string(wave) + "    " + ship->name + "  HP " + std::string(std::max(hp, 0), '#') +
                    std::string(std::max(max_hp - hp, 0), '.') + "    ROAR BOMBS " + std::to_string(bombs) + " (B)",
                W / 2, 92, 15, th.accent, true);
        std::vector<std::string> extras;
        if (rapid > 0) extras.push_back(su::fmt("RAPID FIRE %.0fs", rapid));
        if (shield > 0) extras.push_back(su::fmt("SHIELD %.0fs", shield));
        if (reverse > 0) extras.push_back("CONTROLS REVERSED!");
        if (!extras.empty()) app.txt(su::join(extras, "   "), W / 2, 114, 13, th.good, true);
        if (boss) {
            const BossState& b = *boss;
            app.renderer().rect(W / 2 - 200, 138, 400, 14, C4(0, 0, 0, 0.6));
            app.renderer().rect(W / 2 - 198, 140, 396.0 * std::max(b.hp, 0) / b.max, 10, C4(1.0, 0.25, 0.25, 1.0));
            app.txt(std::string(b.def->name) + "  -  " + b.def->anime, W / 2, 156, 13, th.text, true);
        }
        if (msg.second > 0) app.txt(msg.first, W / 2, H * 0.34, 30, th.logo, true);
        if (over) {
            app.txt("SHIP DOWN  -  press R", W / 2, H * 0.32, 34, {1, 0.3, 0.3}, true);
            app.txt("Score " + std::to_string(score) + "  -  reached wave " + std::to_string(wave), W / 2, H * 0.32 + 46, 16, th.text, true);
        }
    }
};
}  // namespace

std::unique_ptr<Game> make_galaga(GameHost& app) { return std::make_unique<Galaga>(app); }
