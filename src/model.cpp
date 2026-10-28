#include "model.h"

#include <cmath>
#include <cstring>
#include <vector>

#include "drone.h"
#include "fpvapi.h"
#include "log.h"
#include "vc.h"

namespace model {
namespace {

using vc::api;

// rw::gl3::Im3DVertex (24 bytes: position, RGBA, u, v).
struct Vertex {
    float x, y, z;
    uint8_t r, g, b, a;
    float u, v;
};
static_assert(sizeof(Vertex) == 24, "Im3DVertex layout");

// librw's values, as the game passes them (checked in the disassembly).
enum : int {
    kStateTextureRaster = 1, kStateZTest = 6, kStateZWrite = 8, kStateSrcBlend = 10, kStateDestBlend = 11,
    kStateVertexAlpha = 12, kStateFog = 14, kStateCull = 20,
};
constexpr uintptr_t kBlendSrcAlpha = 5, kBlendInvSrcAlpha = 6, kCullNone = 1;
constexpr int kPrimTriList = 3;
constexpr uint32_t kIm3DXyzRgba = 0x18;

struct Vec3 {
    float x, y, z;
};
Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator*(Vec3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
Vec3 vec(const float v[3]) { return {v[0], v[1], v[2]}; }

std::vector<Vertex> g_vertices;
std::vector<uint16_t> g_indices;
float g_light[3] = {1, 1, 1};

// The model is unlit: tint it like the game lights its objects this frame
// (time-of-day ambient plus some of the sun), so it isn't glowing at night.
void light_from_timecycle() {
    float amb[3] = {*api.AmbientRed, *api.AmbientGreen, *api.AmbientBlue};
    float dir[3] = {*api.DirectionalRed, *api.DirectionalGreen, *api.DirectionalBlue};
    for (int i = 0; i < 3; ++i) {
        float l = amb[i] + 0.5f * dir[i];
        g_light[i] = l < 0.08f ? 0.08f : l > 1.0f ? 1.0f : l;
    }
}

void push(Vec3 p, uint32_t argb, float k) {
    auto ch = [&](int shift, float light) {
        float v = float((argb >> shift) & 0xff) * k * light;
        return uint8_t(v > 255 ? 255 : v);
    };
    g_vertices.push_back({p.x, p.y, p.z, ch(16, g_light[0]), ch(8, g_light[1]), ch(0, g_light[2]),
                          uint8_t(argb >> 24), 0, 0});
}

// A box around c with half-extent axes a, b, h (h = its up), lit from above.
void box(Vec3 c, Vec3 a, Vec3 b, Vec3 h, uint32_t color) {
    Vec3 corner[8];
    for (int i = 0; i < 8; ++i)
        corner[i] = c + a * (i & 1 ? 1.f : -1.f) + b * (i & 2 ? 1.f : -1.f) + h * (i & 4 ? 1.f : -1.f);
    static const int faces[6][4] = {{4, 5, 7, 6}, {0, 2, 3, 1}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}};
    static const float shade[6] = {1.0f, 0.45f, 0.7f, 0.8f, 0.6f, 0.75f};
    for (int f = 0; f < 6; ++f) {
        uint16_t base = uint16_t(g_vertices.size());
        for (int k = 0; k < 4; ++k) push(corner[faces[f][k]], color, shade[f]);
        for (int k : {0, 1, 2, 0, 2, 3}) g_indices.push_back(uint16_t(base + k));
    }
}

// A flat disc (the props' blur) around c in the plane of a and b.
void disc(Vec3 c, Vec3 a, Vec3 b, float r, uint32_t color) {
    constexpr int kSegments = 20;
    uint16_t centre = uint16_t(g_vertices.size());
    push(c, color, 1.0f);
    for (int i = 0; i <= kSegments; ++i) {
        float t = 6.2831853f * float(i) / kSegments;
        push(c + a * (std::cos(t) * r) + b * (std::sin(t) * r), color, 1.0f);
        if (i) g_indices.insert(g_indices.end(), {centre, uint16_t(centre + i), uint16_t(centre + i + 1)});
    }
}

// A 5" freestyle quad: carbon X frame, motors, a red camera at the front,
// spinning props as translucent discs (two blades when they stand still).
void build(const drone::ModelPose &p) {
    g_vertices.clear();
    g_indices.clear();
    light_from_timecycle();
    Vec3 o = vec(p.pos), R = vec(p.right), U = vec(p.up), F = vec(p.forward);
    const uint32_t carbon = 0xff1c1c1f, motor = 0xff55555c, cam = 0xffa01818, battery = 0xff203868;
    box(o, R * 0.022f, F * 0.07f, U * 0.012f, carbon);                          // body
    box(o + U * 0.032f, R * 0.018f, F * 0.045f, U * 0.016f, battery);           // battery on top
    box(o + F * 0.075f + U * 0.008f, R * 0.012f, F * 0.012f, U * 0.012f, cam);  // camera
    for (int m = 0; m < 4; ++m) {
        Vec3 hub = vec(p.motor[m]);
        if (hub.x == 0 && hub.y == 0) hub = {m & 1 ? 0.08f : -0.08f, m & 2 ? -0.08f : 0.08f, 0};  // no data: plain X
        Vec3 h = o + R * hub.x + F * hub.y + U * hub.z;
        Vec3 mid = o + (R * hub.x + F * hub.y + U * hub.z) * 0.5f;
        float len = std::sqrt(hub.x * hub.x + hub.y * hub.y);
        Vec3 along = (R * hub.x + F * hub.y) * (0.5f / (len > 1e-4f ? len : 1));
        Vec3 across = (R * -hub.y + F * hub.x) * (1.0f / (len > 1e-4f ? len : 1));
        box(mid, along * len, across * 0.009f, U * 0.003f, carbon);  // arm
        box(h + U * 0.012f, R * 0.013f, F * 0.013f, U * 0.01f, motor);
        Vec3 prop = h + U * 0.025f;
        if (p.rpm[m] > 1000.0f) {
            disc(prop, R, F, 0.0635f, 0x50808088);
        } else {  // two blades
            Vec3 a = (R * 0.7071f + F * 0.7071f) * (m & 1 ? 1.f : -1.f), b = R * -0.7071f + F * 0.7071f;
            box(prop, a * 0.0635f, b * 0.006f, U * 0.0015f, 0xff404046);
        }
    }
}

struct SavedState {
    int states[8] = {kStateTextureRaster, kStateZTest, kStateZWrite, kStateSrcBlend,
                     kStateDestBlend, kStateVertexAlpha, kStateFog, kStateCull};
    uintptr_t values[8] = {};
    void save() {
        for (int i = 0; i < 8; ++i) api.RwRenderStateGet(states[i], &values[i]);
    }
    void restore() {
        for (int i = 0; i < 8; ++i) api.RwRenderStateSet(states[i], reinterpret_cast<void *>(values[i]));
    }
};

void set(int state, uintptr_t value) { api.RwRenderStateSet(state, reinterpret_cast<void *>(value)); }

}  // namespace

void draw() {
    drone::ModelPose pose;
    if (!fpvapi::loaded() || !drone::model_pose(&pose)) return;
    build(pose);

    SavedState saved;
    saved.save();
    set(kStateTextureRaster, 0);
    set(kStateZTest, 1);
    set(kStateZWrite, 1);
    set(kStateVertexAlpha, 1);
    set(kStateSrcBlend, kBlendSrcAlpha);
    set(kStateDestBlend, kBlendInvSrcAlpha);
    set(kStateFog, 0);
    set(kStateCull, kCullNone);
    if (api.RwIm3DTransform(g_vertices.data(), uint32_t(g_vertices.size()), nullptr, kIm3DXyzRgba)) {
        api.RwIm3DRenderIndexedPrimitive(kPrimTriList, g_indices.data(), int(g_indices.size()));
        api.RwIm3DEnd();
    }
    saved.restore();
    static int traced = 0;
    if (traced++ < 2)
        Log("model: %zu triangles, light %.2f %.2f %.2f", g_indices.size() / 3, g_light[0], g_light[1], g_light[2]);
}

}  // namespace model
