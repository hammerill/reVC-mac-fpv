// World collision for libfpv through the game's own line-of-sight test.
#pragma once

#include <cstdint>

#include <libfpv.h>

#include "vc.h"

namespace world {

enum class Kind : uint32_t { None = 0, Ped = 1, Vehicle = 2, Object = 3, Other = 4 };

// What a probe hit, as libfpv's object_id: kind in the top 4 bits, the
// pool handle below (0 for buildings and other non-pool entities).
inline Kind kind_of(uint32_t object_id) { return Kind(object_id >> 28); }
inline uint32_t handle_of(uint32_t object_id) { return object_id & 0x0fffffffu; }

// Makes probes pass through this entity (Tommy). Null clears it.
void ignore(vc::CEntity *entity);

// fpv_raycast_fn. Game thread only (libfpv calls it inside fpv_drone_tick).
int raycast(void *user, const float origin[3], const float dir[3], float max_distance, fpv_raycast_hit *hit);

// The same probe, also returning the entity that was hit (may be null) and
// its surface type.
bool probe(const float origin[3], const float dir[3], float max_distance, fpv_raycast_hit *hit,
           vc::CEntity **entity = nullptr, uint8_t *surface = nullptr);

}  // namespace world
