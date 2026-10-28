#include "sfx.h"

#include <chrono>

#include "log.h"
#include "world.h"

namespace sfx {
namespace {

using vc::api;

constexpr float kImpactSpeed = 3.0f;  // m/s: lighter touches and scrapes stay silent
constexpr int64_t kCooldownMs = 150;  // one sound per hit, not one per contact frame

int64_t g_last_impact = 0;

int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

}  // namespace

void play(vc::FrontendSound sound) { api.PlayFrontEndSound(api.DMAudio, sound, 0); }

void on_frame(const fpv_frame &frame) {
    const fpv_impact *hardest = nullptr;
    for (uint32_t i = 0; i < frame.impact_count && i < FPV_MAX_IMPACTS; ++i)
        if (!hardest || frame.impacts[i].speed > hardest->speed) hardest = &frame.impacts[i];
    if (!hardest || hardest->speed < kImpactSpeed || now_ms() - g_last_impact < kCooldownMs) return;

    // The game's collision sound wants the entity and its surface: probe the
    // contact again, from just off it, into it.
    const fpv_impact &im = *hardest;
    float origin[3], dir[3];
    for (int i = 0; i < 3; ++i) origin[i] = im.position[i] + im.normal[i] * 0.25f, dir[i] = -im.normal[i];
    fpv_raycast_hit hit{};
    vc::CEntity *entity = nullptr;
    uint8_t surface = 0;
    if (!world::probe(origin, dir, 0.5f, &hit, &entity, &surface) || !entity) return;
    g_last_impact = now_ms();
    // CPhysical::ApplyCollision reports the speed change per 1/50 s.
    api.ReportCollision(api.DMAudio, entity, entity, vc::kSurfacePlastic, surface, im.speed / 50.0f, 1.0f);
    static int reported = 0;
    if (reported++ < 20) Log("sfx: impact %.1f m/s on surface %u", im.speed, unsigned(surface));
}

}  // namespace sfx
