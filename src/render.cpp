#include "render.h"

#include <OpenGL/gl3.h>

#include <sys/stat.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include "drone.h"
#include "fpvapi.h"
#include "log.h"
#include "paths.h"

// libfpv's renderer, calling libfpv through the runtime-loaded table.
#define fpv_texture_get fpvapi::fpv.fpv_texture_get
#define FPV_RENDER_GL3_IMPLEMENTATION
#include "fpv_render_gl3.h"
#undef fpv_texture_get

namespace render {
namespace {

using fpvapi::fpv;

fpv_gl3 g_ui{};
bool g_ui_ready = false;
bool g_gl_failed = false;
float g_aspect = 16.0f / 9.0f;
std::chrono::steady_clock::time_point g_last;
double g_time = 0;

// The FPV post-process: Brown-Conrady lens distortion (docs/INTEGRATION.md),
// analog noise and breakup, and full static when the signal is lost.
const char *kPostVs = R"(#version 330 core
out vec2 uv;
void main() { vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2); uv = p; gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); })";
const char *kPostFs = R"(#version 330 core
in vec2 uv; uniform sampler2D scene; uniform vec2 k; uniform float zoom; uniform float aspect;
uniform float noise; uniform float breakup; uniform float time; uniform float analog; uniform float lost;
out vec4 o;
float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
void main() {
  vec2 scale = vec2(aspect, 1.0) / (0.5 * sqrt(aspect * aspect + 1.0));
  vec2 c = (uv - 0.5) * scale;
  float r2 = dot(c, c);
  vec2 suv = c * (1.0 + k.x * r2 + k.y * r2 * r2) / zoom / scale + 0.5;
  if (breakup > 0.0) {
    float line = step(1.0 - breakup * 0.25, hash(vec2(floor(uv.y * 120.0), floor(time * 20.0))));
    suv.x += line * (hash(vec2(time, uv.y)) - 0.5) * 0.15;
  }
  vec3 col = (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) ? vec3(0.0) : texture(scene, suv).rgb;
  float n = hash(uv * 513.0 + time) - 0.5;
  col += n * noise * 0.9 * analog;
  col = mix(col, vec3(dot(col, vec3(0.3, 0.59, 0.11))), noise * 0.6 * analog);
  float band = 0.75 + 0.25 * sin(uv.y * 25.0 + time * 4.0);
  col = mix(col, vec3(hash(floor(uv * vec2(320.0, 180.0)) + fract(time) * 91.0) * 0.85 * band), lost);
  o = vec4(col, 1.0);
})";

struct Post {
    GLuint program = 0, vao = 0, scene = 0;
    int w = 0, h = 0;
    GLint loc_scene, loc_k, loc_zoom, loc_aspect, loc_noise, loc_breakup, loc_time, loc_analog, loc_lost;
};
Post g_post;

GLuint compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        Log("render: shader error: %s", log);
    }
    return s;
}

bool init_post() {
    GLuint v = compile(GL_VERTEX_SHADER, kPostVs), f = compile(GL_FRAGMENT_SHADER, kPostFs);
    g_post.program = glCreateProgram();
    glAttachShader(g_post.program, v);
    glAttachShader(g_post.program, f);
    glLinkProgram(g_post.program);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(g_post.program, GL_LINK_STATUS, &ok);
    if (!ok) return false;
    GLuint p = g_post.program;
    g_post.loc_scene = glGetUniformLocation(p, "scene");
    g_post.loc_k = glGetUniformLocation(p, "k");
    g_post.loc_zoom = glGetUniformLocation(p, "zoom");
    g_post.loc_aspect = glGetUniformLocation(p, "aspect");
    g_post.loc_noise = glGetUniformLocation(p, "noise");
    g_post.loc_breakup = glGetUniformLocation(p, "breakup");
    g_post.loc_time = glGetUniformLocation(p, "time");
    g_post.loc_analog = glGetUniformLocation(p, "analog");
    g_post.loc_lost = glGetUniformLocation(p, "lost");
    glGenVertexArrays(1, &g_post.vao);
    glGenTextures(1, &g_post.scene);
    return true;
}

bool init() {
    if (g_ui_ready) return true;
    if (g_gl_failed) return false;
    g_gl_failed = true;
    const char *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
    Log("render: OpenGL %s", version ? version : "?");
    if (!version || version[0] < '3') {
        Log("render: the game's OpenGL context is older than 3.2, nothing will be drawn");
        return false;
    }
    if (!fpv_gl3_init(&g_ui) || !init_post()) {
        Log("render: shaders failed to build");
        return false;
    }
    g_gl_failed = false;
    g_ui_ready = true;
    return true;
}

// The finished frame, through the lens (or as static) back onto itself.
void post(const fpv_frame &f, bool lost, int w, int h) {
    GLint last_program, last_vao, last_active, last_tex, last_viewport[4];
    glGetIntegerv(GL_CURRENT_PROGRAM, &last_program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &last_vao);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &last_active);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_tex);
    glGetIntegerv(GL_VIEWPORT, last_viewport);
    GLboolean was_blend = glIsEnabled(GL_BLEND), was_depth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean was_cull = glIsEnabled(GL_CULL_FACE), was_scissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean depth_mask;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);

    glBindTexture(GL_TEXTURE_2D, g_post.scene);
    if (g_post.w != w || g_post.h != h) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        g_post.w = w, g_post.h = h;
    }
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);

    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_FALSE);
    glViewport(0, 0, w, h);
    glUseProgram(g_post.program);
    glUniform1i(g_post.loc_scene, 0);
    bool lens = !lost && (f.camera.distortion_k1 != 0 || f.camera.distortion_k2 != 0);
    glUniform2f(g_post.loc_k, lens ? f.camera.distortion_k1 : 0, lens ? f.camera.distortion_k2 : 0);
    glUniform1f(g_post.loc_zoom, lens && f.camera.distortion_zoom > 0 ? f.camera.distortion_zoom : 1.0f);
    glUniform1f(g_post.loc_aspect, float(w) / float(h));
    glUniform1f(g_post.loc_noise, f.video.noise);
    glUniform1f(g_post.loc_breakup, f.video.breakup);
    glUniform1f(g_post.loc_time, float(g_time));
    glUniform1f(g_post.loc_analog, f.video.is_digital ? 0.0f : 1.0f);
    glUniform1f(g_post.loc_lost, lost ? 1.0f : 0.0f);
    glBindVertexArray(g_post.vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glUseProgram(GLuint(last_program));
    glBindVertexArray(GLuint(last_vao));
    glBindTexture(GL_TEXTURE_2D, GLuint(last_tex));
    glActiveTexture(GLenum(last_active));
    glViewport(last_viewport[0], last_viewport[1], last_viewport[2], last_viewport[3]);
    glDepthMask(depth_mask);
    if (was_blend) glEnable(GL_BLEND);
    if (was_depth) glEnable(GL_DEPTH_TEST);
    if (was_cull) glEnable(GL_CULL_FACE);
    if (was_scissor) glEnable(GL_SCISSOR_TEST);
}

// fpv-data/shot.txt (created by hand or the autotest) saves the next frame as
// fpv-data/shots/<time>.ppm and is deleted. Works without screen recording rights.
void screenshot(int w, int h) {
    static int counter = 0;
    if (++counter % 15) return;  // a few checks per second are plenty
    std::string trigger = paths::data_dir() + "/shot.txt";
    struct stat st;
    if (stat(trigger.c_str(), &st) != 0) return;
    std::remove(trigger.c_str());
    std::string dir = paths::data_dir() + "/shots";
    mkdir(dir.c_str(), 0755);
    char name[64];
    std::time_t now = std::time(nullptr);
    std::strftime(name, sizeof name, "/%Y%m%d-%H%M%S.ppm", std::localtime(&now));
    std::vector<uint8_t> px(size_t(w) * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    FILE *f = std::fopen((dir + name).c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; --y) std::fwrite(&px[size_t(y) * w * 3], 1, size_t(w) * 3, f);
    std::fclose(f);
    Log("render: screenshot %s%s", dir.c_str(), name);
}

}  // namespace

void frame(int fb_w, int fb_h, int win_w, int win_h) {
    if (fb_w <= 0 || fb_h <= 0 || win_w <= 0 || win_h <= 0) return;
    g_aspect = float(fb_w) / float(fb_h);
    auto now = std::chrono::steady_clock::now();
    float dt = g_last.time_since_epoch().count() ? std::chrono::duration<float>(now - g_last).count() : 0.016f;
    g_last = now;
    g_time += dt;

    // librw keeps its Im2D/Im3D VBO bound to GL_ARRAY_BUFFER (not VAO state)
    // and uploads with glBufferData without rebinding: if we leave another
    // buffer there, every engine 2D/3D batch afterwards draws stale vertices.
    GLint draw_fb, read_fb, array_buffer, vao, unpack, pack;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_fb);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_fb);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &array_buffer);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack);
    glGetIntegerv(GL_PACK_ALIGNMENT, &pack);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (fpvapi::loaded() && init()) {
        fpv.fpv_overlay_set_display(float(win_w), float(win_h), 1.0f);
        static fpv_frame snapshot;
        bool lost = false;
        if (drone::osd_frame(&snapshot, &lost)) {
            post(snapshot, lost, fb_w, fb_h);
            if (drone::show_osd() || lost)
                fpv_gl3_render(&g_ui, fpv.fpv_osd_draw_data(&snapshot, float(win_w), float(win_h), FPV_OSD_STYLE_ANALOG),
                               fb_w, fb_h);
        }
        fpv_gl3_render(&g_ui, fpv.fpv_overlay_render(dt), fb_w, fb_h);
    }
    screenshot(fb_w, fb_h);

    glBindVertexArray(GLuint(vao));
    glBindBuffer(GL_ARRAY_BUFFER, GLuint(array_buffer));
    glPixelStorei(GL_UNPACK_ALIGNMENT, unpack);
    glPixelStorei(GL_PACK_ALIGNMENT, pack);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(draw_fb));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(read_fb));
}

float aspect() { return g_aspect; }

void release() {
    if (g_ui_ready) fpv_gl3_shutdown(&g_ui);
    if (g_post.program) glDeleteProgram(g_post.program);
    if (g_post.vao) glDeleteVertexArrays(1, &g_post.vao);
    if (g_post.scene) glDeleteTextures(1, &g_post.scene);
    g_post = Post{};
    g_ui_ready = false;
}

}  // namespace render
