// Keyboard and mouse from the game window's GLFW callbacks (plugin.cpp):
// forwarded to libfpv, and the mod's own keys as presses for the game thread.
#pragma once

namespace input {

// A GLFW key event. Returns false when the game must not see it.
bool on_key(int key, int action);
void on_char(unsigned int codepoint);
void on_mouse_button(int button, int action);
void on_cursor(double x, double y);
void on_scroll(double dx, double dy);
void on_focus(bool focused);

// A press of this GLFW key since the last call (and the overlay wasn't typing).
bool take_press(int key);

// The overlay has the keyboard and mouse: the game gets nothing.
bool overlay_has_input();

}  // namespace input
