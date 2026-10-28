#include "kamikaze.h"

#include <cmath>

#include "fpvapi.h"
#include "log.h"
#include "vc.h"

namespace kamikaze {
namespace {

using fpvapi::fpv;
using vc::api;

bool g_ready = false;

}  // namespace

EndingChoice ending_choice() {
    EndingChoice e;
    if (!fpvapi::kamikaze()) return e;
    fpv_kamikaze_config kc{};
    kc.struct_size = sizeof kc;
    kc.ending_duration = e.seconds;
    kc.ending_view = e.view;
    kc.ending_then = e.then;
    if (fpv.fpv_kamikaze_get_config(&kc) != FPV_OK) return e;
    e.view = kc.ending_view == FPV_KAMIKAZE_VIEW_CUTSCENE ? uint32_t(FPV_KAMIKAZE_VIEW_CUTSCENE)
                                                          : uint32_t(FPV_KAMIKAZE_VIEW_SIGNAL_LOST);
    e.seconds = kc.ending_duration >= 0 && kc.ending_duration <= 30 ? kc.ending_duration : 3.0f;
    e.then = kc.ending_then <= FPV_KAMIKAZE_THEN_NOTHING ? kc.ending_then : uint32_t(FPV_KAMIKAZE_THEN_RESPAWN);
    return e;
}

void init() {
    if (g_ready || !fpvapi::kamikaze()) return;
    g_ready = true;
    if (fpv.fpv_kamikaze_set_ending_support) {
        const uint32_t views = (1u << FPV_KAMIKAZE_VIEW_SIGNAL_LOST) | (1u << FPV_KAMIKAZE_VIEW_CUTSCENE);
        const uint32_t thens =
            (1u << FPV_KAMIKAZE_THEN_RESPAWN) | (1u << FPV_KAMIKAZE_THEN_RECALL) | (1u << FPV_KAMIKAZE_THEN_NOTHING);
        fpv_result r = fpv.fpv_kamikaze_set_ending_support(views, thens, FPV_KAMIKAZE_VIEW_SIGNAL_LOST,
                                                           FPV_KAMIKAZE_THEN_RESPAWN);
        if (r != FPV_OK) Log("fpv_kamikaze_set_ending_support: %s", fpv.fpv_result_string(r));
    }
    EndingChoice e = ending_choice();
    Log("kamikaze: after the blast: %s for %.1f s, then %u",
        e.view == FPV_KAMIKAZE_VIEW_CUTSCENE ? "cutscene" : "signal lost", e.seconds, e.then);
}

void forget_detonations() {
    fpv_detonation d;
    while (poll(&d)) {}
}

bool poll(fpv_detonation *out) {
    if (!fpvapi::kamikaze()) return false;
    out->struct_size = sizeof *out;
    return fpv.fpv_kamikaze_poll_detonation(out) != 0;
}

void explode(const fpv_detonation &d) {
    static const char *causes[] = {"impact", "explode switch", "game"};
    // A 5" strike quad carries a few hundred grams of explosive: grenade-sized.
    // Tommy set it off, as far as the world is concerned.
    bool added = api.AddExplosion(nullptr, api.FindPlayerPed(), vc::kExplosionGrenade,
                                  {d.position[0], d.position[1], d.position[2]}, 0, true);
    Log("kamikaze: detonation (%s) at %.2f %.2f %.2f speed %.1f object %08x, explosion %s",
        causes[unsigned(d.cause) < 3 ? d.cause : 2], d.position[0], d.position[1], d.position[2],
        std::sqrt(d.velocity[0] * d.velocity[0] + d.velocity[1] * d.velocity[1] + d.velocity[2] * d.velocity[2]),
        d.object_id, added ? "added" : "refused");
}

}  // namespace kamikaze
