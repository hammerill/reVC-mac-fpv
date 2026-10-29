// Entry point of reVC-fpv, an FPV drone for GTA Vice City on macOS (reVC),
// powered by libfpv.
//
// The game's launcher script (Contents/MacOS/reVC) loads this library through
// DYLD_INSERT_LIBRARIES. From there:
//
//  - dyld interposing of the GLFW functions the game imports: the key, mouse
//    and focus callbacks go through input.cpp (libfpv's keys, the mod's keys,
//    and nothing for the game while the overlay is open), and
//    glfwSwapBuffers draws the lens, the OSD and the overlay on the frame.
//  - inline detours (hook.cpp) on game functions, found by their exported
//    symbols: the game loop, the camera, the world render, the HUD and text,
//    and where the world streams in and populates.


#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "autotest.h"
#include "drone.h"
#include "hook.h"
#include "input.h"
#include "log.h"
#include "model.h"
#include "paths.h"
#include "pilot.h"
#include "render.h"
#include "standin.h"
#include "vc.h"
#include "view.h"

#define DYLD_INTERPOSE(replacement, replacee)                                                                    \
    __attribute__((used)) static struct {                                                                        \
        const void *replacement_;                                                                                \
        const void *replacee_;                                                                                   \
    } interpose_##replacee __attribute__((section("__DATA,__interpose"))) = {(const void *)&replacement,        \
                                                                             (const void *)&replacee};

namespace {

using vc::api;

bool g_game_ok = false;  // the executable matched and the hooks are in

// --- game detours -----------------------------------------------------------------

struct Original {
    void (*game_process)();
    void (*camera_process)(void *);
    void (*render_scene)();
    void (*hud_draw)();
    void (*hud_after_fade)();
    void (*messages_display)();
    void (*darkel_messages)();
    void (*garage_messages)();
    void (*pickup_text)();
    void (*radio_name)(void *);
    void (*load_collision)(const float *);
    const vc::CVector *(*centre_of_world)(int);
    const vc::CVector *(*centre_no_sniper)();
    vc::CVector (*find_coors)();
    void (*render_background)(int16_t, int16_t, int16_t, int16_t, int16_t, int16_t, int16_t);
    void (*render_horizon)();
    void (*scan_world)();
    void (*scan_sector_poly)(vc::RwV2d *, int, void (*)(void *));
    void (*render_effects)();
    void (*process_control)(vc::CEntity *);
    bool (*ped_damage)(vc::CEntity *, vc::CEntity *, int, float, int, uint8_t);
};
Original g;

vc::CVector g_centre;  // what the centre-of-world queries return in flight
bool g_scanning = false;  // inside CRenderer::ScanWorld

// CGame::Process: the world went one step; keys and the session follow.
void game_process_hook() {
    g.game_process();
    drone::tick();
    autotest::tick();
}

// CCamera::Process: the game built its camera; in flight ours replaces it.
void camera_process_hook(void *camera) {
    g.camera_process(camera);
    drone::camera();
}

// RenderScene: the world is drawn, the drone model and the pilot's radio go in with it.
void render_scene_hook() {
    g.render_scene();
    model::draw();
    pilot::draw_radio();
}

// CRenderer::ScanWorld gathers what this frame draws: Tommy goes home for
// the render passes first, and the sectors come from the whole frustum.
void scan_world_hook() {
    pilot::render_begin();
    g_scanning = true;
    g.scan_world();
    g_scanning = false;
}
void scan_sector_poly_hook(vc::RwV2d *poly, int count, void (*scan)(void *)) {
    vc::RwV2d outline[5];
    int n = g_scanning && count == 3 ? view::sector_polygon(poly, scan == api.ScanSectorList_Priority, outline) : 0;
    if (n) return g.scan_sector_poly(outline, n, scan);
    g.scan_sector_poly(poly, count, scan);
}

// RenderEffects: the last of the world's passes; Tommy back to the drone.
void render_effects_hook() {
    g.render_effects();
    pilot::render_end();
}

// The sky, for a camera that may be rolled or upside down.
void render_background_hook(int16_t tr, int16_t tg, int16_t tb, int16_t br, int16_t bg, int16_t bb, int16_t a) {
    if (!view::sky(tr, tg, tb, br, bg, bb, a)) g.render_background(tr, tg, tb, br, bg, bb, a);
}
void render_horizon_hook() {
    if (!view::skip_horizon()) g.render_horizon();
}

// Tommy standing in at the drone: frozen (no gravity, no tasks) and unhurt
// by whatever goes for him there (fists, bullets, cars, blasts, fire).
void process_control_hook(vc::CEntity *ped) {
    if (standin::standing_in(ped)) {
        vc::move_speed(ped) = vc::turn_speed(ped) = {0, 0, 0};
        return;
    }
    g.process_control(ped);
}
bool ped_damage_hook(vc::CEntity *ped, vc::CEntity *by, int weapon, float damage, int piece, uint8_t direction) {
    if (standin::standing_in(ped)) {
        static int reported = 0;
        if (reported++ < 20) Log("standin: Tommy hit at the drone (weapon %d, %.1f damage), ignored", weapon, damage);
        return false;
    }
    return g.ped_damage(ped, by, weapon, damage, piece, direction);
}

// The HUD, radar, radio station name and pickup text would cover the FPV
// feed; with the pilot's OSD disable switch on, every message goes too.
void hud_draw_hook() {
    if (!drone::hide_hud()) g.hud_draw();
}
void radio_name_hook(void *music_manager) {
    if (!drone::hide_hud()) g.radio_name(music_manager);
}
void pickup_text_hook() {
    if (!drone::hide_hud()) g.pickup_text();
}
void hud_after_fade_hook() {
    if (!drone::hide_text()) g.hud_after_fade();
}
void messages_display_hook() {
    if (!drone::hide_text()) g.messages_display();
}
void darkel_messages_hook() {
    if (!drone::hide_text()) g.darkel_messages();
}
void garage_messages_hook() {
    if (!drone::hide_text()) g.garage_messages();
}

// Collision streams in around the drone, and stays at Tommy's spot as the
// second point, as for VC's own RC toys. (EnsureCollisionIsInMemory, right
// after, loads at FindPlayerCoors: the drone too once he stands in. Before
// that the drone is within a few metres of him.)
void load_collision_hook(const float *pos) {
    vc::CVector d, home;
    if (!drone::drone_position(&d)) return g.load_collision(pos);
    if (drone::pilot_position(&home)) {
        float second[2] = {home.x, home.y};
        api.AddCollisionNeededAtPosn(second);
    }
    float at[2] = {d.x, d.y};
    g.load_collision(at);
}

// Zones, streaming, pickups' respawns, police: "the player" is at the drone
// while Tommy stands in (or the queries follow it from his vehicle).
vc::CVector find_coors_hook() {
    vc::CVector p;
    if (standin::coors(&p)) return p;
    return g.find_coors();
}

// Peds and traffic are created and removed around the drone.
const vc::CVector *centre_of_world_hook(int player) {
    if (drone::drone_position(&g_centre)) return &g_centre;
    return g.centre_of_world(player);
}
const vc::CVector *centre_no_sniper_hook() {
    if (drone::drone_position(&g_centre)) return &g_centre;
    return g.centre_no_sniper();
}

void install_game_hooks() {
    std::string error;
    if (!vc::init(error)) {
        Log("%s", error.c_str());
        Log("the FPV drone is off");
        return;
    }
#define HOOK(field, target, fn) g.field = hook::detour(reinterpret_cast<void *>(api.target), &fn, #target)
    HOOK(game_process, CGame_Process, game_process_hook);
    HOOK(camera_process, CCamera_Process, camera_process_hook);
    HOOK(render_scene, RenderScene, render_scene_hook);
    HOOK(hud_draw, CHud_Draw, hud_draw_hook);
    HOOK(hud_after_fade, CHud_DrawAfterFade, hud_after_fade_hook);
    HOOK(messages_display, CMessages_Display, messages_display_hook);
    HOOK(darkel_messages, CDarkel_DrawMessages, darkel_messages_hook);
    HOOK(garage_messages, CGarages_PrintMessages, garage_messages_hook);
    HOOK(pickup_text, CPickups_RenderPickUpText, pickup_text_hook);
    HOOK(radio_name, DisplayRadioStationName, radio_name_hook);
    HOOK(load_collision, CColStore_LoadCollision, load_collision_hook);
    HOOK(centre_of_world, FindPlayerCentreOfWorld, centre_of_world_hook);
    HOOK(centre_no_sniper, FindPlayerCentreOfWorld_NoSniperShift, centre_no_sniper_hook);
    HOOK(find_coors, FindPlayerCoors, find_coors_hook);
    HOOK(render_background, CClouds_RenderBackground, render_background_hook);
    HOOK(render_horizon, CClouds_RenderHorizon, render_horizon_hook);
    HOOK(scan_world, CRenderer_ScanWorld, scan_world_hook);
    HOOK(scan_sector_poly, CRenderer_ScanSectorPoly, scan_sector_poly_hook);
    HOOK(render_effects, RenderEffects, render_effects_hook);
    HOOK(process_control, CPlayerPed_ProcessControl, process_control_hook);
    HOOK(ped_damage, PedInflictDamage, ped_damage_hook);
#undef HOOK
    // The loop and the camera are essential; the rest only hide or stream things.
    // The stand-in needs all of its hooks, or Tommy could fall or get hurt at the drone.
    g_game_ok = g.game_process && g.camera_process;
    standin::set_available(g.find_coors && g.process_control && g.ped_damage && g.scan_world && g.render_effects);
    Log("hooks: loop %d camera %d scene %d hud %d/%d/%d text %d/%d/%d/%d streaming %d/%d/%d/%d sky %d/%d scan %d/%d "
        "effects %d standin %d/%d",
        !!g.game_process, !!g.camera_process, !!g.render_scene, !!g.hud_draw, !!g.radio_name, !!g.pickup_text,
        !!g.hud_after_fade, !!g.messages_display, !!g.darkel_messages, !!g.garage_messages, !!g.load_collision,
        !!g.centre_of_world, !!g.centre_no_sniper, !!g.find_coors, !!g.render_background, !!g.render_horizon,
        !!g.scan_world, !!g.scan_sector_poly, !!g.render_effects, !!g.process_control, !!g.ped_damage);
    if (!g_game_ok) Log("the FPV drone is off");
}

// --- GLFW: input and the frame ------------------------------------------------------

GLFWwindow *g_window = nullptr;

// Callbacks the game (or another plugin) registered; ours sit in front of them.
GLFWkeyfun g_key_cb = nullptr;
GLFWcharfun g_char_cb = nullptr;
GLFWmousebuttonfun g_button_cb = nullptr;
GLFWcursorposfun g_cursor_cb = nullptr;
GLFWscrollfun g_scroll_cb = nullptr;
GLFWwindowfocusfun g_focus_cb = nullptr;
GLFWcursorenterfun g_enter_cb = nullptr;

// The game reads mouse movement as cursor position deltas. While the overlay
// has the mouse the game sees a frozen position; afterwards an offset hides
// how far the real cursor travelled, so the camera doesn't jump.
int g_game_cursor_mode = GLFW_CURSOR_NORMAL;
bool g_cursor_free = false;
double g_frozen_x = 0, g_frozen_y = 0;
double g_offset_x = 0, g_offset_y = 0;

bool blocked() { return g_cursor_free; }

void free_cursor(bool free) {
    if (free == g_cursor_free || !g_window) return;
    double x, y;
    glfwGetCursorPos(g_window, &x, &y);
    if (free) {
        g_frozen_x = x - g_offset_x;
        g_frozen_y = y - g_offset_y;
        glfwSetInputMode(g_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        input::on_cursor(x, y);
    } else {
        glfwSetInputMode(g_window, GLFW_CURSOR, g_game_cursor_mode);
        glfwGetCursorPos(g_window, &x, &y);
        g_offset_x = x - g_frozen_x;
        g_offset_y = y - g_frozen_y;
    }
    g_cursor_free = free;
}

void on_key(GLFWwindow *w, int key, int scancode, int action, int mods) {
    bool pass = input::on_key(key, action);
    free_cursor(input::overlay_has_input());
    if (pass && g_key_cb) g_key_cb(w, key, scancode, action, mods);
}

void on_char(GLFWwindow *w, unsigned int codepoint) {
    input::on_char(codepoint);
    if (!blocked() && g_char_cb) g_char_cb(w, codepoint);
}

void on_mouse_button(GLFWwindow *w, int button, int action, int mods) {
    input::on_mouse_button(button, action);
    if ((!blocked() || action == GLFW_RELEASE) && g_button_cb) g_button_cb(w, button, action, mods);
}

void on_cursor(GLFWwindow *w, double x, double y) {
    input::on_cursor(x, y);
    if (!blocked() && g_cursor_cb) g_cursor_cb(w, x - g_offset_x, y - g_offset_y);
}

void on_scroll(GLFWwindow *w, double dx, double dy) {
    input::on_scroll(dx, dy);
    if (!blocked() && g_scroll_cb) g_scroll_cb(w, dx, dy);
}

void on_focus(GLFWwindow *w, int focused) {
    input::on_focus(focused != 0);
    if (g_focus_cb) g_focus_cb(w, focused);
}

void on_cursor_enter(GLFWwindow *w, int entered) {
    if (g_enter_cb) g_enter_cb(w, entered);
}

// Testing without hands: fpv-data/keys.txt lists keys ("down enter wait
// f7 c"), typed into the game window one after another, as if pressed. The
// file is deleted when it is read.
void scripted_keys() {
    static std::vector<int> queue;
    static size_t next = 0;
    static int frames = 0;
    ++frames;
    if (next >= queue.size()) {
        if (frames % 30) return;
        std::string path = paths::data_dir() + "/keys.txt";
        FILE *f = std::fopen(path.c_str(), "r");
        if (!f) return;
        queue.clear();
        next = 0;
        static const struct { const char *name; int key; } names[] = {
            {"enter", GLFW_KEY_ENTER}, {"esc", GLFW_KEY_ESCAPE}, {"up", GLFW_KEY_UP}, {"down", GLFW_KEY_DOWN},
            {"left", GLFW_KEY_LEFT}, {"right", GLFW_KEY_RIGHT}, {"space", GLFW_KEY_SPACE}, {"f7", GLFW_KEY_F7},
            {"f8", GLFW_KEY_F8}, {"wait", 0}};
        char word[32];
        while (std::fscanf(f, "%31s", word) == 1) {
            int key = -1;
            for (const auto &n : names)
                if (!std::strcmp(word, n.name)) key = n.key;
            if (key < 0 && word[1] == 0 && word[0] >= 'a' && word[0] <= 'z') key = GLFW_KEY_A + (word[0] - 'a');
            if (key >= 0) queue.push_back(key);
        }
        std::fclose(f);
        std::remove(path.c_str());
        Log("keys.txt: %zu keys", queue.size());
        frames = 0;
        return;
    }
    // Held for 6 frames, then 12 frames before the next one.
    int key = queue[next];
    if (frames == 1 && key) on_key(g_window, key, 0, GLFW_PRESS, 0);
    if (frames == 7 && key) on_key(g_window, key, 0, GLFW_RELEASE, 0);
    if (frames >= 18) ++next, frames = 0;
}

void frame() {
    if (!g_game_ok) return;
    scripted_keys();
    free_cursor(input::overlay_has_input());
    if (void *cam = vc::scene_camera())
        if (const float *vw = api.RwCameraGetViewWindow(cam)) drone::observe_view_window(vw[1]);
    int fbw = 0, fbh = 0, ww = 0, wh = 0;
    glfwGetFramebufferSize(g_window, &fbw, &fbh);
    glfwGetWindowSize(g_window, &ww, &wh);
    render::frame(fbw, fbh, ww, wh);
}

// --- interposed GLFW functions ------------------------------------------------------
// Calls to glfw* from inside this library reach the real functions; dyld only
// redirects the calls of other images (the game, other plugins).

GLFWwindow *Hook_glfwCreateWindow(int width, int height, const char *title, GLFWmonitor *monitor, GLFWwindow *share) {
    GLFWwindow *window = glfwCreateWindow(width, height, title, monitor, share);
    if (window && !g_window) {
        g_window = window;
        glfwSetKeyCallback(window, on_key);
        glfwSetCharCallback(window, on_char);
        glfwSetMouseButtonCallback(window, on_mouse_button);
        glfwSetCursorPosCallback(window, on_cursor);
        glfwSetScrollCallback(window, on_scroll);
        glfwSetWindowFocusCallback(window, on_focus);
        glfwSetCursorEnterCallback(window, on_cursor_enter);
    }
    return window;
}
DYLD_INTERPOSE(Hook_glfwCreateWindow, glfwCreateWindow)

void Hook_glfwDestroyWindow(GLFWwindow *window) {
    if (window && window == g_window) {
        drone::shutdown();
        render::release();
        g_window = nullptr;
        g_cursor_free = false;
        g_key_cb = nullptr, g_char_cb = nullptr, g_button_cb = nullptr, g_cursor_cb = nullptr;
        g_scroll_cb = nullptr, g_focus_cb = nullptr, g_enter_cb = nullptr;
    }
    glfwDestroyWindow(window);
}
DYLD_INTERPOSE(Hook_glfwDestroyWindow, glfwDestroyWindow)

void Hook_glfwSwapBuffers(GLFWwindow *window) {
    if (window && window == g_window) frame();
    glfwSwapBuffers(window);
}
DYLD_INTERPOSE(Hook_glfwSwapBuffers, glfwSwapBuffers)

// Registers the caller's callback behind ours. Returns the previous one, as
// the real setter would.
#define CHAIN_CALLBACK(Type, Setter, Stored)              \
    Type Hook_##Setter(GLFWwindow *window, Type callback) { \
        if (window != g_window) return Setter(window, callback); \
        Type previous = Stored;                           \
        Stored = callback;                                \
        return previous;                                  \
    }                                                     \
    DYLD_INTERPOSE(Hook_##Setter, Setter)

CHAIN_CALLBACK(GLFWkeyfun, glfwSetKeyCallback, g_key_cb)
CHAIN_CALLBACK(GLFWcharfun, glfwSetCharCallback, g_char_cb)
CHAIN_CALLBACK(GLFWmousebuttonfun, glfwSetMouseButtonCallback, g_button_cb)
CHAIN_CALLBACK(GLFWcursorposfun, glfwSetCursorPosCallback, g_cursor_cb)
CHAIN_CALLBACK(GLFWscrollfun, glfwSetScrollCallback, g_scroll_cb)
CHAIN_CALLBACK(GLFWwindowfocusfun, glfwSetWindowFocusCallback, g_focus_cb)
CHAIN_CALLBACK(GLFWcursorenterfun, glfwSetCursorEnterCallback, g_enter_cb)

void Hook_glfwSetInputMode(GLFWwindow *window, int mode, int value) {
    if (window == g_window && mode == GLFW_CURSOR) {
        // The game switching cursor modes (its pause menu) resets the
        // relationship between real and reported positions.
        g_game_cursor_mode = value;
        g_offset_x = g_offset_y = 0;
        if (g_cursor_free) return;
    }
    glfwSetInputMode(window, mode, value);
}
DYLD_INTERPOSE(Hook_glfwSetInputMode, glfwSetInputMode)

void Hook_glfwGetCursorPos(GLFWwindow *window, double *x, double *y) {
    double rx, ry;
    if (window == g_window && g_cursor_free) {
        rx = g_frozen_x, ry = g_frozen_y;
    } else {
        glfwGetCursorPos(window, &rx, &ry);
        if (window == g_window) rx -= g_offset_x, ry -= g_offset_y;
    }
    if (x) *x = rx;
    if (y) *y = ry;
}
DYLD_INTERPOSE(Hook_glfwGetCursorPos, glfwGetCursorPos)

void Hook_glfwSetCursorPos(GLFWwindow *window, double x, double y) {
    if (window == g_window && g_cursor_free) {
        g_frozen_x = x, g_frozen_y = y;
        return;
    }
    if (window == g_window) x += g_offset_x, y += g_offset_y;
    glfwSetCursorPos(window, x, y);
}
DYLD_INTERPOSE(Hook_glfwSetCursorPos, glfwSetCursorPos)

int Hook_glfwGetMouseButton(GLFWwindow *window, int button) {
    if (window == g_window && g_cursor_free) return GLFW_RELEASE;
    return glfwGetMouseButton(window, button);
}
DYLD_INTERPOSE(Hook_glfwGetMouseButton, glfwGetMouseButton)

int Hook_glfwGetGamepadState(int jid, GLFWgamepadstate *state) {
    int result = glfwGetGamepadState(jid, state);
    if (result && g_cursor_free) {
        std::memset(state, 0, sizeof *state);
        state->axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] = -1.0f;  // triggers rest at -1
        state->axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] = -1.0f;
    }
    return result;
}
DYLD_INTERPOSE(Hook_glfwGetGamepadState, glfwGetGamepadState)

const unsigned char *Hook_glfwGetJoystickButtons(int jid, int *count) {
    static unsigned char released[128];
    const unsigned char *buttons = glfwGetJoystickButtons(jid, count);
    if (buttons && g_cursor_free) {
        if (*count > int(sizeof released)) *count = sizeof released;
        return released;
    }
    return buttons;
}
DYLD_INTERPOSE(Hook_glfwGetJoystickButtons, glfwGetJoystickButtons)

const float *Hook_glfwGetJoystickAxes(int jid, int *count) {
    static float centred[32];
    const float *axes = glfwGetJoystickAxes(jid, count);
    if (axes && g_cursor_free) {
        if (*count > int(sizeof centred / sizeof centred[0])) *count = sizeof centred / sizeof centred[0];
        return centred;
    }
    return axes;
}
DYLD_INTERPOSE(Hook_glfwGetJoystickAxes, glfwGetJoystickAxes)

__attribute__((constructor)) void on_load() {
    // Don't pass the injection on to anything the game launches.
    unsetenv("DYLD_INSERT_LIBRARIES");
    Log("reVC-fpv %s loaded", REVC_FPV_VERSION);
    // Before main(): the game runs no other thread yet, so patching its code is safe.
    install_game_hooks();
}

}  // namespace
