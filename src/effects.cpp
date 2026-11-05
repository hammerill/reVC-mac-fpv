#include "effects.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "log.h"
#include "vc.h"
#include "world.h"

namespace effects {
namespace {

using vc::api;

// A 5" quad is ~0.65 kg; at 30 m/s that is a real hit for a pedestrian and a
// dent for a car. Speeds are the relative normal speed libfpv reports (m/s).
constexpr float kPedKnockSpeed = 4.0f;
constexpr float kPedDamagePerMs = 2.0f;  // health points per m/s (peds have 100)
constexpr float kCarDamageSpeed = 5.0f;
constexpr float kCarDamagePerMs = 3.0f;  // health points per m/s (cars have 1000)
constexpr float kDroneMass = 0.65f;
// The drone flies through peds (world::raycast leaves them out) and its
// spinning props cut them instead: harder than a bump, 10 m/s takes a full
// 100 health. A prop strike doesn't hurt the drone.
constexpr float kPropStrikeDamagePerMs = 10.0f;
constexpr float kPropStrikeRadius = 0.2f;  // a 5" quad's props reach ~0.17 m from the centre
constexpr float kPropSpinningRpm = 1000.0f;
constexpr int64_t kCooldownMs = 500;  // one effect per entity per half second

struct Recent {
    uint32_t object_id;
    int64_t time;
};
Recent g_recent[16];
bool g_prop_strikes = true;

int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

bool cooling_down(uint32_t object_id) {
    int64_t now = now_ms();
    for (const Recent &r : g_recent)
        if (r.object_id == object_id && now - r.time < kCooldownMs) return true;
    return false;
}

void start_cooldown(uint32_t object_id) {
    Recent *oldest = &g_recent[0];
    for (Recent &r : g_recent)
        if (r.time < oldest->time) oldest = &r;
    *oldest = {object_id, now_ms()};
}

// The drone's momentum as a CPhysical move force: the game keeps speeds in
// metres per 1/50 s, so an impulse in kg m/s is divided by 50.
vc::CVector push_of(const fpv_frame &frame) {
    float k = kDroneMass / 50.0f;
    return {frame.velocity[0] * k, frame.velocity[1] * k, frame.velocity[2] * k};
}

void push(vc::CEntity *e, vc::CVector f) { api.ApplyMoveForce(e, f.x, f.y, f.z); }

// Tommy flies the drone, so as far as the game is concerned he hit them.
bool hit_ped(int handle, float speed, vc::CVector force, float damage_per_ms = kPedDamagePerMs) {
    vc::CEntity *ped = api.GetPed(handle);
    vc::CEntity *tommy = api.FindPlayerPed();
    if (!ped || ped == tommy || speed < kPedKnockSpeed) return false;
    float before = vc::ped_health(ped);
    api.PedInflictDamage(ped, tommy, vc::kWeaponRammedByCar, speed * damage_per_ms, vc::kPedPieceTorso, 0);
    push(ped, force);
    Log("effects: ped %d hit at %.1f m/s, health %.0f -> %.0f", handle, speed, before, vc::ped_health(ped));
    return true;
}

bool props_spinning(const fpv_frame &frame) {
    if (!(frame.flags & FPV_DRONE_ARMED)) return false;
    for (const fpv_motor_state &m : frame.motors)
        if (m.rpm > kPropSpinningRpm && m.prop_damage < 1.0f && !(m.flags & FPV_MOTOR_PROP_BROKEN)) return true;
    return false;
}

// Peds the drone passed through since the last frame, cut by its props.
void prop_strikes(const fpv_frame &frame) {
    static float last[3];
    static bool have_last = false;
    bool spinning = props_spinning(frame);
    float from[3] = {last[0], last[1], last[2]};
    bool had = have_last;
    for (int i = 0; i < 3; ++i) last[i] = frame.position[i];
    have_last = true;
    if (!spinning || !g_prop_strikes) return;
    if (!had) std::copy(frame.position, frame.position + 3, from);
    float dx = frame.position[0] - from[0], dy = frame.position[1] - from[1], dz = frame.position[2] - from[2];
    if (dx * dx + dy * dy + dz * dz > 25.0f) std::copy(frame.position, frame.position + 3, from);  // a reset or teleport

    const float *v = frame.velocity;
    float speed = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (speed < kPedKnockSpeed) return;
    vc::CEntity *ped = world::sweep_peds(from, frame.position, kPropStrikeRadius);
    if (!ped) return;
    int handle = api.GetPedRef(ped);
    uint32_t id = uint32_t(world::Kind::Ped) << 28 | (uint32_t(handle) & 0x0fffffffu);
    if (cooling_down(id)) return;
    if (hit_ped(handle, speed, push_of(frame), kPropStrikeDamagePerMs)) start_cooldown(id);
}

bool hit_vehicle(int handle, float speed, vc::CVector force, const float at[3]) {
    vc::CEntity *car = api.GetVehicle(handle);
    if (!car || speed < kCarDamageSpeed) return false;
    push(car, force);
    api.VehicleInflictDamage(car, api.FindPlayerPed(), vc::kWeaponRammedByCar, speed * kCarDamagePerMs,
                             {at[0], at[1], at[2]});
    Log("effects: vehicle %d hit at %.1f m/s", handle, speed);
    return true;
}

bool hit_object(int handle, float speed, vc::CVector force) {
    vc::CEntity *object = api.GetObject(handle);
    if (!object) return false;
    push(object, force);
    Log("effects: object %d hit at %.1f m/s", handle, speed);
    return true;
}

}  // namespace

void set_prop_strikes(bool on) { g_prop_strikes = on; }

void on_frame(const fpv_frame &frame) {
    prop_strikes(frame);
    for (uint32_t i = 0; i < frame.impact_count && i < FPV_MAX_IMPACTS; ++i) {
        const fpv_impact &im = frame.impacts[i];
        int handle = int(world::handle_of(im.object_id));
        if (!handle || im.speed < 1.0f || cooling_down(im.object_id)) continue;
        bool applied = false;
        switch (world::kind_of(im.object_id)) {
            case world::Kind::Ped: applied = hit_ped(handle, im.speed, push_of(frame)); break;
            case world::Kind::Vehicle: applied = hit_vehicle(handle, im.speed, push_of(frame), im.position); break;
            case world::Kind::Object: applied = hit_object(handle, im.speed, push_of(frame)); break;
            default: break;
        }
        if (applied) start_cooldown(im.object_id);
    }
}

}  // namespace effects
