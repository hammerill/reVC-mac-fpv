#include "world.h"

#include <cmath>
#include <cstddef>

#include "fpvapi.h"
#include "log.h"

namespace world {
namespace {

using vc::api;

vc::CEntity *g_ignore = nullptr;

// Kind and pool handle of an entity, packed as an fpv object_id.
uint32_t classify(vc::CEntity *e) {
    if (!e) return 0;
    switch (vc::type_of(e)) {
        case vc::EntityType::Ped: return uint32_t(Kind::Ped) << 28 | (uint32_t(api.GetPedRef(e)) & 0x0fffffffu);
        case vc::EntityType::Vehicle:
            return uint32_t(Kind::Vehicle) << 28 | (uint32_t(api.GetVehicleRef(e)) & 0x0fffffffu);
        case vc::EntityType::Object:
            return uint32_t(Kind::Object) << 28 | (uint32_t(api.GetObjectRef(e)) & 0x0fffffffu);
        default: return uint32_t(Kind::Other) << 28;
    }
}

// libfpv's friction by what was hit: car paint, rough ground (asphalt,
// pavement, grass, gravel, sand), or its smooth default.
uint8_t material_of(vc::CEntity *e, uint8_t surface) {
    if (e && vc::type_of(e) == vc::EntityType::Vehicle) return fpvapi::kMaterialVehicle;
    switch (surface) {
        case 1: case 2: case 3: case 4: case 5:  // tarmac, grass, gravel, dry mud, pavement
        case 18: case 25: case 26: case 33: case 34:  // sand, hedge, steep cliff, beach sand and concrete
            return fpvapi::kMaterialRough;
        default: return fpvapi::kMaterialSmooth;
    }
}

constexpr int kWaterSkips = 4;  // water surfaces in the collision the probe goes on through

}  // namespace

void ignore(vc::CEntity *entity) { g_ignore = entity; }

bool probe(const float origin[3], const float dir[3], float max_distance, fpv_raycast_hit *hit,
           vc::CEntity **entity_out, uint8_t *surface_out) {
    vc::CVector from{origin[0], origin[1], origin[2]};
    vc::CVector to{origin[0] + dir[0] * max_distance, origin[1] + dir[1] * max_distance,
                   origin[2] + dir[2] * max_distance};
    vc::CColPoint cp{};
    vc::CEntity *entity = nullptr;
    // Everything solid: buildings, vehicles, peds, objects and dummies, glass
    // and fences too (no see-through skipping). Water isn't solid: a drone
    // goes in (damage.cpp holds it back and soaks the props), so a hit on a
    // water surface goes on past it.
    vc::CEntity *saved = *api.pIgnoreEntity;
    *api.pIgnoreEntity = g_ignore;
    bool ok = false;
    for (int skip = 0; skip <= kWaterSkips; ++skip) {
        entity = nullptr;
        ok = api.ProcessLineOfSight(from, to, cp, entity, true, true, true, true, true, false, false, false);
        if (!ok || cp.surfaceB != vc::kSurfaceWater) break;
        from = {cp.point.x + dir[0] * 0.05f, cp.point.y + dir[1] * 0.05f, cp.point.z + dir[2] * 0.05f};
        ok = false;
    }
    *api.pIgnoreEntity = saved;
    if (!ok) return false;

    float dx = cp.point.x - origin[0], dy = cp.point.y - origin[1], dz = cp.point.z - origin[2];
    float distance = dx * dir[0] + dy * dir[1] + dz * dir[2];
    if (distance < 0 || distance > max_distance) return false;

    const vc::CVector &n = cp.normal;
    float len2 = n.x * n.x + n.y * n.y + n.z * n.z;
    if (len2 > 0.81f && len2 < 1.21f && n.x * dir[0] + n.y * dir[1] + n.z * dir[2] < 0) {
        hit->normal[0] = n.x;
        hit->normal[1] = n.y;
        hit->normal[2] = n.z;
    } else {  // no trustworthy normal: treat it as facing straight back at the ray
        hit->normal[0] = -dir[0];
        hit->normal[1] = -dir[1];
        hit->normal[2] = -dir[2];
    }
    hit->distance = distance;
    hit->material = material_of(entity, cp.surfaceB);
    hit->object_id = classify(entity);
    if (entity_out) *entity_out = entity;
    if (surface_out) *surface_out = cp.surfaceB;

    static vc::CEntity *last_entity = nullptr;
    static int traced = 0;
    if (traced < 10 || (entity != last_entity && traced < 60)) {
        ++traced;
        last_entity = entity;
        Log("world: hit d %.2f entity %p type %d id %08x surface %u pos %.2f %.2f %.2f n %.2f %.2f %.2f", distance,
            static_cast<void *>(entity), entity ? int(vc::type_of(entity)) : -1, hit->object_id, cp.surfaceB,
            cp.point.x, cp.point.y, cp.point.z, hit->normal[0], hit->normal[1], hit->normal[2]);
    }
    return true;
}

int raycast(void *, const float origin[3], const float dir[3], float max_distance, fpv_raycast_hit *hit) {
    return probe(origin, dir, max_distance, hit) ? 1 : 0;
}

int raycast_ex(void *, const float origin[3], const float dir[3], float max_distance, fpv_raycast_hit_ex *out) {
    vc::CEntity *entity = nullptr;
    if (!probe(origin, dir, max_distance, &out->hit, &entity)) return 0;
    Kind kind = kind_of(out->hit.object_id);
    bool moves = entity && handle_of(out->hit.object_id) &&
                 (kind == Kind::Ped || kind == Kind::Vehicle || kind == Kind::Object);
    if (!moves || out->struct_size < offsetof(fpv_raycast_hit_ex, object_rotation) + sizeof out->object_rotation)
        return 1;
    // Its pose as drawn this frame: columns right, forward, up.
    const vc::CMatrix &m = *vc::entity_matrix(entity);
    float m00 = m.right.x, m01 = m.forward.x, m02 = m.up.x, m10 = m.right.y, m11 = m.forward.y, m12 = m.up.y,
          m20 = m.right.z, m21 = m.forward.z, m22 = m.up.z;
    float *q = out->object_rotation, tr = m00 + m11 + m22;
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
    out->object_position[0] = m.pos.x, out->object_position[1] = m.pos.y, out->object_position[2] = m.pos.z;
    out->flags = FPV_RAYCAST_HIT_POSE;
    return 1;
}

}  // namespace world
