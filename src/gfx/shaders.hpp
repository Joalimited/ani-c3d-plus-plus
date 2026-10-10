// GLSL 3.30 ports of the WGSL shaders: boxes get a solid lit face plus glowing neon edges (the FSN look); the floor is a
// procedural grid (or a forest floor); quads are textured and tinted.
#pragma once

namespace shaders {
constexpr const char* GLOBALS = R"GLSL(
uniform mat4 u_viewproj;
uniform vec4 u_cam;     // xyz camera position
uniform vec4 u_fog;     // rgb fog colour, a = density
uniform vec4 u_time;    // x = seconds, y = posterize levels, z = lightning flash, w = floor style (1 = forest, 2 = sakura)
uniform vec4 u_fbase;
uniform vec4 u_fsmall;
uniform vec4 u_fbig;
uniform vec4 u_fglow;

vec3 fog_only(vec3 col, vec3 world) {
  float d = length(world - u_cam.xyz) * u_fog.a;
  vec3 c = mix(col, u_fog.rgb, clamp(1.0 - exp(-d * d), 0.0, 1.0));
  c += vec3(0.55, 0.45, 0.75) * u_time.z;      // lightning
  return c;
}

vec3 apply_fog(vec3 col, vec3 world) {
  vec3 c = fog_only(col, world);
  if (u_time.y > 0.5) { c = floor(c * u_time.y + 0.5) / u_time.y; }   // retro posterize
  return c;
}
)GLSL";

constexpr const char* BOX_VS = R"GLSL(
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_nrm;
layout(location = 2) in vec3 i_pos;
layout(location = 3) in float i_glow;
layout(location = 4) in vec3 i_scale;
layout(location = 5) in float i_yaw;
layout(location = 6) in vec4 i_color;
layout(location = 7) in float i_pitch;
layout(location = 8) in float i_flat;
layout(location = 9) in float i_rollz;
layout(location = 10) in float i_rollx;
out vec3 v_lp;
out vec3 v_nrm;
out vec3 v_world;
out vec4 v_color;
out float v_glow;
out vec3 v_sc;
out float v_flat;

vec3 yaw_rot(vec3 v, float a) { float c = cos(a); float s = sin(a); return vec3(v.x * c + v.z * s, v.y, -v.x * s + v.z * c); }
vec3 pitch_rot(vec3 v, float a) { float c = cos(a); float s = sin(a); return vec3(v.x, v.y * c - v.z * s, v.y * s + v.z * c); }
vec3 rotz(vec3 v, float a) { float c = cos(a); float s = sin(a); return vec3(v.x * c - v.y * s, v.x * s + v.y * c, v.z); }
vec3 rotx(vec3 v, float a) { float c = cos(a); float s = sin(a); return vec3(v.x, v.y * c - v.z * s, v.y * s + v.z * c); }

void main() {
  // local rolls first (limbs swing about the side axis, wings flap about the forward axis), then heading, then the book tilt
  vec3 local = rotx(rotz(a_pos * i_scale, i_rollz), i_rollx);
  vec3 world = i_pos + pitch_rot(yaw_rot(local, i_yaw), i_pitch);
  gl_Position = u_viewproj * vec4(world, 1.0);
  v_lp = a_pos;
  v_nrm = pitch_rot(yaw_rot(rotx(rotz(a_nrm, i_rollz), i_rollx), i_yaw), i_pitch);
  v_world = world;
  v_color = i_color;
  v_glow = i_glow;
  v_sc = i_scale;
  v_flat = i_flat;
}
)GLSL";

constexpr const char* BOX_FS = R"GLSL(
in vec3 v_lp;
in vec3 v_nrm;
in vec3 v_world;
in vec4 v_color;
in float v_glow;
in vec3 v_sc;
in float v_flat;
out vec4 o_color;

void main() {
  vec3 n = normalize(v_nrm);
  // the face we are on is the local axis where |lp| is largest; edges are measured along the other two axes
  vec3 al = abs(v_lp);
  float mx = max(al.x, max(al.y, al.z));
  vec3 m = vec3(1.0) - step(vec3(mx - 0.0001), al);
  vec3 dw = (vec3(0.5) - abs(v_lp)) * v_sc;
  float e = min(m.x > 0.5 ? dw.x : 1000.0, min(m.y > 0.5 ? dw.y : 1000.0, m.z > 0.5 ? dw.z : 1000.0));
  float edge = (1.0 - smoothstep(0.0, 0.05, e)) * v_color.a;

  vec3 L = normalize(vec3(0.35, 0.85, 0.30));
  float lit = 0.30 + 0.70 * max(dot(n, L), 0.0);
  vec3 col = v_color.rgb * lit * 0.65;
  vec3 edge_col = mix(v_color.rgb, vec3(1.0), 0.45) * (1.1 + v_glow * 1.6);
  col = mix(col, edge_col, edge);
  col += v_color.rgb * v_glow * 0.55;
  // flat boxes (the book) skip posterizing so paper keeps its smooth cream shading in every theme
  if (v_flat > 0.5) { o_color = vec4(fog_only(col, v_world), 1.0); return; }
  o_color = vec4(apply_fog(col, v_world), 1.0);
}
)GLSL";

constexpr const char* FLOOR_VS = R"GLSL(
out vec3 v_world;
void main() {
  vec2 c[6] = vec2[6](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0), vec2(-1.0, 1.0), vec2(1.0, -1.0), vec2(1.0, 1.0));
  vec2 p = c[gl_VertexID] * 600.0;
  vec3 w = vec3(p.x + u_cam.x, -0.02, p.y + u_cam.z);   // follows the camera so the floor is endless
  gl_Position = u_viewproj * vec4(w, 1.0);
  v_world = w;
}
)GLSL";

constexpr const char* FLOOR_FS = R"GLSL(
in vec3 v_world;
out vec4 o_color;

float grid(vec2 p, float cell) {
  vec2 q = p / cell;
  vec2 w = fwidth(q) * 1.2;
  vec2 a = abs(fract(q - 0.5) - 0.5) / max(w, vec2(0.0001));
  return 1.0 - min(min(a.x, a.y), 1.0);
}

float hash2(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

float vnoise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  vec2 u = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash2(i), hash2(i + vec2(1.0, 0.0)), u.x), mix(hash2(i + vec2(0.0, 1.0)), hash2(i + vec2(1.0, 1.0)), u.x), u.y);
}

float fbm(vec2 p) { return 0.55 * vnoise(p) + 0.3 * vnoise(p * 2.03 + 17.0) + 0.15 * vnoise(p * 4.1 + 41.0); }

// forest floor: moss in two shades, bare-earth patches, leaf litter up close and slow dappled sunlight through the canopy
vec3 forest_floor(vec2 p) {
  float detail = 1.0 - smoothstep(0.25, 1.4, length(fwidth(p)));
  float moss = fbm(p * 0.06);
  vec3 col = mix(u_fbase.rgb, u_fsmall.rgb, smoothstep(0.32, 0.72, moss));
  float earth = smoothstep(0.58, 0.78, fbm(p * 0.018 + 5.0));
  col = mix(col, u_fbig.rgb, earth * 0.85);
  float litter = vnoise(p * 1.4);
  vec3 tint = mix(vec3(0.42, 0.26, 0.08), vec3(0.55, 0.45, 0.12), vnoise(p * 0.7 + 3.0));
  col = mix(col, tint, smoothstep(0.7, 0.8, litter) * (u_time.w > 1.5 ? 0.12 : 0.55) * detail);  // sakura: little leaf litter
  col *= 0.88 + 0.24 * vnoise(p * 0.45) * detail + 0.0;
  float sun = fbm(p * 0.07 + vec2(u_time.x * 0.04, u_time.x * 0.025));
  col *= 0.72 + 0.6 * smoothstep(0.42, 0.7, sun);
  return col;
}

void main() {
  vec2 p = v_world.xz;
  if (u_time.w > 0.5) {
    vec3 fc = forest_floor(p);
    if (u_time.w > 1.5) {  // sakura: fallen petals, drifted into soft pink patches
      float detail = 1.0 - smoothstep(0.25, 1.4, length(fwidth(p)));
      float drift = smoothstep(0.45, 0.8, fbm(p * 0.035 + 9.0));
      float petal = smoothstep(0.86, 0.92, vnoise(p * 3.1)) * detail;
      vec3 pink = vec3(0.95, 0.62, 0.74);
      fc = mix(fc, pink * 0.8, clamp(petal * (0.25 + 0.75 * drift) + drift * 0.22, 0.0, 0.85));
    }
    fc += u_fglow.rgb * exp(-length(p) / 70.0);
    o_color = vec4(apply_fog(fc, v_world), 1.0);
    return;
  }
  float small = grid(p, 4.0);
  float big = grid(p, 20.0);
  float r = length(p);
  vec3 col = u_fbase.rgb;
  col += u_fglow.rgb * exp(-r / 70.0);
  col += u_fsmall.rgb * small;
  col += u_fbig.rgb * big;
  o_color = vec4(apply_fog(col, v_world), 1.0);
}
)GLSL";

constexpr const char* QUAD_VS = R"GLSL(
uniform mat4 q_mvp;
uniform vec4 q_uv;
out vec2 v_uv;
void main() {
  vec2 c[6] = vec2[6](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(0.0, 1.0), vec2(1.0, 0.0), vec2(1.0, 1.0));
  vec2 k = c[gl_VertexID];
  gl_Position = q_mvp * vec4(k, 0.0, 1.0);
  v_uv = mix(q_uv.xy, q_uv.zw, k);
}
)GLSL";

constexpr const char* QUAD_FS = R"GLSL(
uniform sampler2D q_tex;
uniform vec4 q_tint;
in vec2 v_uv;
out vec4 o_color;
void main() { o_color = texture(q_tex, v_uv) * q_tint; }
)GLSL";
}  // namespace shaders
