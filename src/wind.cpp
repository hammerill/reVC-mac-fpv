#include "wind.h"

#include <algorithm>
#include <cmath>

#include "fpvapi.h"
#include "log.h"
#include "vc.h"

namespace wind {
namespace {

using fpvapi::fpv;
using vc::api;

// CWeather::Wind is the weather's Windyness: 0.25 sunny, 0.7 cloudy, 1.0
// raining, up to 2 in a hurricane. 1.0 is a storm's 11 m/s at 10 m.
constexpr float kStormMps = 11.0f;
// Sunny Vice City is calm, but real air is never dead still: a faint breeze
// at least (libfpv adds the height profile and the gusts on top).
constexpr float kCalmMps = 0.5f;
// The game's wind has no direction (only its particles lean one way), so
// the mean wind is a sea breeze off the Atlantic, east of the city: it
// blows towards the west.
constexpr float kDirection[2] = {-1.0f, 0.0f};
constexpr float kSourceRange = 150.0f;   // m: helicopters farther away can't reach the drone
constexpr float kBlastRange = 300.0f;    // m
constexpr float kRotorFullSpeed = 0.15f;  // m_aWheelSpeed[1] from which a helicopter flies
constexpr float kAirDensity = 1.2f;

bool g_slot_active[vc::kExplosionCount];
vc::CVector g_slot_pos[vc::kExplosionCount];
float g_log_timer = 0;

void weather() {
    float speed = std::max(kCalmMps, *api.Wind * kStormMps);
    fpv_wind w;
    fpv.fpv_wind_defaults(&w);
    w.velocity[0] = kDirection[0] * speed;
    w.velocity[1] = kDirection[1] * speed;
    // Interiors are separate areas, often placed high above the map: no weather in there.
    if (*api.currArea != 0) w.flags |= FPV_WIND_ENCLOSED;
    fpv.fpv_wind_set(&w);
}

// Helicopters with their rotors turning: rotor downwash, from momentum theory
// with the helicopter's weight as the thrust at full rotor speed.
uint32_t helicopters(const float at[3], fpv_wind_source *out, uint32_t max) {
    uint32_t n = 0;
    for (int i = 0, count = vc::vehicle_pool_size(); i < count && n < max; ++i) {
        auto *v = static_cast<vc::CEntity *>(vc::vehicle_at(i));
        if (!v || !vc::is_real_heli(v)) continue;
        float rotor = vc::heli_rotor_speed(v);
        if (!(rotor > 0.01f)) continue;
        const vc::CMatrix &m = *vc::entity_matrix(v);
        float dx = m.pos.x - at[0], dy = m.pos.y - at[1], dz = m.pos.z - at[2];
        if (dx * dx + dy * dy + dz * dz > kSourceRange * kSourceRange) continue;
        float mass = vc::heli_lift_mass(v);
        bool rc = vc::heli_is_rc(v);
        float radius = rc ? 0.4f : 5.0f, top = rc ? 0.2f : 1.8f;
        float power = std::min(1.0f, rotor / kRotorFullSpeed);
        float thrust = mass * 9.81f * power * power;
        fpv_wind_source &s = out[n++];
        s = fpv_wind_source{};
        s.position[0] = m.pos.x + m.up.x * top;
        s.position[1] = m.pos.y + m.up.y * top;
        s.position[2] = m.pos.z + m.up.z * top;
        s.direction[0] = -m.up.x, s.direction[1] = -m.up.y, s.direction[2] = -m.up.z;
        s.radius = radius;
        s.speed = std::sqrt(thrust / (2.0f * kAirDensity * 3.14159265f * radius * radius));
        s.type = FPV_WIND_SOURCE_ROTOR;
    }
    return n;
}

// A slot is a new explosion when it turns active or moves.
void blasts(const float at[3], bool report) {
    for (int i = 0; i < vc::kExplosionCount; ++i) {
        vc::Explosion e = vc::explosion_at(i);
        bool moved = e.pos.x != g_slot_pos[i].x || e.pos.y != g_slot_pos[i].y || e.pos.z != g_slot_pos[i].z;
        bool fresh = e.active && (!g_slot_active[i] || moved);
        g_slot_active[i] = e.active;
        g_slot_pos[i] = e.pos;
        if (!fresh || !report || e.type == vc::kExplosionMolotov) continue;  // a molotov burns, it doesn't blast
        float dx = e.pos.x - at[0], dy = e.pos.y - at[1], dz = e.pos.z - at[2];
        if (dx * dx + dy * dy + dz * dz > kBlastRange * kBlastRange) continue;
        // the game's radius is how far it does damage; the blast's strong core is about half that
        float pos[3] = {e.pos.x, e.pos.y, e.pos.z};
        fpv.fpv_wind_add_blast(pos, std::max(1.0f, 0.5f * e.radius));
        Log("wind: blast of explosion type %d (radius %.1f) %.0f m from the drone", e.type, e.radius,
            std::sqrt(dx * dx + dy * dy + dz * dz));
    }
}

}  // namespace

void reset() {
    float nowhere[3] = {0, 0, 0};
    blasts(nowhere, false);
    g_log_timer = 0;
}

void update(float dt, const float drone_pos[3]) {
    weather();
    fpv_wind_source sources[FPV_MAX_WIND_SOURCES];
    uint32_t n = helicopters(drone_pos, sources, FPV_MAX_WIND_SOURCES);
    fpv.fpv_wind_set_sources(sources, n);
    blasts(drone_pos, true);

    g_log_timer -= dt;
    if (g_log_timer <= 0) {
        g_log_timer = 10.0f;
        float strongest = 0;
        for (uint32_t i = 0; i < n; ++i) strongest = std::max(strongest, sources[i].speed);
        Log("wind: weather %.2f (%.1f m/s towards the west), area %d, %u rotor source(s), strongest %.1f m/s at the disc",
            *api.Wind, std::max(kCalmMps, *api.Wind * kStormMps), *api.currArea, n, strongest);
    }
}

}  // namespace wind
