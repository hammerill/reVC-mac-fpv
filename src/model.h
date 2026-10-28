// The drone as seen from the chase and line-of-sight cameras: a small
// procedural 5" quad drawn with the engine's Im3D, right after the world.
#pragma once

namespace model {

// After RenderScene: the world's depth buffer and camera are still bound.
void draw();

}  // namespace model
