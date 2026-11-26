#include "drone.h"

#include <GLFW/glfw3.h>

#include <chrono>
#include <cmath>

#include "audio.h"
#include "damage.h"
#include "ear.h"
#include "effects.h"
#include "fpvapi.h"
#include "input.h"
#include "kamikaze.h"
#include "log.h"
#include "pilot.h"
#include "render.h"
#include "sfx.h"
#include "standin.h"
#include "wind.h"
#include "world.h"

namespace drone {
namespace {

using fpvapi::fpv;
using vc::api;

constexpr float kRadToDeg = 57.2957795f;
constexpr float kNearClip = 0.2f;           // with clear space in front of the lens
constexpr float kMinNearClip = 0.02f;       // when the lens almost touches something
constexpr float kWidestAspect = 2.4f;       // frustum probes cover up to 21:9
constexpr float kPedFeet = 1.0f;            // a ped's position is this far above his feet
#if defined(LIBFPV_KAMIKAZE)
constexpr float kCutsceneDistance = 14.0f;  // blast camera back from the blast, horizontally
constexpr float kCutsceneHeight = 5.0f;     // and up
#endif

enum class View { Fpv, Chase, Los, Count };
constexpr uint32_t kMoveHereAction = 1;  // F8 > Fly button id

fpv_drone *g_drone = nullptr;
View g_view = View::Fpv;  // the flight view, also the one F7 goes back to from the pilot view
// F: back to Tommy (the game's camera and controls), the drone left in the
// world, still flown by the radio. F7 goes back to it.
bool g_pilot_view = false;
bool g_pilot_moved = false;  // the pilot view was used this flight: Tommy may not be at the launch point
// The F that opened the pilot view is still down: Tommy stays held until it
// is released, or the game takes it as its own F (get in a car nearby).
bool g_pilot_key_down = false;
float g_spawn_pos[3];
float g_spawn_rot[4];
std::chrono::steady_clock::time_point g_last_step;
double g_first_sim_time = -1;  // sim clock at the first frame of this flight
fpv_frame g_frame{};
bool g_have_frame = false;     // g_frame is from this flight
bool g_osd_visible = false;
bool g_signal_lost = false;

// The camera the game shows this frame.
struct Shot {
    bool on = false;
    float pos[3], forward[3], up[3], right[3];
    float fov;  // vertical degrees
    float near_clip;
};
Shot g_shot;

// CDraw::SetFOV takes a 4:3 horizontal FOV and the RwCamera's vertical view
// window comes out as fov_scale * tan(fov / 2). The scale is learned from the
// RwCamera after each frame (observe_view_window), so cutscenes and other
// aspect handling don't skew the vertical FOV.
float g_fov_scale = 0.75f;
float g_fov_passed = 0;  // what we gave SetFOV this frame, degrees

#if defined(LIBFPV_KAMIKAZE)
// After the warhead went off: static in the goggles or an outside shot of the
// blast for a while (F8 > Kamikaze), then a respawn, back to Tommy, or
// nothing. Timed with the game's clock, so the pause menu pauses it too.
struct Ending {
    bool on = false;
    bool shown = false;     // the view is up (false: duration 0, straight to "then")
    bool cutscene = false;  // the view is the outside shot (else signal lost)
    bool timed = false;     // false: held until the player respawns, recalls or moves Tommy
    uint32_t then = FPV_KAMIKAZE_THEN_RESPAWN;
    uint32_t end_ms = 0;  // game time when "then" happens
    View view_before = View::Fpv;
};
Ending g_ending;
#endif

bool ending_on() {
#if defined(LIBFPV_KAMIKAZE)
    return g_ending.on;
#else
    return false;
#endif
}

bool ending_cutscene() {
#if defined(LIBFPV_KAMIKAZE)
    return g_ending.on && g_ending.cutscene;
#else
    return false;
#endif
}

// Tommy can't move while we fly, or while the overlay takes the keyboard. We
// own one bit of CPad::DisablePlayerControls; scripts' own bits are untouched.
void hold_player(bool hold) {
    uint16_t &bits = vc::pad_disabled_controls();
    bits = hold ? uint16_t(bits | vc::kPadDisabledByFpv) : uint16_t(bits & ~vc::kPadDisabledByFpv);
}

float heading_of(const vc::CVector &forward) { return std::atan2(-forward.x, forward.y); }

// The FPV camera rides a few cm above the road: a fixed near plane cuts the
// ground away under the lens. Probe the view frustum (centre, edge midpoints
// and corners) and pull the near plane in front of the closest geometry.
float near_clip_for(const fpv_camera &c, float fov_y) {
    const float ty = std::tan(fov_y * 0.5f / kRadToDeg), tx = ty * kWidestAspect;
    float nearest = kNearClip / 0.8f;  // depth along the view axis
    for (int sy = -1; sy <= 1; ++sy)
        for (int sx = -1; sx <= 1; ++sx) {
            float d[3], len = 0;
            for (int i = 0; i < 3; ++i) {
                d[i] = c.forward[i] + c.right[i] * tx * float(sx) + c.up[i] * ty * float(sy);
                len += d[i] * d[i];
            }
            len = std::sqrt(len);
            for (float &v : d) v /= len;
            fpv_raycast_hit hit{};
            // depth = distance * cos(angle to the axis) = distance / len
            if (world::raycast(nullptr, c.position, d, nearest * len, &hit) && hit.distance / len < nearest)
                nearest = hit.distance / len;
        }
    float n = nearest * 0.8f;
    return n < kMinNearClip ? kMinNearClip : n > kNearClip ? kNearClip : n;
}

// libfpv widens the FOV for its fisheye so the top and bottom edges of the
// distorted image show the FOV the player chose, for the camera's
// aspect_hint (4:3 analog). Redo it for the aspect the lens really works on:
// the game window's, or with the video feed's resolution effect the
// camera's own picture (4:3 or 16:9, black bars) cut from the middle of the
// frame, where a picture wider than the window needs a taller frame.
float lens_edge(const fpv_camera &c, float aspect) {
    float r2 = 1 / (1 + aspect * aspect);  // squared radius of the top edge centre
    float zoom = c.distortion_zoom > 0 ? c.distortion_zoom : 1;
    return (1 + c.distortion_k1 * r2 + c.distortion_k2 * r2 * r2) / zoom;
}
float lens_fov(const fpv_camera &c) {
    float out = render::aspect(), taller = 1;
    const fpv_video_feed &vf = g_frame.video_feed;
    if (g_have_frame && (vf.flags & FPV_VIDEO_FEED_RESOLUTION) && vf.picture_aspect > 0.1f) {
        out = vf.picture_aspect;
        if (out > render::aspect()) taller = out / render::aspect();
    }
    float t = std::tan(c.fov_y_degrees * 0.5f / kRadToDeg);
    if ((c.distortion_k1 != 0 || c.distortion_k2 != 0) && c.aspect_hint > 0)
        t *= lens_edge(c, c.aspect_hint) / lens_edge(c, out);
    float fov = 2 * std::atan(t * taller) * kRadToDeg;
    return fov > 170 ? 170 : fov;
}

void set_shot(const float pos[3], const float forward[3], const float up[3], const float right[3], float fov,
              float near_clip) {
    for (int i = 0; i < 3; ++i) {
        g_shot.pos[i] = pos[i];
        g_shot.forward[i] = forward[i];
        g_shot.up[i] = up[i];
        g_shot.right[i] = right[i];
    }
    g_shot.fov = fov;
    g_shot.near_clip = near_clip;
    g_shot.on = true;
}

void apply_camera(const fpv_camera &c, bool fpv_view) {
    float fov = fpv_view ? lens_fov(c) : c.fov_y_degrees;
    set_shot(c.position, c.forward, c.up, c.right, fov, fpv_view ? near_clip_for(c, fov) : kNearClip);
}

// Betaflight's home arrow, home distance and GPS Rescue lead back to Tommy
// (his own spot while he stands in for the drone).
void update_home() {
    vc::CVector p;
    if (!pilot_position(&p)) return;
    float home[3] = {p.x, p.y, p.z};
    fpv.fpv_gps_set_home(home);
}

// The pilot's feet: the LOS camera, the video and radio links' receiver and,
// heard from the pilot, the ear. Tommy's spot while he stands in, else Tommy
// wherever he walks or drives (the pilot view).
void update_pilot_position() {
    vc::CVector p;
    if (!pilot_position(&p)) return;
    float feet[3] = {p.x, p.y, p.z - kPedFeet};
    fpv.fpv_drone_set_pilot_position(g_drone, feet);
}

fpv_view libfpv_view() {
    if (g_pilot_view) return FPV_VIEW_PILOT;
    if (ending_cutscene()) return FPV_VIEW_LOS;  // an outside shot of the blast
    return g_view == View::Chase ? FPV_VIEW_CHASE : g_view == View::Los ? FPV_VIEW_LOS : FPV_VIEW_FPV;
}

// The game camera as libfpv's listener in the pilot view, so the drone pans
// to where it is on screen. Rotation: right/forward/up as a quaternion.
void update_listener() {
    const vc::CMatrix &m = vc::camera_matrix();
    // the game's camera "right" is screen left (see apply_shot)
    float r[3] = {-m.right.x, -m.right.y, -m.right.z}, f[3] = {m.forward.x, m.forward.y, m.forward.z},
          u[3] = {m.up.x, m.up.y, m.up.z};
    float m00 = r[0], m01 = f[0], m02 = u[0], m10 = r[1], m11 = f[1], m12 = u[1], m20 = r[2], m21 = f[2], m22 = u[2];
    float q[4], tr = m00 + m11 + m22;
    if (tr > 0) {
        float k = std::sqrt(tr + 1.0f) * 2;
        q[3] = 0.25f * k, q[0] = (m21 - m12) / k, q[1] = (m02 - m20) / k, q[2] = (m10 - m01) / k;
    } else if (m00 > m11 && m00 > m22) {
        float k = std::sqrt(1.0f + m00 - m11 - m22) * 2;
        q[3] = (m21 - m12) / k, q[0] = 0.25f * k, q[1] = (m01 + m10) / k, q[2] = (m02 + m20) / k;
    } else if (m11 > m22) {
        float k = std::sqrt(1.0f + m11 - m00 - m22) * 2;
        q[3] = (m02 - m20) / k, q[0] = (m01 + m10) / k, q[1] = 0.25f * k, q[2] = (m12 + m21) / k;
    } else {
        float k = std::sqrt(1.0f + m22 - m00 - m11) * 2;
        q[3] = (m10 - m01) / k, q[0] = (m02 + m20) / k, q[1] = (m12 + m21) / k, q[2] = 0.25f * k;
    }
    float pos[3] = {m.pos.x, m.pos.y, m.pos.z};
    fpv.fpv_audio_set_listener(pos, q);
}

// Heard from the pilot (FPV_DRONE_HEARD_FROM_PILOT): the game's own sounds
// too, from the pilot's head facing the drone. Not in the pilot view, whose
// game camera is already on Tommy.
void update_ear() {
    if (!g_drone || !g_have_frame || !(g_frame.flags & FPV_DRONE_HEARD_FROM_PILOT) || g_pilot_view) {
        ear::clear();
        return;
    }
    const fpv_camera &c = g_frame.los_camera;
    ear::set(c.position, c.forward, c.up);
}

// The pilot's pose while the drone is flown from its views and the radio is
// in use (the player can turn it off in F8 > Fly > Pilot). Never in the
// pilot view: Tommy is the player's there, with the game's own anims.
void update_pose(const fpv_frame &frame) {
    vc::CEntity *tommy = api.FindPlayerPed();
    if (!tommy || api.FindPlayerVehicle()) return;
    bool radio = (frame.flags & FPV_DRONE_RADIO_ACTIVE) != 0;
    pilot::hold(tommy, !g_pilot_view && radio && (fpvapi::pilot_config().features & FPV_PILOT_POSE));
}

// Where a drone starts (a launch, or a respawn after the pilot view): in
// front of Tommy (or his vehicle) where he is now, resting on the ground.
// Sets g_spawn_pos/rot. Returns whether it found the ground.
bool place_spawn(vc::CEntity *tommy, vc::CEntity *car, float *heading) {
    vc::CMatrix &m = *vc::entity_matrix(car ? car : tommy);
    // In a vehicle, the vehicle's own position.
    vc::CVector p = car ? m.pos : vc::entity_position(tommy);
    float h = heading_of(m.forward);
    float ahead = car ? 4.0f : 2.0f;
    g_spawn_pos[0] = p.x - std::sin(h) * ahead;
    g_spawn_pos[1] = p.y + std::cos(h) * ahead;
    float origin[3] = {g_spawn_pos[0], g_spawn_pos[1], p.z + 1.0f}, down[3] = {0, 0, -1};
    fpv_raycast_hit hit{};
    world::ignore(tommy);
    bool ground = world::raycast(nullptr, origin, down, 5.0f, &hit) != 0;
    g_spawn_pos[2] = ground ? origin[2] - hit.distance : p.z - 1.0f;
    g_spawn_rot[0] = 0;
    g_spawn_rot[1] = 0;
    g_spawn_rot[2] = std::sin(h / 2);
    g_spawn_rot[3] = std::cos(h / 2);
    if (heading) *heading = h;
    return ground;
}

void start() {
    if (!fpvapi::load()) {
        sfx::play(vc::kSoundError);
        return;
    }
    vc::CEntity *tommy = api.FindPlayerPed();
    if (!tommy || vc::replay_playing()) {
        sfx::play(vc::kSoundError);
        return;
    }
    vc::CEntity *car = api.FindPlayerVehicle();
    float h = 0;
    bool ground = place_spawn(tommy, car, &h);
    vc::CVector p = vc::entity_position(tommy);

    fpv_drone_desc desc;
    fpv.fpv_drone_desc_defaults(&desc);
    for (int i = 0; i < 3; ++i) desc.position[i] = g_spawn_pos[i];
    for (int i = 0; i < 4; ++i) desc.rotation[i] = g_spawn_rot[i];
    desc.pilot_position[0] = p.x;
    desc.pilot_position[1] = p.y;
    desc.pilot_position[2] = p.z - kPedFeet;
    fpv_result r = fpv.fpv_drone_create(&desc, &g_drone);
    if (r != FPV_OK) {
        Log("fpv_drone_create: %s (%s)", fpv.fpv_result_string(r), fpv.fpv_last_error());
        sfx::play(vc::kSoundError);
        g_drone = nullptr;
        return;
    }
    fpv.fpv_world_set_raycast_callback_ex(&world::raycast_ex, nullptr);
    kamikaze::forget_detonations();
#if defined(LIBFPV_KAMIKAZE)
    g_ending = Ending{};
#endif
    update_home();
    wind::reset();
    damage::reset();

    hold_player(true);
    g_last_step = std::chrono::steady_clock::now();
    g_first_sim_time = -1;
    g_have_frame = false;
    g_shot.on = false;
    g_view = View::Fpv;
    g_pilot_view = false;
    g_pilot_moved = false;
    g_pilot_key_down = false;
    fpv.fpv_set_view(libfpv_view());
    audio::start();
    fpv.fpv_overlay_set_action(kMoveHereAction, "Move Tommy here (Y)");
    sfx::play(vc::kSoundSelect);

    fpv_input_status st{};
    st.struct_size = sizeof st;
    fpv.fpv_input_get_status(&st);
    Log("drone launched at %.2f %.2f %.2f heading %.1f (ground %s, %s), input %d '%s'%s", g_spawn_pos[0],
        g_spawn_pos[1], g_spawn_pos[2], h * kRadToDeg, ground ? "hit" : "missed", car ? "in a vehicle" : "on foot",
        int(st.source), st.device_name, st.needs_setup ? " (needs setup)" : "");
}

void stop() {
    fpv.fpv_overlay_set_action(kMoveHereAction, nullptr);
    audio::stop();  // before the drone: the audio thread reads libfpv's synth
    g_shot.on = false;
    standin::end(true);  // no-op after move_here's end(false)
    pilot::release();
    hold_player(false);
    ear::clear();
    damage::reset();
    if (g_pilot_view) fpv.fpv_audio_set_listener(nullptr, nullptr);
    g_pilot_view = false;
    if (g_drone) {
        fpv.fpv_world_set_raycast_callback_ex(nullptr, nullptr);
        fpv.fpv_wind_set(nullptr);
        fpv.fpv_wind_set_sources(nullptr, 0);
        world::ignore(nullptr);
        fpv.fpv_drone_delete(g_drone);
        g_drone = nullptr;
    }
    fpv.fpv_set_view(FPV_VIEW_FPV);
#if defined(LIBFPV_KAMIKAZE)
    g_ending = Ending{};
#endif
    g_osd_visible = false;
    g_signal_lost = false;
    g_have_frame = false;
    Log("drone stopped");
}

#if defined(LIBFPV_KAMIKAZE)
// Points the camera from `from` at `at`, level (no roll).
void aim_camera(const float from[3], const float at[3], float fov) {
    float f[3] = {at[0] - from[0], at[1] - from[1], at[2] - from[2]};
    float len = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (len < 1e-3f) return;
    for (float &v : f) v /= len;
    float r[3] = {f[1], -f[0], 0};  // forward x world up
    float rl = std::sqrt(r[0] * r[0] + r[1] * r[1]);
    if (rl < 1e-3f) r[0] = 1, r[1] = 0, rl = 1;
    r[0] /= rl, r[1] /= rl;
    float u[3] = {r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0]};
    set_shot(from, f, u, r, fov, kNearClip);
}

// Cutscene: a camera behind and above where the drone came from, pulled in
// front of any wall in the way, watching the blast.
void place_blast_camera(const fpv_detonation &d) {
    float back[2] = {-d.velocity[0], -d.velocity[1]};
    float bl = std::sqrt(back[0] * back[0] + back[1] * back[1]);
    if (bl < 1.0f) {  // dropped straight down or hovered: behind the lens instead
        back[0] = -g_frame.camera.forward[0], back[1] = -g_frame.camera.forward[1];
        bl = std::sqrt(back[0] * back[0] + back[1] * back[1]);
    }
    if (bl < 1e-3f) back[0] = 0, back[1] = -1, bl = 1;
    back[0] /= bl, back[1] /= bl;
    const float *n = d.normal;
    bool has_normal = n[0] * n[0] + n[1] * n[1] + n[2] * n[2] > 0.5f;
    float origin[3];  // off the surface, so the probes start in open air
    for (int i = 0; i < 3; ++i) origin[i] = d.position[i] + (has_normal ? n[i] : (i == 2 ? 1.0f : 0.0f)) * 0.5f;
    const float len = std::sqrt(kCutsceneDistance * kCutsceneDistance + kCutsceneHeight * kCutsceneHeight);
    static const float kTurns[] = {0, 45, -45, 90, -90, 135, -135, 180};
    float best[3] = {0, 0, 0}, best_clear = -1;
    for (float turn : kTurns) {
        float a = turn / kRadToDeg, c = std::cos(a), sn = std::sin(a);
        float dir[3] = {(back[0] * c - back[1] * sn) * kCutsceneDistance / len,
                        (back[0] * sn + back[1] * c) * kCutsceneDistance / len, kCutsceneHeight / len};
        fpv_raycast_hit hit{};
        float clear = world::raycast(nullptr, origin, dir, len, &hit) ? hit.distance : len;
        if (clear > best_clear) {
            best_clear = clear;
            for (int i = 0; i < 3; ++i) best[i] = dir[i];
        }
        if (clear >= len * 0.9f) break;
    }
    float dist_out = best_clear * 0.85f < 2.5f ? 2.5f : best_clear * 0.85f;
    float from[3], at[3] = {d.position[0], d.position[1], d.position[2] + 1.0f};  // the fireball rises
    for (int i = 0; i < 3; ++i) from[i] = origin[i] + best[i] * dist_out;
    aim_camera(from, at, 50.0f);
    Log("kamikaze: cutscene camera at %.2f %.2f %.2f (%.1f m clear)", from[0], from[1], from[2], best_clear);
}

const char *then_name(uint32_t then) {
    return then == FPV_KAMIKAZE_THEN_RECALL ? "recall" : then == FPV_KAMIKAZE_THEN_NOTHING ? "nothing" : "respawn";
}

// The player's F8 > Kamikaze choice, read at the blast. The "then" runs from tick().
void begin_ending(const fpv_detonation &d) {
    kamikaze::EndingChoice e = kamikaze::ending_choice();
    g_ending = Ending{};
    g_ending.on = true;
    g_ending.then = e.then;
    g_ending.view_before = g_view;
    // "Nothing" keeps the view up whatever the duration: there is nothing else to show.
    g_ending.shown = e.seconds > 0 || e.then == FPV_KAMIKAZE_THEN_NOTHING;
    g_ending.timed = e.then != FPV_KAMIKAZE_THEN_NOTHING;
    g_ending.end_ms = *api.TimeInMilliseconds + (e.seconds > 0 ? uint32_t(e.seconds * 1000.0f) : 0);
    g_ending.cutscene = g_ending.shown && e.view == FPV_KAMIKAZE_VIEW_CUTSCENE;
    Log("kamikaze: ending: %s, then %s", !g_ending.shown ? "no view" : g_ending.cutscene ? "cutscene" : "signal lost",
        then_name(e.then));
    if (!g_ending.shown) return;
    if (g_pilot_view) {  // the player watches from Tommy: the view only shows if he goes back (F7)
        g_ending.cutscene = false;
        return;
    }
    if (g_ending.cutscene) {
        place_blast_camera(d);
    } else {
        g_view = View::Fpv;  // the goggles
        audio::set_static(true);
    }
    g_osd_visible = !g_ending.cutscene;
    g_signal_lost = g_osd_visible;
}

// The blast's view is over: back to the live feed (the drone was just reset).
void clear_ending() {
    if (!g_ending.on) return;
    if (g_ending.shown && !g_ending.cutscene) audio::set_static(false);
    g_view = g_ending.view_before;
    g_ending = Ending{};
    g_signal_lost = false;
}
#endif

// R, or the ending's "respawn": repaired, at the launch point. After the
// pilot view (F), where Tommy may have walked or driven off, in front of him
// where he is now (his real self comes home from the drone first).
void respawn_drone() {
    standin::end(true);
    vc::CEntity *tommy = api.FindPlayerPed();
    if (g_pilot_moved && tommy) {
        bool ground = place_spawn(tommy, api.FindPlayerVehicle(), nullptr);
        Log("drone respawned at %.2f %.2f %.2f, in front of Tommy (ground %s)", g_spawn_pos[0], g_spawn_pos[1],
            g_spawn_pos[2], ground ? "hit" : "missed");
    }
    fpv.fpv_drone_repair(g_drone);
    fpv.fpv_drone_reset(g_drone, g_spawn_pos, g_spawn_rot);
    standin::after_respawn(g_spawn_pos);
#if defined(LIBFPV_KAMIKAZE)
    clear_ending();
#endif
}

// One libfpv step, from the camera hook, so the camera it produces is the one
// this frame renders.
void step() {
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - g_last_step).count();
    g_last_step = now;
    if (dt > 0.1f) dt = 0.1f;

    wind::update(dt, g_have_frame ? g_frame.position : g_spawn_pos);
    fpv_frame frame{};
    frame.struct_size = sizeof frame;
    fpv_result r = fpv.fpv_drone_tick(g_drone, dt, &frame);
    if (r != FPV_OK) {
        static int reported = 0;
        if (reported++ < 5) Log("fpv_drone_tick: %s", fpv.fpv_result_string(r));
        return;
    }
    // Until libfpv's sim thread has stepped the new drone, the frame shows a
    // stale snapshot (the world origin, or the last drone): the camera would
    // jump there. libfpv's clock is not reset per drone, so wait until it
    // moved 50 ms past its first reading.
    if (g_first_sim_time < 0) g_first_sim_time = frame.sim_time;
    if (frame.sim_time < g_first_sim_time + 0.05) return;
    g_frame = frame;
    g_have_frame = true;
    if (!g_pilot_view) standin::update(frame.position);
    update_pose(frame);
    update_home();
    update_pilot_position();
#if defined(LIBFPV_KAMIKAZE)
    fpv_detonation d;
    while (kamikaze::poll(&d)) {
        kamikaze::explode(d);
        if (!g_ending.on) begin_ending(d);
    }
#endif
    if (!ending_on()) g_osd_visible = !g_pilot_view && g_view == View::Fpv;
    if (!ending_cutscene() && !g_pilot_view)
        apply_camera(g_view == View::Fpv ? frame.camera : g_view == View::Chase ? frame.chase_camera : frame.los_camera,
                     g_view == View::Fpv);

    for (uint32_t i = 0; i < frame.impact_count && i < FPV_MAX_IMPACTS; ++i) {
        const fpv_impact &im = frame.impacts[i];
        if (im.speed < 1.0f) continue;  // resting contact
        Log("impact at %.2f %.2f %.2f n %.2f %.2f %.2f speed %.1f impulse %.2f motor %d object %08x", im.position[0],
            im.position[1], im.position[2], im.normal[0], im.normal[1], im.normal[2], im.speed, im.impulse,
            int(im.motor_index), im.object_id);
    }
    effects::on_frame(frame);
    damage::update(g_drone, frame, dt);
    sfx::on_frame(frame);

    // Sparse trace for debugging the axes and the camera mapping.
    static float log_timer = 0;
    log_timer += dt;
    if (log_timer > 2.0f) {
        log_timer = 0;
        Log("drone pos %.2f %.2f %.2f flags 0x%x alt %.2f thr %.2f fov %.1f (game %.1f, scale %.3f) near %.3f "
            "wind %.1f %.1f %.1f video 0x%x%s arm '%s'",
            frame.position[0], frame.position[1], frame.position[2], frame.flags, frame.altitude, frame.channels[2],
            g_shot.fov, g_fov_passed, g_fov_scale, g_shot.near_clip, frame.wind[0], frame.wind[1], frame.wind[2],
            frame.video_feed.flags, g_pilot_view ? " (pilot view)" : "", frame.arming_disable_text);
    }
}

// Writes the shot into TheCamera and the RenderWare camera, as the end of
// CCamera::Process does with the game's own camera.
void apply_shot() {
    float t = std::tan(g_shot.fov * 0.5f / kRadToDeg) / g_fov_scale;
    g_fov_passed = 2 * std::atan(t) * kRadToDeg;
    api.SetFOV(g_fov_passed);

    vc::CMatrix &m = vc::camera_matrix();
    // The game's camera "right" is up x forward, i.e. screen left.
    m.right = {-g_shot.right[0], -g_shot.right[1], -g_shot.right[2]};
    m.forward = {g_shot.forward[0], g_shot.forward[1], g_shot.forward[2]};
    m.up = {g_shot.up[0], g_shot.up[1], g_shot.up[2]};
    m.pos = {g_shot.pos[0], g_shot.pos[1], g_shot.pos[2]};
    api.CalculateDerivedValues(api.TheCamera);

    void *cam = vc::scene_camera();
    if (!cam) return;
    void *frame = api.RwCameraGetFrame(cam);
    if (!frame) return;
    void *rw = api.RwFrameGetMatrix(frame);
    auto put = [](float *dst, const vc::CVector &v) { dst[0] = v.x, dst[1] = v.y, dst[2] = v.z; };
    put(api.RwMatrixGetRight(rw), m.right);
    put(api.RwMatrixGetUp(rw), m.up);
    put(api.RwMatrixGetAt(rw), m.forward);
    put(api.RwMatrixGetPos(rw), m.pos);
    api.RwMatrixUpdate(rw);
    api.RwFrameUpdateObjects(frame);
    api.RwFrameOrthoNormalize(frame);
    api.RwCameraSetNearClipPlane(cam, g_shot.near_clip);
    // CCamera::Process copied the game's own near plane (about 0.9 m) to
    // CDraw before we got here. Sprites take their depth from CDraw's: with
    // the old value, coronas and pickup glows came out nearer than they are
    // and showed through walls.
    *api.ms_fNearClipZ = g_shot.near_clip;
}

// While the overlay is open on foot, Tommy must not walk around as you type.
void hold_for_overlay() {
    static bool held = false;
    bool open = input::overlay_has_input();
    if (g_drone || open == held) return;
    hold_player(open);
    held = open;
}

// F: the view goes back to Tommy, who walks and drives again; the drone stays
// where it is, armed or not, flown by the radio. Tommy comes home from standing in.
void to_pilot() {
    standin::end(true);
    g_pilot_view = true;
    g_pilot_moved = true;
    g_pilot_key_down = true;
    g_shot.on = false;
    g_osd_visible = false;
    audio::set_static(false);
    input::release_flight_keys();
    fpv.fpv_set_view(FPV_VIEW_PILOT);
    update_listener();
    // Tommy's controls come back once F is up (tick)
    sfx::play(vc::kSoundBack);
    Log("pilot view: back to Tommy, the drone left at %.2f %.2f %.2f%s", g_frame.position[0], g_frame.position[1],
        g_frame.position[2], g_frame.flags & FPV_DRONE_ARMED ? " (armed)" : "");
}

// F7 from the pilot view: back to the drone, in the view the player had. A
// kamikaze blast's view still going on shows as the goggles' lost signal.
void back_to_drone() {
    g_pilot_view = false;
    fpv.fpv_audio_set_listener(nullptr, nullptr);
#if defined(LIBFPV_KAMIKAZE)
    if (g_ending.on && g_ending.shown) {
        g_ending.cutscene = false;
        g_view = View::Fpv;
        audio::set_static(true);
        g_osd_visible = g_signal_lost = true;
    }
#endif
    hold_player(true);
    fpv.fpv_set_view(libfpv_view());
    sfx::play(vc::kSoundSelect);
    Log("pilot view over: back to the drone");
}

}  // namespace

void inject_channels(const float *channels, uint32_t count) {
    if (fpvapi::loaded()) fpv.fpv_input_set_channels(channels, count);
}

bool active() { return g_drone != nullptr; }

bool flying_view() { return g_drone && !g_pilot_view; }

bool pilot_view() { return g_drone && g_pilot_view; }

bool camera_on() { return g_drone && g_shot.on && !g_pilot_view; }

bool hide_hud() { return flying_view(); }

bool hide_text() { return flying_view() && (g_view == View::Fpv || g_signal_lost) && !ending_cutscene(); }

const fpv_frame *frame() { return g_drone && g_have_frame ? &g_frame : nullptr; }

bool drone_position(vc::CVector *out) {
    if (!g_drone || !g_have_frame) return false;
    *out = {g_frame.position[0], g_frame.position[1], g_frame.position[2]};
    return true;
}

bool pilot_position(vc::CVector *out) {
    if (!g_drone || !api.FindPlayerPed()) return false;
    *out = standin::home();  // his spot, also while he stands in at the drone
    return true;
}

bool osd_frame(fpv_frame *out, bool *lost) {
    if (!g_osd_visible || g_pilot_view || !g_have_frame || vc::replay_playing()) return false;
    *out = g_frame;
    *lost = g_signal_lost;
    return true;
}

namespace {
// v rotated by quaternion q {x, y, z, w}.
void rotate(const float q[4], const float v[3], float out[3]) {
    float tx = 2 * (q[1] * v[2] - q[2] * v[1]), ty = 2 * (q[2] * v[0] - q[0] * v[2]),
          tz = 2 * (q[0] * v[1] - q[1] * v[0]);
    out[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
    out[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
    out[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
}
}  // namespace

bool model_pose(ModelPose *out) {
#if defined(LIBFPV_KAMIKAZE)
    if (g_have_frame && (g_frame.flags & FPV_DRONE_DETONATED)) return false;  // nothing left to draw
#endif
    if (!g_drone || !g_have_frame) return false;
    // The pilot view shows the drone through the game's own camera.
    if (!g_pilot_view && (!g_shot.on || (g_view == View::Fpv && !ending_cutscene()))) return false;
    const fpv_frame &f = g_frame;
    // libfpv rotations map the host's right/up/forward (east, up, north) to world axes.
    static const float kRight[3] = {1, 0, 0}, kUp[3] = {0, 0, 1}, kForward[3] = {0, 1, 0};
    rotate(f.rotation, kRight, out->right);
    rotate(f.rotation, kUp, out->up);
    rotate(f.rotation, kForward, out->forward);
    for (int i = 0; i < 3; ++i) out->pos[i] = f.position[i];
    for (int m = 0; m < 4; ++m) {
        for (int i = 0; i < 3; ++i) out->motor[m][i] = f.motors[m].position[i];
        out->rpm[m] = f.motors[m].rpm;
        out->prop_damage[m] = f.motors[m].prop_damage;
    }
    return true;
}

void move_here() {
    float pos[3] = {g_frame.position[0], g_frame.position[1], g_frame.position[2]};
    const float *f = g_frame.camera.forward;
    float heading = (f[0] * f[0] + f[1] * f[1] > 1e-6f) ? std::atan2(-f[0], f[1]) : 0;
    // Stand on whatever is under the drone (roof, road, floor); the probe
    // ignores Tommy. Fall back to the drone's own height.
    float origin[3] = {pos[0], pos[1], pos[2] + 0.5f}, down[3] = {0, 0, -1};
    fpv_raycast_hit hit{};
    bool ground = world::raycast(nullptr, origin, down, 300.0f, &hit) != 0;
    float z = ground ? origin[2] - hit.distance : pos[2];
    standin::end(false);  // he stays where the drone is, in its area (an interior, maybe)
    stop();
    vc::CEntity *tommy = api.FindPlayerPed();
    if (!tommy) return;
    api.LoadScene({pos[0], pos[1], z});
    vc::CEntity *car = api.FindPlayerVehicle();
    vc::CEntity *mover = car ? car : tommy;
    if (!vc::teleport(mover, {pos[0], pos[1], z + (car ? 1.5f : 1.0f)})) {
        sfx::play(vc::kSoundError);
        Log("move here: unknown entity type, Tommy stays");
        return;
    }
    api.SetHeading(vc::entity_matrix(mover), heading);
    if (!car) {
        float *rot = &vc::ped_rotation(tommy);
        rot[0] = rot[1] = heading;  // current and wanted heading, or he turns back
    }
    sfx::play(vc::kSoundSelect);
    Log("moved Tommy%s to the drone: %.2f %.2f %.2f (ground %s) heading %.1f", car ? " and his vehicle" : "", pos[0],
        pos[1], z, ground ? "hit" : "missed", heading * kRadToDeg);
}

void respawn() {
    if (!g_drone) return;
    if (ending_on()) Log("kamikaze: ending cut short: respawn");
    respawn_drone();
}

void cycle_view() {
    if (!g_drone || ending_on() || g_pilot_view) return;
    g_view = View((int(g_view) + 1) % int(View::Count));
    fpv.fpv_set_view(libfpv_view());
    sfx::play(vc::kSoundHighlight);
}

void toggle() {
    if (active() && g_pilot_view) {
        back_to_drone();
    } else if (active()) {
        stop();
        sfx::play(vc::kSoundBack);
    } else {
        start();
    }
}

void shutdown() {
    if (active()) stop();
}

void camera() {
    if (!g_drone) return;
    if (!vc::paused()) step();
    if (g_shot.on && !g_pilot_view) apply_shot();
}

void observe_view_window(float tan_half_y) {
    if (!g_drone || !g_shot.on || g_pilot_view || g_fov_passed <= 0 || tan_half_y <= 0) return;
    float scale = tan_half_y / std::tan(g_fov_passed * 0.5f / kRadToDeg);
    if (std::fabs(scale - g_fov_scale) > 0.002f) {
        static int reported = 0;
        if (reported++ < 10)
            Log("camera: view window %.4f for SetFOV %.2f: scale %.4f (was %.4f)", tan_half_y, g_fov_passed, scale,
                g_fov_scale);
        g_fov_scale = scale;
    }
}

void tick() {
    if (!fpvapi::loaded()) {
        if (input::take_press(GLFW_KEY_F8)) {  // F8 opens the overlay before the first flight too
            Log("F8 pressed, loading libfpv");
            if (fpvapi::load()) fpv.fpv_input_key(FPV_KEY_F8, 1), fpv.fpv_input_key(FPV_KEY_F8, 0);
        }
        if (input::take_press(GLFW_KEY_F7)) toggle();
        input::take_press(GLFW_KEY_F);  // on foot without a drone, F is the game's
        return;
    }
    kamikaze::init();
    hold_for_overlay();
    bool move = false;  // F8 buttons work on foot too, but Move here only in flight
    for (uint32_t id; (id = fpv.fpv_overlay_poll_action()) != 0;) move |= id == kMoveHereAction;
    bool toggle_key = input::take_press(GLFW_KEY_F7);
    bool camera_key = input::take_press(GLFW_KEY_C), reset_key = input::take_press(GLFW_KEY_R);
    bool pilot_key = input::take_press(GLFW_KEY_F);
    move |= input::take_press(GLFW_KEY_Y);
    if (!g_drone) {
        if (toggle_key) toggle();
        return;
    }
    if (toggle_key) {
        toggle();
        return;
    }
    if (vc::replay_playing()) {  // the replay takes the camera and the world
        Log("replay started, drone stopped");
        stop();
        return;
    }
    vc::CEntity *tommy = api.FindPlayerPed();
    if (!tommy || vc::ped_health(tommy) <= 0) {  // the game takes over
        Log("Tommy is down, drone stopped");
        stop();
        return;
    }
    // During a blast's view, R / F7 / Y (and the F8 button) act at once and
    // replace the ending's own "then". In the pilot view C is Tommy's.
    if (pilot_key && !g_pilot_view) to_pilot();
    else if (camera_key && !g_pilot_view) cycle_view();
    if (reset_key) respawn();
#if defined(LIBFPV_KAMIKAZE)
    if (g_ending.on && !move && g_ending.timed && int32_t(*api.TimeInMilliseconds - g_ending.end_ms) >= 0) {
        Log("kamikaze: ending over, %s", then_name(g_ending.then));
        if (g_ending.then == FPV_KAMIKAZE_THEN_RECALL) {
            stop();  // as F7: Tommy back in control where he stands
            sfx::play(vc::kSoundBack);
            return;
        }
        respawn_drone();
    }
    if (g_ending.on && move) Log("kamikaze: ending cut short: Move Tommy here");
#endif
    // Scripts may have cleared the pad's state (cutscenes, missions). In the
    // pilot view Tommy is the player's, but not while F is still down (the
    // game would take it as "get in a car") or he types in the overlay.
    if (g_pilot_key_down && !input::is_down(GLFW_KEY_F)) g_pilot_key_down = false;
    hold_player(!g_pilot_view || g_pilot_key_down || input::overlay_has_input());
    if (g_pilot_view) update_listener();
    update_ear();
    if (move && g_have_frame) move_here();
}

}  // namespace drone
