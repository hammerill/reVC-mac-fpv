// What breaks the props besides libfpv's own hits (docs/INTEGRATION.md
// "Damage"): water (which also holds the drone back: it isn't solid), fire,
// explosions, a turning helicopter rotor, bullets through the props or the
// body, and what the game does to Tommy while he stands in for the drone.
#pragma once

#include <libfpv.h>

#include "vc.h"

namespace damage {

// Once per drone step, with the frame just ticked.
void update(fpv_drone *drone, const fpv_frame &frame, float dt);

// The flight ended (or a new one starts).
void reset();

// CWeapon::DoBulletImpact: one instant-hit bullet from `from` to `to` (where
// it stopped). Game thread.
void shot(vc::CEntity *owner, const vc::CVector &from, const vc::CVector &to);

// The game took `amount` health from Tommy while he stands in at the drone.
void stand_in_hit(float amount);

}  // namespace damage
