// The air the drone flies through (libfpv's wind, docs/INTEGRATION.md "Wind"):
// the weather's wind, helicopters' rotor downwash and explosions' blasts.
#pragma once

namespace wind {

// A new flight: the explosions already going on are history.
void reset();

// Once per drone step, before fpv_drone_tick.
void update(float dt, const float drone_pos[3]);

}  // namespace wind
