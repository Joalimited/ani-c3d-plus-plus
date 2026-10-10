// The FSN world (port of scene.py): a root pedestal, labelled section platforms with one pedestal per anime wired to
// the root, and a block field of episodes.
#pragma once
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "../gfx/renderer.hpp"
#include "../gfx/text.hpp"
#include "json.hpp"
#include "themes.hpp"

constexpr double PED_W = 4.6, PED_D = 6.4, PED_H = 0.5;

// what the mouse is over: ("root"), ("ped", i), ("ep", "12"), and the other modes' kinds (books, buttons...)
struct Hit {
    std::string kind;
    int i = -1;
    std::string s;
    bool operator==(const Hit& o) const { return kind == o.kind && i == o.i && s == o.s; }
    bool operator!=(const Hit& o) const { return !(*this == o); }
    explicit operator bool() const { return !kind.empty(); }
};

class Camera {
public:
    V3 target{0, 0, 0};
    double yaw = 0, pitch = 38 * PI / 180, dist = 55;
    V3 g_target{0, 0, 0};
    double g_yaw = 0, g_pitch = 38 * PI / 180, g_dist = 55;
    void goto_(V3 target, double yaw, double pitch, double dist);
    void update(double dt);
    void snap();
    V3 eye() const;
    void orbit(double dx, double dy);
    void pan(double dx, double dy);
    void zoom(double dy);
};

struct Ped {
    std::string id, title, poster_url;
    TexP poster;
    double x = 0, z = 0, born = 0;
    int eps = -1;  // episode count, -1 = unknown
    std::string sub;
    nlohmann::json resume;  // {ep, pos} on a continue-watching pedestal
    int sec = 0, row = 0, col = 0;
};

struct Section {
    std::string label;
    double x0, w, zf, d, cx;
    int n;
};

struct Field {
    int idx;
    std::vector<std::string> eps;
    std::set<std::string> watched;
    int cols, rows;
    double pitch, w, d, cx, cz, born;
};

class Scene {
public:
    Scene();
    Camera cam;
    std::vector<Ped> peds;
    std::optional<Field> field;
    int selected = -1;
    std::string heading = "ANI";
    struct Pick {
        V3 mn, mx;
        Hit ref;
    };
    std::vector<Pick> pick_boxes;
    std::vector<Section> sections;
    double dist_scale = 1.0;
    std::set<std::string>* starred = nullptr;  // ids on the watch list (shared with the app)

    static std::vector<std::pair<double, double>> ring_positions(int n);
    static constexpr double PX = PED_W + 2.0, PZ = PED_D + 3.6, HEADER = 4.4;
    void set_results(const std::vector<nlohmann::json>& rows, double t, const std::string& heading = "SEARCH",
                     const std::vector<std::pair<std::string, std::vector<nlohmann::json>>>* groups = nullptr);
    void extents(double& x0, double& x1, double& z0, double& z1) const;
    void goto_home();
    void open_field(int idx, const std::vector<std::string>& episodes, const std::set<std::string>& watched, double t);
    void close_field();
    std::pair<double, double> ep_pos(int i) const;
    void build(Renderer& r, TextCache& text, double t, const Hit& hover, const nlohmann::json& player, const V3& cam_pos, const Theme& th);
    static std::vector<std::string> wrap_title(const std::string& title, size_t width = 15);
    void label(Renderer& r, TextCache& text, const std::string& s, V3 pos, double height, C3 color, bool center = false);
    void digits(Renderer& r, TextCache& text, const std::string& ep, double x, double y, double z, C4 tint);
    Hit pick(const V3& origin, const V3& direction) const;
};
