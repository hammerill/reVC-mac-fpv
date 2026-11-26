#include "damage.h"

#include <algorithm>
#include <cmath>

#include "fpvapi.h"
#include "log.h"
#include "standin.h"

namespace damage {
namespace {

using fpvapi::fpv;
using vc::api;

// The amounts libfpv's guide suggests: 1 breaks a prop.
constexpr float kWaterPerSecond = 1.0f;  // a prop under the surface: gone in a second
constexpr float kFirePerSecond = 0.5f;   // a prop in the flames
constexpr float kWaterDrag = 6.0f;       // 1/s: a dead quad sinks at ~1.6 m/s, full throttle climbs out
constexpr float kBullet = 1.0f;          // a bullet through a prop breaks it
constexpr float kBlast = 1.0f;           // an explosion within its radius, all four
constexpr float kRotor = 1.0f;           // into a helicopter's main rotor, all four
constexpr float kPropRadius = 0.0635f;   // 5" props
// A bullet through the body (frame, battery, flight controller, camera; the
// warhead if there is one), a ball about the centre: the drone goes down.
constexpr float kBodyRadius = 0.06f;
constexpr float kPropThickness = 0.012f;  // m either side of a prop's disc
constexpr float kShotLogRange = 2.0f;     // shots passing this close are logged
constexpr float kAimedAtTommy = 1.5f;     // a ped's shot this close to Tommy's spine was aimed at him
// Fires: a ground fire's flames reach about this far around and up from its
// point; a burning car's (the fire follows the car) a little wider.
constexpr float kFireRadius = 1.2f, kEntityFireRadius = 2.0f, kFireHeight = 2.5f;
constexpr float kRotorThickness = 0.4f;  // m above or below the rotor disc
constexpr uint32_t kRotorCooldownMs = 250;
constexpr float kTommyHealth = 100.0f;   // what he has: all of it is all four props

fpv_drone *g_drone = nullptr;
float g_props[4][3];  // world positions of the props, latest step
float g_centre[3], g_up[3];
bool g_have_props = false;
bool g_slot_active[vc::kExplosionCount];
vc::CVector g_slot_pos[vc::kExplosionCount];
uint32_t g_rotor_ms = 0;
bool g_in_water = false;
// Fire and water soak the props a little every frame: one line each time it stops.
struct Soak {
    const char *what;
    float seconds = 0, amount[4] = {};
    bool now = false;
};
Soak g_fire{"in fire"}, g_water{"in water"};
float g_logged_damage[4];
int g_shots_logged = 0;

void hurt(int motor, float amount) { fpv.fpv_drone_damage_props(g_drone, motor, amount); }

void soak(Soak &s, int motor, float amount) {
    hurt(motor, amount);
    s.amount[motor] += amount;
    s.now = true;
}

void end_frame(Soak &s, float dt) {
    if (s.now) {
        s.seconds += dt;
    } else if (s.seconds > 0) {
        Log("damage: %s for %.1f s, props +%.2f +%.2f +%.2f +%.2f", s.what, s.seconds, s.amount[0], s.amount[1],
            s.amount[2], s.amount[3]);
        s = Soak{s.what};
    }
    s.now = false;
}

// libfpv's own hits too: a line whenever a prop is 0.05 worse than last said.
void log_prop_damage(const fpv_frame &f) {
    bool worse = false;
    for (int m = 0; m < 4; ++m) worse |= f.motors[m].prop_damage > g_logged_damage[m] + 0.05f;
    if (!worse) {
        for (int m = 0; m < 4; ++m) g_logged_damage[m] = std::min(g_logged_damage[m], f.motors[m].prop_damage);
        return;  // repaired or respawned: from there
    }
    for (int m = 0; m < 4; ++m) g_logged_damage[m] = f.motors[m].prop_damage;
    Log("damage: props now %.2f %.2f %.2f %.2f", g_logged_damage[0], g_logged_damage[1], g_logged_damage[2],
        g_logged_damage[3]);
}

// v rotated by quaternion q {x, y, z, w}.
void rotate(const float q[4], const float v[3], float out[3]) {
    float tx = 2 * (q[1] * v[2] - q[2] * v[1]), ty = 2 * (q[2] * v[0] - q[0] * v[2]),
          tz = 2 * (q[0] * v[1] - q[1] * v[0]);
    out[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
    out[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
    out[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
}

void update_props(const fpv_frame &f) {
    for (int m = 0; m < 4; ++m) {
        // hubs in drone-local host coords (right, forward, up); the prop sits a little above
        float local[3] = {f.motors[m].position[0], f.motors[m].position[1], f.motors[m].position[2] + 0.025f};
        if (local[0] == 0 && local[1] == 0) local[0] = m & 1 ? 0.08f : -0.08f, local[1] = m & 2 ? -0.08f : 0.08f;
        float w[3];
        rotate(f.rotation, local, w);
        for (int i = 0; i < 3; ++i) g_props[m][i] = f.position[i] + w[i];
    }
    const float up[3] = {0, 0, 1};
    rotate(f.rotation, up, g_up);
    for (int i = 0; i < 3; ++i) g_centre[i] = f.position[i];
    g_have_props = true;
}

// The sea, the bay, rivers and pools. Water isn't solid for the drone (the
// line of sight probes pass through it, world.cpp): it goes in, the water
// holds it back, and each prop under the surface is soaked.
void water(const fpv_frame &f, float dt) {
    float level = 0;
    bool centre_in = api.GetWaterLevel(f.position[0], f.position[1], f.position[2], &level, false) &&
                     f.position[2] < level;
    if (centre_in) {
        float k = 1.0f - std::exp(-kWaterDrag * dt), dv[3];
        for (int i = 0; i < 3; ++i) dv[i] = -f.velocity[i] * k;
        fpv.fpv_drone_push(g_drone, dv, 0);
    }
    if (centre_in != g_in_water) {
        g_in_water = centre_in;
        Log("damage: the drone %s the water at %.2f %.2f %.2f (surface %.2f, CWaterLevel)",
            centre_in ? "went into" : "came out of", f.position[0], f.position[1], f.position[2], level);
    }
    for (int m = 0; m < 4; ++m) {
        float l = 0;
        if (api.GetWaterLevel(g_props[m][0], g_props[m][1], g_props[m][2], &l, false) && g_props[m][2] < l)
            soak(g_water, m, kWaterPerSecond * dt);
    }
}

// gFireManager's fires: on the ground (molotovs, petrol, the flamethrower) or
// on an entity (a burning car or ped), whose position the fire follows.
void fires(float dt) {
    for (int i = 0; i < vc::kFireCount; ++i) {
        vc::CVector p;
        bool on_entity = false;
        if (!vc::fire_at(i, &p, &on_entity)) continue;
        float r = on_entity ? kEntityFireRadius : kFireRadius;
        for (int m = 0; m < 4; ++m) {
            float dx = g_props[m][0] - p.x, dy = g_props[m][1] - p.y, dz = g_props[m][2] - p.z;
            if (dx * dx + dy * dy > r * r || dz < -1.0f || dz > kFireHeight) continue;
            soak(g_fire, m, kFirePerSecond * dt);
        }
    }
}

// A slot is a new explosion when it turns active or moves (as in wind.cpp).
void explosions(const fpv_frame &f, bool apply) {
    for (int i = 0; i < vc::kExplosionCount; ++i) {
        vc::Explosion e = vc::explosion_at(i);
        bool moved = e.pos.x != g_slot_pos[i].x || e.pos.y != g_slot_pos[i].y || e.pos.z != g_slot_pos[i].z;
        bool fresh = e.active && (!g_slot_active[i] || moved);
        g_slot_active[i] = e.active;
        g_slot_pos[i] = e.pos;
        if (!fresh || !apply) continue;
        float dx = f.position[0] - e.pos.x, dy = f.position[1] - e.pos.y, dz = f.position[2] - e.pos.z;
        float r = std::max(1.0f, e.radius);
        if (dx * dx + dy * dy + dz * dz > r * r) continue;
        hurt(-1, kBlast);
        Log("damage: explosion type %d %.1f m from the drone (radius %.1f), all props +%.2f", e.type,
            std::sqrt(dx * dx + dy * dy + dz * dz), e.radius, kBlast);
    }
}

// Into a turning main rotor: the disc above the fuselage, as wind.cpp models its downwash.
void rotors(const fpv_frame &f) {
    if (*api.TimeInMilliseconds - g_rotor_ms < kRotorCooldownMs) return;
    for (int i = 0, count = vc::vehicle_pool_size(); i < count; ++i) {
        auto *v = static_cast<vc::CEntity *>(vc::vehicle_at(i));
        if (!v || !vc::is_real_heli(v) || !(vc::heli_rotor_speed(v) > 0.015f)) continue;
        const vc::CMatrix &m = *vc::entity_matrix(v);
        bool rc = vc::heli_is_rc(v);
        float radius = rc ? 0.4f : 5.0f, top = rc ? 0.2f : 1.8f;
        float d[3] = {f.position[0] - (m.pos.x + m.up.x * top), f.position[1] - (m.pos.y + m.up.y * top),
                      f.position[2] - (m.pos.z + m.up.z * top)};
        float off_plane = d[0] * m.up.x + d[1] * m.up.y + d[2] * m.up.z;
        float r2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2] - off_plane * off_plane;
        if (std::fabs(off_plane) > kRotorThickness || r2 > (radius + kPropRadius) * (radius + kPropRadius)) continue;
        hurt(-1, kRotor);
        Log("damage: helicopter rotor (%.1f m disc, %.1f m from its hub), all props +%.2f", radius, std::sqrt(r2),
            kRotor);
        g_rotor_ms = *api.TimeInMilliseconds;
        return;
    }
}

float dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

void nearest_on_segment(const float p[3], const float a[3], const float b[3], float out[3]) {
    float ab[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, ap[3] = {p[0] - a[0], p[1] - a[1], p[2] - a[2]};
    float len2 = dot3(ab, ab);
    float t = len2 > 1e-6f ? std::clamp(dot3(ap, ab) / len2, 0.0f, 1.0f) : 0;
    for (int i = 0; i < 3; ++i) out[i] = a[i] + ab[i] * t;
}

float distance(const float a[3], const float b[3]) {
    float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    return std::sqrt(dot3(d, d));
}

// The segment a..b through a prop's disc (centre c, the drone's up n).
bool through_prop(const float c[3], const float n[3], const float a[3], const float b[3]) {
    float ca[3] = {a[0] - c[0], a[1] - c[1], a[2] - c[2]}, cb[3] = {b[0] - c[0], b[1] - c[1], b[2] - c[2]};
    float da = dot3(ca, n), db = dot3(cb, n);
    if (da * db <= 0 && da != db) {
        float t = da / (da - db), p[3];
        for (int i = 0; i < 3; ++i) p[i] = a[i] + (b[i] - a[i]) * t;
        if (distance(p, c) < kPropRadius) return true;
    }
    float q[3];
    nearest_on_segment(c, a, b, q);
    float cq[3] = {q[0] - c[0], q[1] - c[1], q[2] - c[2]};
    return std::fabs(dot3(cq, n)) < kPropThickness && distance(q, c) < kPropRadius;
}

}  // namespace

void shot(vc::CEntity *owner, const vc::CVector &from, const vc::CVector &to) {
    if (!g_drone || !g_have_props) return;
    float a[3] = {from.x, from.y, from.z}, b[3] = {to.x, to.y, to.z};
    vc::CEntity *tommy = api.FindPlayerPed();
    bool by_tommy = owner && owner == tommy;
    // Tommy standing in at the drone: peds aim at his spine, which is on the
    // drone, but their aim isn't perfect. A shot aimed at him is aimed at the
    // drone: the same line, turned about the muzzle onto the drone, so it
    // misses the drone by as much as it missed him.
    bool reaimed = false;
    float spine[3];
    if (!by_tommy && standin::aim_point(spine)) {
        float q[3];
        nearest_on_segment(spine, a, b, q);
        float to_spine = distance(a, spine), length = distance(a, b);
        if (distance(q, spine) < kAimedAtTommy && to_spine > 0.5f) {
            float k = length / to_spine;
            for (int i = 0; i < 3; ++i) b[i] += (g_centre[i] - spine[i]) * k;
            reaimed = true;
        }
    }
    float q[3];
    nearest_on_segment(g_centre, a, b, q);
    float miss = distance(q, g_centre);
    if (miss > kShotLogRange) return;
    vc::EntityType type = owner ? vc::type_of(owner) : vc::EntityType::Nothing;
    const char *who = by_tommy                           ? "Tommy"
                      : type == vc::EntityType::Ped     ? (reaimed ? "a ped, aimed at Tommy there" : "a ped")
                      : type == vc::EntityType::Vehicle ? (reaimed ? "a vehicle, aimed at Tommy there" : "a vehicle")
                                                        : "someone";
    if (miss < kBodyRadius) {
        Log("damage: bullet from %s (%.0f m) through the body, %.2f m from the centre", who, distance(a, g_centre),
            miss);
        // The warhead is on the body: it goes off, armed or not. Without one, the drone falls.
        bool blown = false;
#if defined(LIBFPV_KAMIKAZE)
        blown = fpvapi::kamikaze() && fpv.fpv_kamikaze_detonate(g_drone, 1) == FPV_OK;
#endif
        if (!blown) hurt(-1, kBlast);
        return;
    }
    bool hit = false;
    for (int m = 0; m < 4; ++m)
        if (through_prop(g_props[m], g_up, a, b)) {
            hurt(m, kBullet);
            Log("damage: bullet from %s (%.0f m) through prop %d, broken", who, distance(a, g_centre), m);
            hit = true;
        }
    if (!hit && g_shots_logged++ < 200)
        Log("damage: bullet from %s (%.0f m) missed the drone by %.2f m (%+.2f %+.2f %+.2f)", who,
            distance(a, g_centre), miss, q[0] - g_centre[0], q[1] - g_centre[1], q[2] - g_centre[2]);
}

void stand_in_hit(float amount) {
    if (!g_drone || !(amount > 0)) return;
    // All of his health is all four props; the bullets are counted in shot()
    // (he has no collision there, so they never reach him).
    float props = amount / kTommyHealth;
    hurt(-1, props);
    static int reported = 0;
    if (reported++ < 20) Log("damage: Tommy hit at the drone for %.1f health, all props +%.2f", amount, props);
}

void update(fpv_drone *drone, const fpv_frame &frame, float dt) {
    bool fresh = g_drone != drone;
    g_drone = drone;
    update_props(frame);
    explosions(frame, !fresh);  // the explosions going on at the launch are history
    water(frame, dt);
    fires(dt);
    end_frame(g_water, dt);
    end_frame(g_fire, dt);
    rotors(frame);
    log_prop_damage(frame);
}

void reset() {
    g_drone = nullptr;
    g_have_props = false;
    g_in_water = false;
    g_fire = Soak{"in fire"}, g_water = Soak{"in water"};
    for (float &d : g_logged_damage) d = 0;
    g_shots_logged = 0;
}

}  // namespace damage
