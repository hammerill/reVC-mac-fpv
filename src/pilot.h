// Tommy at his piloting spot: while the drone flies he stands still in a
// pilot's pose (ped.ifp's IDLE_cam: a tourist holding a camera in both
// hands, an upper-body anim over his idle) holding a radio transmitter, a
// small procedural model drawn in his hands (model.cpp). While he stands in
// for the drone (standin.cpp) his real position is at the drone, so for the
// render passes of each frame he is put back at his spot and shown, then
// returned: the world keeps seeing "Tommy" at the drone, and the pilot still
// shows from the drone looking back home.
#pragma once

#include "vc.h"

namespace pilot {

// Every drone step while Tommy flies the drone on foot: the pose.
void hold(vc::CEntity *tommy);

// The flight is over: the pose fades out.
void release();

// Body stand-in on (home: Tommy's spot, his real position is at the drone) or off (null).
void away(const vc::CVector *home);

// Around the world's render passes: CRenderer::ScanWorld (start) and the
// end of RenderEffects.
void render_begin();
void render_end();

// After RenderScene: the radio in his hands.
void draw_radio();

}  // namespace pilot
