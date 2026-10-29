#include "standin.h"

#include <cmath>

#include "log.h"
#include "pilot.h"

namespace standin {
namespace {

using vc::api;

// Low, unlike gtasa-fpv's 20 m: Vice City's doors are script triggers on the
// player, and he often launches right outside one.
constexpr float kStart = 6.0f;    // drone this far from Tommy's spot: he stands in for it
constexpr float kStop = 3.0f;     // back this close: he goes home
constexpr float kReplace = 2.0f;  // re-file him in the world's sectors after this much travel

enum class Mode { Off, Body, Queries };  // Body: Tommy himself at the drone; Queries: only FindPlayer*

bool g_available = false;
Mode g_mode = Mode::Off;
vc::CVector g_home{};     // Tommy's spot
int g_home_area = 0;      // CGame::currArea there (an interior, or 0 for the city)
vc::CVector g_drone{};    // latest drone position
vc::CVector g_placed{};   // where Tommy was last re-filed
bool g_had_collision = true, g_was_visible = true;

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
    bool refile = dist(g_placed, g_drone) > kReplace;
    if (refile) api.WorldRemove(tommy), g_placed = g_drone;
    vc::entity_matrix(tommy)->pos = g_drone;
    if (refile) api.WorldAdd(tommy);
    vc::move_speed(tommy) = vc::turn_speed(tommy) = {0, 0, 0};
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

void update(const float drone_pos[3]) {
    g_drone = {drone_pos[0], drone_pos[1], drone_pos[2]};
    vc::CEntity *tommy = api.FindPlayerPed();
    if (tommy && !api.FindPlayerVehicle()) pilot::hold(tommy);
    if (g_mode == Mode::Off) {
        if (g_available && tommy && dist(vc::entity_position(tommy), g_drone) > kStart) begin();
        return;
    }
    if (dist(g_home, g_drone) < kStop) {
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
