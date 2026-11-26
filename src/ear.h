// The game's own sounds heard from the pilot (libfpv's
// FPV_DRONE_HEARD_FROM_PILOT): Vice City positions every sound relative to
// TheCamera's matrix (cAudioManager::TranslateEntity, GetDistanceSquared and
// reads inlined all over its audio code), and all of that runs inside
// cDMAudio::Service. So for that call TheCamera's matrix is swapped for the
// pilot's head, and put back right after: traffic, peds, gunfire and
// explosions are heard from where the pilot stands, as for a real pilot.
#pragma once

namespace ear {

// Heard from here (host coords) until clear(). Game thread.
void set(const float pos[3], const float forward[3], const float up[3]);
void clear();

// Around cDMAudio::Service (plugin.cpp): swaps TheCamera's matrix in and out.
void begin_service();
void end_service();

}  // namespace ear
