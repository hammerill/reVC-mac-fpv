// libfpv builds with a warhead: the game's side of a detonation. Only built
// with -DREVC_FPV_KAMIKAZE=ON (local development builds); otherwise every
// call here is a no-op.
#pragma once

#include <libfpv.h>

namespace kamikaze {

#if defined(LIBFPV_KAMIKAZE)
// What happens after the blast, as the player set it in the overlay.
struct EndingChoice {
    uint32_t view = FPV_KAMIKAZE_VIEW_SIGNAL_LOST;
    float seconds = 3.0f;  // 0: no view, the "then" at once
    uint32_t then = FPV_KAMIKAZE_THEN_RESPAWN;
};
EndingChoice ending_choice();

// Once libfpv is loaded: tells it which endings the game offers. Game thread.
void init();

// Drops detonations of earlier drones.
void forget_detonations();

// The next detonation, if any.
bool poll(fpv_detonation *out);

// The game's explosion at the detonation point. Game thread.
void explode(const fpv_detonation &d);
#else
inline void init() {}
inline void forget_detonations() {}
#endif

}  // namespace kamikaze
