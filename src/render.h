// What the mod draws over the finished game frame (OpenGL 3, from the
// glfwSwapBuffers hook): the fisheye lens and analog noise of the FPV view,
// the OSD, and libfpv's overlay.
#pragma once

namespace render {

// Once per presented frame, before the swap. fb_*: framebuffer pixels,
// win_*: window points (the overlay and the mouse use those).
void frame(int fb_w, int fb_h, int win_w, int win_h);

// Width / height of the game's framebuffer.
float aspect();

// Frees GL objects (the game window is going away).
void release();

}  // namespace render
