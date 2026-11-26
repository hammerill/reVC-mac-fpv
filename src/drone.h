// The FPV drone session: libfpv's drone and the game's camera.
#pragma once

#include <libfpv.h>

#include "vc.h"

namespace drone {

// Launches the drone in front of Tommy, or ends the flight. Game thread.
void toggle();

// Once per frame after CGame::Process: keys, overlay buttons, session state.
void tick();

// Right after CCamera::Process: steps the drone and, in flight, puts the
// game's camera (matrix, FOV, near plane) where the drone's camera is.
void camera();

// After the frame: the RwCamera view window the game built from our FOV.
void observe_view_window(float tan_half_y);

bool active();

// A drone view (FPV, chase, LOS) is on: the radio flies it and Tommy stands.
bool flying_view();

// F: back to Tommy, the drone left flying in the world (F7 goes back to it).
bool pilot_view();

// The game renders through the drone's camera (or the blast's) this frame.
bool camera_on();

// No game UI over the drone's picture: the HUD (radar, health, money, wanted
// level, zone and vehicle names, help boxes, timers) in the drone's views,
// and every other text (messages, subtitles, pickup and garage text) in the
// FPV view. Back in the pilot view and in the menus.
bool hide_hud();
bool hide_text();

// Where the world should stream and populate around (the drone), and where
// the pilot stands. False when not flying.
bool drone_position(vc::CVector *out);
bool pilot_position(vc::CVector *out);

// The frame for the FPV post-process and the OSD; false when no drone flies
// or a third-person camera is on. lost: no video signal (the goggles show static).
bool osd_frame(fpv_frame *out, bool *lost);

// What the drone model needs: the drone's pose and motors. False in the FPV
// view and when no drone is up.
struct ModelPose {
    float pos[3], right[3], up[3], forward[3];  // body axes, world
    float motor[4][3];                           // hubs, body-local (right, forward, up)
    float rpm[4];
    float prop_damage[4];                        // 1: the prop is gone
};
bool model_pose(ModelPose *out);

// Ends the flight with Tommy standing under the drone (F8 > Fly button, Y). Game thread.
void move_here();

// Back to the launch point (or in front of Tommy after the pilot view),
// repaired (R). Game thread.
void respawn();

// Next camera: FPV, chase, line of sight (C). Game thread.
void cycle_view();

// Host-injected radio channels (AETR, AUX..., -1..1), overriding devices for 0.5 s.
void inject_channels(const float *channels, uint32_t count);

// The game window is closing.
void shutdown();

// The drone's frame of this step, for the damage hooks (null without one).
const fpv_frame *frame();

}  // namespace drone
