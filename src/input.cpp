#include "input.h"

#include <GLFW/glfw3.h>

#include <string>

#include "drone.h"
#include "fpvapi.h"
#include "vc.h"

namespace input {
namespace {

using fpvapi::fpv;

bool g_pressed[GLFW_KEY_LAST + 1];
bool g_down[GLFW_KEY_LAST + 1];

// GLFW key tokens name physical keys (US layout positions), which is what
// libfpv wants for letters: W/S/A/D stay under the left hand on AZERTY.
int to_fpv(int key) {
    if ((key >= GLFW_KEY_A && key <= GLFW_KEY_Z) || (key >= GLFW_KEY_0 && key <= GLFW_KEY_9)) return key;
    if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F12) return FPV_KEY_F1 + (key - GLFW_KEY_F1);
    switch (key) {
        case GLFW_KEY_SPACE: return FPV_KEY_SPACE;
        case GLFW_KEY_ESCAPE: return FPV_KEY_ESCAPE;
        case GLFW_KEY_ENTER:
        case GLFW_KEY_KP_ENTER: return FPV_KEY_ENTER;
        case GLFW_KEY_TAB: return FPV_KEY_TAB;
        case GLFW_KEY_BACKSPACE: return FPV_KEY_BACKSPACE;
        case GLFW_KEY_INSERT: return FPV_KEY_INSERT;
        case GLFW_KEY_DELETE: return FPV_KEY_DELETE;
        case GLFW_KEY_RIGHT: return FPV_KEY_RIGHT;
        case GLFW_KEY_LEFT: return FPV_KEY_LEFT;
        case GLFW_KEY_DOWN: return FPV_KEY_DOWN;
        case GLFW_KEY_UP: return FPV_KEY_UP;
        case GLFW_KEY_PAGE_UP: return FPV_KEY_PAGE_UP;
        case GLFW_KEY_PAGE_DOWN: return FPV_KEY_PAGE_DOWN;
        case GLFW_KEY_HOME: return FPV_KEY_HOME;
        case GLFW_KEY_END: return FPV_KEY_END;
        case GLFW_KEY_LEFT_SHIFT: return FPV_KEY_LSHIFT;
        case GLFW_KEY_RIGHT_SHIFT: return FPV_KEY_RSHIFT;
        case GLFW_KEY_LEFT_CONTROL: return FPV_KEY_LCTRL;
        case GLFW_KEY_RIGHT_CONTROL: return FPV_KEY_RCTRL;
        case GLFW_KEY_LEFT_ALT: return FPV_KEY_LALT;
        case GLFW_KEY_RIGHT_ALT: return FPV_KEY_RALT;
        default: return FPV_KEY_NONE;
    }
}

}  // namespace

bool overlay_has_input() { return fpvapi::loaded() && fpv.fpv_overlay_wants_input(); }

bool on_key(int key, int action) {
    if (key < 0 || key > GLFW_KEY_LAST || action == GLFW_REPEAT) return !overlay_has_input();
    bool down = action == GLFW_PRESS;
    bool was_overlay = overlay_has_input();
    if (down && !g_down[key]) g_pressed[key] = !was_overlay;
    g_down[key] = down;
    if (fpvapi::loaded())
        if (int k = to_fpv(key)) fpv.fpv_input_key(k, down);

    // Releases always reach the game, so nothing held when the overlay
    // opened (or a flight started) stays stuck.
    if (!down) return true;
    if (was_overlay || overlay_has_input()) return false;
    // In flight the keyboard flies the drone; the game only gets its pause
    // menu (and everything while that menu is open).
    if (drone::active() && !vc::paused() && key != GLFW_KEY_ESCAPE) return false;
    return true;
}

void on_char(unsigned int codepoint) {
    if (!overlay_has_input() || codepoint < 32) return;
    char utf8[5] = {};
    if (codepoint < 0x80) {
        utf8[0] = char(codepoint);
    } else if (codepoint < 0x800) {
        utf8[0] = char(0xc0 | codepoint >> 6), utf8[1] = char(0x80 | (codepoint & 0x3f));
    } else if (codepoint < 0x10000) {
        utf8[0] = char(0xe0 | codepoint >> 12), utf8[1] = char(0x80 | ((codepoint >> 6) & 0x3f));
        utf8[2] = char(0x80 | (codepoint & 0x3f));
    } else {
        utf8[0] = char(0xf0 | codepoint >> 18), utf8[1] = char(0x80 | ((codepoint >> 12) & 0x3f));
        utf8[2] = char(0x80 | ((codepoint >> 6) & 0x3f)), utf8[3] = char(0x80 | (codepoint & 0x3f));
    }
    fpv.fpv_overlay_text(utf8);
}

void on_mouse_button(int button, int action) {
    if (fpvapi::loaded() && button >= 0 && button <= 2)
        fpv.fpv_overlay_mouse_button(button == GLFW_MOUSE_BUTTON_RIGHT ? 1 : button == GLFW_MOUSE_BUTTON_MIDDLE ? 2 : 0,
                                     action == GLFW_PRESS);
}

void on_cursor(double x, double y) {
    if (fpvapi::loaded()) fpv.fpv_overlay_mouse_move(float(x), float(y));
}

void on_scroll(double dx, double dy) {
    if (overlay_has_input()) fpv.fpv_overlay_mouse_wheel(float(dx), float(dy));
}

void on_focus(bool focused) {
    if (focused) return;
    // Keys released while another app has focus never reach us.
    for (int key = 0; key <= GLFW_KEY_LAST; ++key)
        if (g_down[key]) {
            g_down[key] = false;
            if (fpvapi::loaded())
                if (int k = to_fpv(key)) fpv.fpv_input_key(k, 0);
        }
}

bool take_press(int key) {
    bool p = g_pressed[key];
    g_pressed[key] = false;
    return p;
}

}  // namespace input
