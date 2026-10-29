// The game's view code assumes an upright camera: the sky is a gradient
// above the screen's horizon line, and the world is gathered from the 2D
// triangle between the camera and two far corners of the view. With the
// drone's camera (any roll, upside down, straight down) both break: grey or
// missing sky, and ground or buildings left out of the frame. In flight these
// replace them with versions that work for any orientation.
#pragma once

#include <cstdint>

#include "vc.h"

namespace view {

// CClouds::RenderBackground in flight: the sky and the distant ground, by each
// screen point's elevation. False when the game should draw its own.
bool sky(int16_t top_r, int16_t top_g, int16_t top_b, int16_t bottom_r, int16_t bottom_g, int16_t bottom_b,
         int16_t alpha);

// CClouds::RenderHorizon is part of sky() in flight: true to skip it.
bool skip_horizon();

// CRenderer::ScanWorld's sector polygon in flight: the outline on the ground
// of the whole view frustum (to the same depth the game used), instead of
// its triangle. `game` is the game's polygon (its first point is the camera),
// `priority` the narrow priority scan. Returns the point count (counter-
// clockwise, as ScanSectorPoly wants), 0 to keep the game's.
int sector_polygon(const vc::RwV2d *game, bool priority, vc::RwV2d out[5]);

}  // namespace view
