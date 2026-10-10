// OpenGL 3.3 renderer (port of renderer.py): floor grid, instanced neon boxes, textured quads (3D billboards + HUD), 4x MSAA.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "mathx.hpp"

struct Theme;

struct Tex {
    unsigned id = 0;
    int w = 0, h = 0;
    ~Tex();
};
using TexP = std::shared_ptr<Tex>;

struct Quad {
    TexP tex;
    float mvp[16];  // column-major
    C4 tint;
    UV uv;
};

class Renderer {
public:
    static constexpr int MAX_BOXES = 32768;
    static constexpr int MAX_QUADS = 6000;
    Renderer();
    ~Renderer();
    std::string gpu_name;
    TexP white;
    M4 viewproj, inv_viewproj, ortho;

    TexP make_texture(const uint8_t* rgba, int w, int h, bool mips = false);
    void resize(int w, int h);
    void set_camera(const M4& view, const M4& proj, const V3& cam, double t, double lw, double lh, const Theme& theme, double flash = 0.0);
    void add_boxes(const std::vector<Box>& b) { boxes.insert(boxes.end(), b.begin(), b.end()); }
    void add_box(const Box& b) { boxes.push_back(b); }
    void quad3d(const TexP& tex, const M4& model, C4 tint = {}, UV uv = {});
    void quad2d(const TexP& tex, double x, double y, double w, double h, C4 tint = {}, UV uv = {});
    void rect(double x, double y, double w, double h, C4 color) { quad2d(white, x, y, w, h, color); }
    void render(int target_fbo = 0);  // draws everything recorded this frame, then resets the lists
    std::vector<uint8_t> read_pixels();  // last frame, RGBA, top row first
    int width() const { return w_; }
    int height() const { return h_; }

    // the frame being recorded (the app filters some of these, see App::keep_clear)
    std::vector<Box> boxes;
    std::vector<Quad> q3d, q2d;

private:
    int w_ = 0, h_ = 0;
    unsigned box_prog = 0, floor_prog = 0, quad_prog = 0;
    unsigned cube_vbo = 0, inst_vbo = 0, box_vao = 0, empty_vao = 0;
    unsigned msaa_fbo = 0, msaa_color = 0, msaa_depth = 0, resolve_fbo = 0, resolve_color = 0;
    int samples = 4;
    float globals[44];
    float clear[4] = {0, 0, 0, 1};
    int q_mvp = -1, q_tint = -1, q_uv = -1, q_tex = -1;
    void upload_globals(unsigned prog);
};
