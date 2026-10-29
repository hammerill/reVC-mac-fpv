// The world reacts to the drone as if it were Tommy (as gtasa-fpv's CJ):
// once the drone is a few metres from his spot, Tommy himself goes along
// with it, hidden, frozen, collision-less and unhurtable, so pickups, the
// doors of interiors (script triggers on the player), peds, police, traffic,
// population and streaming all see "Tommy" right where the quad is. Back
// close to his spot, he goes home. In a vehicle he stays put and only the
// FindPlayer* queries follow the drone. He still shows at his spot
// (pilot.cpp: moved home for the render passes).
#pragma once

#include "vc.h"

namespace standin {

// Once, at startup: the hooks it relies on are in (else it never starts).
void set_available(bool available);

// Every drone step with the drone's position.
void update(const float drone_pos[3]);

// Ends it: Tommy back to his spot (and the area he was in) or, go_home
// false, left at the drone.
void end(bool go_home);

// Tommy stands in (or, in a vehicle, the queries follow the drone).
bool on();

// Where Tommy really is (his spot while he stands in), for the GPS home.
vc::CVector home();

// Hooks' questions: what FindPlayerCoors answers (false: the game's own),
// and whether this ped is Tommy standing in (not hurt, not processed).
bool coors(vc::CVector *out);
bool standing_in(vc::CEntity *ped);

}  // namespace standin
