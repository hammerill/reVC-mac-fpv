#include "effects.h"

#include <chrono>

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
constexpr int64_t kCooldownMs = 500;  // one effect per entity per half second

struct Recent {
    uint32_t object_id;
    int64_t time;
};
Recent g_recent[16];

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
bool hit_ped(int handle, float speed, vc::CVector force) {
    vc::CEntity *ped = api.GetPed(handle);
    vc::CEntity *tommy = api.FindPlayerPed();
    if (!ped || ped == tommy || speed < kPedKnockSpeed) return false;
    float before = vc::ped_health(ped);
    api.PedInflictDamage(ped, tommy, vc::kWeaponRammedByCar, speed * kPedDamagePerMs, vc::kPedPieceTorso, 0);
    push(ped, force);
    Log("effects: ped %d hit at %.1f m/s, health %.0f -> %.0f", handle, speed, before, vc::ped_health(ped));
    return true;
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

void on_frame(const fpv_frame &frame) {
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
