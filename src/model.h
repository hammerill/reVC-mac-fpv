// The drone as seen from the chase and line-of-sight cameras: a small
// procedural 5" quad drawn with the engine's Im3D, right after the world.
#pragma once

namespace model {

// After RenderScene: the world's depth buffer and camera are still bound.
void draw();

// The pilot's radio, a gamepad-style FPV transmitter, between his hands
// (each the middle of the hand and finger bones) and facing his head. Also
// after RenderScene.
void draw_radio(const float right_hand[3], const float left_hand[3], const float head[3]);

}  // namespace model
