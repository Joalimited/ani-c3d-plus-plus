#include "renderer.hpp"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

#include "../world/themes.hpp"
#include "gl.hpp"
#include "shaders.hpp"

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif

#define ANI_GL_DEFINE(type, name) type name##_p = nullptr;
ANI_GL_FUNCS(ANI_GL_DEFINE)
#undef ANI_GL_DEFINE

bool gl_load() {
    bool ok = true;
#define ANI_GL_LOAD(type, name)                                  \
    name##_p = (type)SDL_GL_GetProcAddress(#name);               \
    if (!name##_p) {                                             \
        std::fprintf(stderr, "missing GL function %s\n", #name); \
        ok = false;                                              \
    }
    ANI_GL_FUNCS(ANI_GL_LOAD)
#undef ANI_GL_LOAD
    return ok;
}

Tex::~Tex() {
    if (id) glDeleteTextures(1, &id);
}

static unsigned compile(GLenum kind, const std::string& src) {
    unsigned s = glCreateShader(kind);
    const char* p = src.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    int ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        throw std::runtime_error(std::string("shader compile failed: ") + log);
    }
    return s;
}

static unsigned program(const std::string& vs, const std::string& fs) {
    unsigned p = glCreateProgram();
    unsigned a = compile(GL_VERTEX_SHADER, vs), b = compile(GL_FRAGMENT_SHADER, fs);
    glAttachShader(p, a);
    glAttachShader(p, b);
    glLinkProgram(p);
    int ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(p, sizeof log, nullptr, log);
        throw std::runtime_error(std::string("shader link failed: ") + log);
    }
    glDeleteShader(a);
    glDeleteShader(b);
    return p;
}

static std::vector<float> cube_vertices() {
    struct Face {
        float n[3];
        float c[4][3];
    };
    const Face faces[6] = {  // normal, then four corners (counter-clockwise seen from outside)
        {{0, 0, 1}, {{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}}},
        {{0, 0, -1}, {{1, -1, -1}, {-1, -1, -1}, {-1, 1, -1}, {1, 1, -1}}},
        {{1, 0, 0}, {{1, -1, 1}, {1, -1, -1}, {1, 1, -1}, {1, 1, 1}}},
        {{-1, 0, 0}, {{-1, -1, -1}, {-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1}}},
        {{0, 1, 0}, {{-1, 1, 1}, {1, 1, 1}, {1, 1, -1}, {-1, 1, -1}}},
        {{0, -1, 0}, {{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}}},
    };
    std::vector<float> out;
    for (auto& f : faces)
        for (int i : {0, 1, 2, 0, 2, 3}) {
            for (int k = 0; k < 3; k++) out.push_back(f.c[i][k] * 0.5f);
            for (int k = 0; k < 3; k++) out.push_back(f.n[k]);
        }
    return out;
}

Renderer::Renderer() {
    const char* ver = "#version 330 core\n";
    box_prog = program(std::string(ver) + shaders::GLOBALS + shaders::BOX_VS, std::string(ver) + shaders::GLOBALS + shaders::BOX_FS);
    floor_prog = program(std::string(ver) + shaders::GLOBALS + shaders::FLOOR_VS, std::string(ver) + shaders::GLOBALS + shaders::FLOOR_FS);
    quad_prog = program(std::string(ver) + shaders::QUAD_VS, std::string(ver) + shaders::QUAD_FS);
    q_mvp = glGetUniformLocation(quad_prog, "q_mvp");
    q_tint = glGetUniformLocation(quad_prog, "q_tint");
    q_uv = glGetUniformLocation(quad_prog, "q_uv");
    q_tex = glGetUniformLocation(quad_prog, "q_tex");
    const char* r = (const char*)glGetString(GL_RENDERER);
    gpu_name = r ? r : "OpenGL";

    auto verts = cube_vertices();
    glGenVertexArrays(1, &box_vao);
    glBindVertexArray(box_vao);
    glGenBuffers(1, &cube_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, cube_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size() * sizeof(float)), verts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void*)12);
    glGenBuffers(1, &inst_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, inst_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(MAX_BOXES * sizeof(Box)), nullptr, GL_STREAM_DRAW);
    struct Attr {
        int loc, size, offset;
    };
    const Attr attrs[] = {{2, 3, 0}, {3, 1, 12}, {4, 3, 16}, {5, 1, 28}, {6, 4, 32}, {7, 1, 48}, {8, 1, 52}, {9, 1, 56}, {10, 1, 60}};
    for (auto& a : attrs) {
        glEnableVertexAttribArray((GLuint)a.loc);
        glVertexAttribPointer((GLuint)a.loc, a.size, GL_FLOAT, GL_FALSE, sizeof(Box), (void*)(intptr_t)a.offset);
        glVertexAttribDivisor((GLuint)a.loc, 1);
    }
    glGenVertexArrays(1, &empty_vao);
    glBindVertexArray(0);

    uint8_t px[16];
    std::memset(px, 255, sizeof px);
    white = make_texture(px, 2, 2);
    std::memset(globals, 0, sizeof globals);
}

Renderer::~Renderer() {
    if (msaa_fbo) glDeleteFramebuffers(1, &msaa_fbo);
    if (resolve_fbo) glDeleteFramebuffers(1, &resolve_fbo);
    if (msaa_color) glDeleteRenderbuffers(1, &msaa_color);
    if (msaa_depth) glDeleteRenderbuffers(1, &msaa_depth);
    if (resolve_color) glDeleteRenderbuffers(1, &resolve_color);
}

TexP Renderer::make_texture(const uint8_t* rgba, int w, int h, bool mips) {
    auto t = std::make_shared<Tex>();
    t->w = w;
    t->h = h;
    glGenTextures(1, &t->id);
    glBindTexture(GL_TEXTURE_2D, t->id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (mips) {  // a full mip chain so small, dense text stays crisp at a distance
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 8.0f);
        glGetError();  // anisotropy is an extension: ignore if missing
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    return t;
}

void Renderer::resize(int w, int h) {
    if (w == w_ && h == h_ && msaa_fbo) return;
    w_ = w < 1 ? 1 : w;
    h_ = h < 1 ? 1 : h;
    if (!msaa_fbo) {
        glGenFramebuffers(1, &msaa_fbo);
        glGenFramebuffers(1, &resolve_fbo);
        glGenRenderbuffers(1, &msaa_color);
        glGenRenderbuffers(1, &msaa_depth);
        glGenRenderbuffers(1, &resolve_color);
    }
    int max_samples = 4;
    glGetIntegerv(GL_MAX_SAMPLES, &max_samples);
    samples = max_samples < 4 ? max_samples : 4;
    glBindRenderbuffer(GL_RENDERBUFFER, msaa_color);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, w_, h_);
    glBindRenderbuffer(GL_RENDERBUFFER, msaa_depth);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, w_, h_);
    glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msaa_color);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msaa_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, resolve_color);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, w_, h_);
    glBindFramebuffer(GL_FRAMEBUFFER, resolve_fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, resolve_color);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::set_camera(const M4& view, const M4& proj, const V3& cam, double t, double lw, double lh, const Theme& th, double flash) {
    viewproj = proj * view;
    inv_viewproj = viewproj.inverse();
    ortho = mx::ortho_px(lw, lh);
    clear[0] = th.fog.r;
    clear[1] = th.fog.g;
    clear[2] = th.fog.b;
    float* b = globals;
    viewproj.gpu(b);
    b[16] = (float)cam.x; b[17] = (float)cam.y; b[18] = (float)cam.z; b[19] = 0;
    b[20] = th.fog.r; b[21] = th.fog.g; b[22] = th.fog.b; b[23] = th.density;
    b[24] = (float)t; b[25] = (float)th.levels; b[26] = (float)flash; b[27] = th.floor_style;
    const C3* cs[4] = {&th.floor_base, &th.floor_small, &th.floor_big, &th.glow};
    for (int i = 0; i < 4; i++) {
        b[28 + i * 4] = cs[i]->r;
        b[29 + i * 4] = cs[i]->g;
        b[30 + i * 4] = cs[i]->b;
        b[31 + i * 4] = 0;
    }
}

void Renderer::upload_globals(unsigned prog) {
    glUniformMatrix4fv(glGetUniformLocation(prog, "u_viewproj"), 1, GL_FALSE, globals);
    const char* names[] = {"u_cam", "u_fog", "u_time", "u_fbase", "u_fsmall", "u_fbig", "u_fglow"};
    for (int i = 0; i < 7; i++) glUniform4fv(glGetUniformLocation(prog, names[i]), 1, globals + 16 + i * 4);
}

void Renderer::quad3d(const TexP& tex, const M4& model, C4 tint, UV uv) {
    Quad q;
    q.tex = tex;
    (viewproj * model).gpu(q.mvp);
    q.tint = tint;
    q.uv = uv;
    q3d.push_back(std::move(q));
}

void Renderer::quad2d(const TexP& tex, double x, double y, double w, double h, C4 tint, UV uv) {
    Quad q;
    q.tex = tex;
    (ortho * mx::translate(x, y, 0) * mx::scale(w, h, 1)).gpu(q.mvp);
    q.tint = tint;
    q.uv = uv;
    q2d.push_back(std::move(q));
}

void Renderer::render(int target_fbo) {
    glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo);
    glViewport(0, 0, w_, h_);
    glClearColor(clear[0], clear[1], clear[2], 1.0f);
    glDepthMask(GL_TRUE);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);

    // floor
    glDisable(GL_CULL_FACE);
    glUseProgram(floor_prog);
    upload_globals(floor_prog);
    glBindVertexArray(empty_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // boxes
    size_t n = boxes.size() < (size_t)MAX_BOXES ? boxes.size() : (size_t)MAX_BOXES;
    if (n) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);
        glUseProgram(box_prog);
        upload_globals(box_prog);
        glBindVertexArray(box_vao);
        glBindBuffer(GL_ARRAY_BUFFER, inst_vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(MAX_BOXES * sizeof(Box)), nullptr, GL_STREAM_DRAW);  // orphan
        glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(n * sizeof(Box)), boxes.data());
        glDrawArraysInstanced(GL_TRIANGLES, 0, 36, (GLsizei)n);
        glDisable(GL_CULL_FACE);
    }

    // quads: 3D ones read depth, HUD ones ignore it
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glUseProgram(quad_prog);
    glUniform1i(q_tex, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(empty_vao);
    size_t budget = MAX_QUADS;
    unsigned bound = 0;
    auto draw = [&](const std::vector<Quad>& qs) {
        for (auto& q : qs) {
            if (!budget) return;
            budget--;
            if (!q.tex) continue;
            if (q.tex->id != bound) {
                glBindTexture(GL_TEXTURE_2D, q.tex->id);
                bound = q.tex->id;
            }
            glUniformMatrix4fv(q_mvp, 1, GL_FALSE, q.mvp);
            float tint[4] = {q.tint.r, q.tint.g, q.tint.b, q.tint.a};
            float uv[4] = {q.uv.u0, q.uv.v0, q.uv.u1, q.uv.v1};
            glUniform4fv(q_tint, 1, tint);
            glUniform4fv(q_uv, 1, uv);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }
    };
    glDepthFunc(GL_LEQUAL);
    draw(q3d);
    glDepthFunc(GL_ALWAYS);
    draw(q2d);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);

    // resolve MSAA, then show it
    glBindFramebuffer(GL_READ_FRAMEBUFFER, msaa_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolve_fbo);
    glBlitFramebuffer(0, 0, w_, h_, 0, 0, w_, h_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, resolve_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)target_fbo);
    glBlitFramebuffer(0, 0, w_, h_, 0, 0, w_, h_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)target_fbo);
    boxes.clear();
    q3d.clear();
    q2d.clear();
}

std::vector<uint8_t> Renderer::read_pixels() {
    std::vector<uint8_t> px((size_t)w_ * h_ * 4), out((size_t)w_ * h_ * 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, resolve_fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w_, h_, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    for (int y = 0; y < h_; y++) std::memcpy(&out[(size_t)y * w_ * 4], &px[(size_t)(h_ - 1 - y) * w_ * 4], (size_t)w_ * 4);
    return out;
}
