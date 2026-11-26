#include "ear.h"

#include <cmath>

#include "log.h"
#include "vc.h"

namespace ear {
namespace {

bool g_on = false;
bool g_swapped = false;
vc::CMatrix g_ear{};    // the pilot's head, laid out as TheCamera's matrix
vc::CMatrix g_saved{};  // TheCamera's own, during the service

}  // namespace

void set(const float pos[3], const float forward[3], const float up[3]) {
    float f[3] = {forward[0], forward[1], forward[2]};
    float fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (!(fl > 1e-4f)) f[0] = 0, f[1] = 1, f[2] = 0, fl = 1;
    for (float &v : f) v /= fl;
    // up made square to forward; the camera's "right" row is up x forward (screen left)
    float d = up[0] * f[0] + up[1] * f[1] + up[2] * f[2];
    float u[3] = {up[0] - d * f[0], up[1] - d * f[1], up[2] - d * f[2]};
    float ul = std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
    if (!(ul > 1e-4f)) {  // looking straight up or down: any up square to it
        u[0] = 0, u[1] = -f[2], u[2] = f[1];
        ul = std::sqrt(u[1] * u[1] + u[2] * u[2]);
        if (!(ul > 1e-4f)) u[0] = 0, u[1] = 0, u[2] = 1, ul = 1;
    }
    for (float &v : u) v /= ul;
    g_ear.right = {u[1] * f[2] - u[2] * f[1], u[2] * f[0] - u[0] * f[2], u[0] * f[1] - u[1] * f[0]};
    g_ear.forward = {f[0], f[1], f[2]};
    g_ear.up = {u[0], u[1], u[2]};
    g_ear.pos = {pos[0], pos[1], pos[2]};
    if (g_on) return;
    g_on = true;
    Log("ear: the game's sounds heard from the pilot at %.1f %.1f %.1f", pos[0], pos[1], pos[2]);
}

void clear() {
    if (!g_on) return;
    g_on = false;
    Log("ear: the game's sounds heard from the camera again");
}

void begin_service() {
    if (!g_on || g_swapped) return;
    vc::CMatrix &m = vc::camera_matrix();
    g_saved = m;
    m.right = g_ear.right, m.forward = g_ear.forward, m.up = g_ear.up, m.pos = g_ear.pos;
    g_swapped = true;
}

void end_service() {
    if (!g_swapped) return;
    vc::CMatrix &m = vc::camera_matrix();
    m.right = g_saved.right, m.forward = g_saved.forward, m.up = g_saved.up, m.pos = g_saved.pos;
    g_swapped = false;
}

}  // namespace ear
