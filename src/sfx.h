// Little reactions in the game's own sounds: menu sounds for the drone's
// actions, a collision sound at impacts.
#pragma once

#include <libfpv.h>

#include "vc.h"

namespace sfx {

// A frontend sound (launch, recall, camera switch, refused action). Game thread.
void play(vc::FrontendSound sound);

// Once per drone step: the hardest impact of the frame, if it was a real hit,
// sounds like the drone's plastic against what it hit.
void on_frame(const fpv_frame &frame);

}  // namespace sfx
