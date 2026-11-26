#include "standin.h"

#include <cmath>

#include "fpvapi.h"
#include "log.h"
#include "pilot.h"

namespace standin {
namespace {

using vc::api;

// The distances (fpvapi::kStandInDistance, low: Vice City's doors are script
// triggers on the player) come from the player's F8 > Fly > Pilot choice.
constexpr float kReplace = 2.0f;     // re-file him in the world's sectors after this much travel
constexpr float kMaxAimOffset = 1.5f;  // spine farther than this from his root: the bones aren't in place yet
constexpr int kRespawnFrames = 30;     // at most this long waiting for a respawned drone to show up

enum class Mode { Off, Body, Queries };  // Body: Tommy himself at the drone; Queries: only FindPlayer*

bool g_available = false;
Mode g_mode = Mode::Off;
vc::CVector g_home{};     // Tommy's spot
int g_home_area = 0;      // CGame::currArea there (an interior, or 0 for the city)
vc::CVector g_drone{};    // latest drone position
vc::CVector g_placed{};   // where Tommy was last re-filed
bool g_had_collision = true, g_was_visible = true;
vc::CVector g_aim_offset{};  // his spine from his root, measured every step
vc::CVector g_respawn{};
int g_respawn_frames = 0;    // waiting for the respawned drone

float dist(const vc::CVector &a, const vc::CVector &b) {
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

void place(vc::CEntity *tommy) {
    // Every step: the game makes the player visible again on its own.
    vc::set_flag(tommy, vc::kUsesCollision, false);
    vc::set_flag(tommy, vc::kVisible, false);
    // New world sector maybe: Remove + Add. Not CPed::Teleport, which
    // restarts his tasks and anims: it would knock him out of the pose.
    // The AI aims at his spine (CPedIK's PED_MID): that point goes on the
    // drone, not his root, or police bullets pass under it.
    vc::CVector root{g_drone.x - g_aim_offset.x, g_drone.y - g_aim_offset.y, g_drone.z - g_aim_offset.z};
    bool refile = dist(g_placed, root) > kReplace;
    if (refile) api.WorldRemove(tommy), g_placed = root;
    vc::entity_matrix(tommy)->pos = root;
    if (refile) api.WorldAdd(tommy);
    vc::move_speed(tommy) = vc::turn_speed(tommy) = {0, 0, 0};
    // The bones (world space) where he is now: the AI reads them.
    api.MatrixUpdateRW(vc::entity_matrix(tommy));
    api.UpdateRwFrame(tommy);
    api.UpdateRpHAnim(tommy);
    vc::CVector spine = vc::aim_point(tommy);
    vc::CVector off{spine.x - root.x, spine.y - root.y, spine.z - root.z};
    g_aim_offset = dist(spine, root) < kMaxAimOffset ? off : vc::CVector{0, 0, 0};
}

void begin() {
    vc::CEntity *tommy = api.FindPlayerPed();
    if (!tommy) return;
    g_home = vc::entity_position(tommy);
    g_home_area = *api.currArea;
    if (api.FindPlayerVehicle()) {
        g_mode = Mode::Queries;
        Log("standin: Tommy is in a vehicle; streaming and population follow the drone, he stays");
        return;
    }
    g_had_collision = vc::flag(tommy, vc::kUsesCollision);
    g_was_visible = vc::flag(tommy, vc::kVisible);
    g_mode = Mode::Body;
    g_placed = {1e9f, 0, 0};
    g_aim_offset = {0, 0, 0};
    place(tommy);
    pilot::away(&g_home);  // still drawn at his spot
    Log("standin: Tommy stands in for the drone, home %.2f %.2f %.2f (area %d)", g_home.x, g_home.y, g_home.z,
        g_home_area);
}

}  // namespace

void set_available(bool available) { g_available = available; }

bool on() { return g_mode != Mode::Off; }

vc::CVector home() {
    if (g_mode != Mode::Off) return g_home;
    vc::CEntity *tommy = api.FindPlayerPed();
    return tommy ? vc::entity_position(tommy) : vc::CVector{0, 0, 0};
}

bool coors(vc::CVector *out) {
    if (g_mode == Mode::Off) return false;
    *out = g_drone;
    return true;
}

bool standing_in(vc::CEntity *ped) { return g_mode == Mode::Body && ped && ped == api.FindPlayerPed(); }

bool aim_point(float out[3]) {
    vc::CEntity *tommy = api.FindPlayerPed();
    if (g_mode != Mode::Body || !tommy) return false;
    vc::CVector p = vc::aim_point(tommy);
    if (dist(p, vc::entity_position(tommy)) > kMaxAimOffset) p = g_drone;
    out[0] = p.x, out[1] = p.y, out[2] = p.z;
    return true;
}

void after_respawn(const float spawn[3]) {
    g_respawn = {spawn[0], spawn[1], spawn[2]};
    g_respawn_frames = kRespawnFrames;
}

void update(const float drone_pos[3]) {
    g_drone = {drone_pos[0], drone_pos[1], drone_pos[2]};
    vc::CEntity *tommy = api.FindPlayerPed();
    if (g_respawn_frames > 0) {  // stale frames of the drone before the respawn
        if (dist(g_drone, g_respawn) > 1.0f && --g_respawn_frames > 0) return;
        g_respawn_frames = 0;
    }
    fpv_pilot_config pc = fpvapi::pilot_config();
    bool wanted = g_available && (pc.features & FPV_PILOT_STAND_IN);
    if (g_mode == Mode::Off) {
        if (wanted && tommy && dist(vc::entity_position(tommy), g_drone) > pc.stand_in_distance) begin();
        return;
    }
    // Turned off in the overlay mid-flight, or the drone came back: Tommy goes home.
    if (!wanted || dist(g_home, g_drone) < pc.stand_in_return_distance) {
        end(true);
        return;
    }
    if (g_mode == Mode::Body && tommy) place(tommy);
}

void end(bool go_home) {
    if (g_mode == Mode::Off) return;
    Mode mode = g_mode;
    g_mode = Mode::Off;
    pilot::away(nullptr);
    vc::CEntity *tommy = api.FindPlayerPed();
    if (mode != Mode::Body || !tommy) return;
    vc::set_flag(tommy, vc::kUsesCollision, g_had_collision);
    vc::set_flag(tommy, vc::kVisible, g_was_visible);
    if (!go_home) {
        Log("standin: over, Tommy stays at the drone (area %d)", *api.currArea);
        return;
    }
    // The drone may have gone through a door into an interior (or out of
    // one): as SET_AREA_VISIBLE, back to the one Tommy stands in.
    if (*api.currArea != g_home_area) {
        Log("standin: area %d -> %d (Tommy's)", *api.currArea, g_home_area);
        *api.currArea = g_home_area;
        api.RemoveBuildingsNotInArea(g_home_area);
    }
    api.LoadScene(g_home);
    api.WorldRemove(tommy);  // like place(): CPed::Teleport would restart the pose
    vc::entity_matrix(tommy)->pos = g_home;
    api.WorldAdd(tommy);
    vc::move_speed(tommy) = vc::turn_speed(tommy) = {0, 0, 0};
    Log("standin: over, Tommy back at %.2f %.2f %.2f", g_home.x, g_home.y, g_home.z);
}

}  // namespace standin
