#include "pilot.h"

#include "drone.h"
#include "log.h"
#include "model.h"

namespace pilot {
namespace {

using vc::api;

constexpr float kBlend = 4.0f;  // the game's usual blend speed

vc::CEntity *g_posed = nullptr;  // Tommy, in the pose

bool g_away = false;  // Tommy's real position is at the drone
vc::CVector g_home{};
bool g_swapped = false;  // Tommy is at home for this frame's render passes
vc::CVector g_saved_pos{};

void update_rw(vc::CEntity *e) {
    api.MatrixUpdateRW(vc::entity_matrix(e));
    api.UpdateRwFrame(e);
}

// A bone's position (world space) in the skinned clump's hierarchy.
bool bone(void *hierarchy, const float *matrices, int id, float out[3]) {
    int i = api.RpHAnimIDGetIndex(hierarchy, id);
    if (i < 0) return false;
    const float *pos = matrices + i * 16 + 12;  // RwMatrix: right, up, at, pos (4 floats each)
    out[0] = pos[0], out[1] = pos[1], out[2] = pos[2];
    return true;
}

}  // namespace

void hold(vc::CEntity *tommy) {
    render_end();  // a frame that never reached RenderEffects
    void *clump = tommy ? vc::rw_object(tommy) : nullptr;
    if (!clump) return;
    // Partial (upper body) and looped: his idle keeps his legs and head.
    // Asked for every step: the game's own idles (and scripts) blend theirs in.
    api.BlendAnimation(clump, vc::kAssocGroupStd, vc::kAnimIdleCam, kBlend);
    if (g_posed != tommy) Log("pilot: Tommy in the pilot's pose");
    g_posed = tommy;
}

void release() {
    render_end();
    vc::CEntity *tommy = g_posed;
    g_posed = nullptr;
    if (!tommy || tommy != api.FindPlayerPed() || !vc::rw_object(tommy)) return;
    if (void *pose = api.GetAssociation(vc::rw_object(tommy), vc::kAnimIdleCam)) {
        vc::anim_blend_delta(pose) = -kBlend;  // fades out, then the game deletes it
        vc::anim_flags(pose) |= vc::kAnimDeleteFadedOut;
    }
    Log("pilot: pose released");
}

void away(const vc::CVector *home) {
    render_end();
    g_away = home != nullptr;
    if (home) g_home = *home;
}

void render_begin() {
    if (!g_away || g_swapped) return;
    vc::CEntity *tommy = api.FindPlayerPed();
    if (!tommy || api.FindPlayerVehicle()) return;
    vc::CMatrix *m = vc::entity_matrix(tommy);
    g_saved_pos = m->pos;
    m->pos = g_home;
    vc::set_flag(tommy, vc::kVisible, true);
    update_rw(tommy);
    // The skin draws from world-space bone matrices, last computed at the drone.
    api.UpdateRpHAnim(tommy);
    g_swapped = true;
}

void render_end() {
    if (!g_swapped) return;
    g_swapped = false;
    vc::CEntity *tommy = api.FindPlayerPed();
    if (!tommy) return;
    vc::entity_matrix(tommy)->pos = g_saved_pos;
    // The renderer judged him at home, maybe off screen, which makes his
    // anims update the root only. At the drone he is in view: full updates.
    vc::set_flag(tommy, vc::kVisible, false);
    vc::set_flag(tommy, vc::kOffscreen, false);
    update_rw(tommy);
}

void draw_radio() {
    if (g_posed && !drone::active()) release();
    vc::CEntity *tommy = api.FindPlayerPed();
    if (!g_posed || tommy != g_posed || api.FindPlayerVehicle() || (g_away && !g_swapped)) return;
    void *clump = vc::rw_object(tommy);
    if (!clump) return;
    api.UpdateRpHAnim(tommy);  // his bones where he is drawn this frame
    void *hierarchy = api.GetAnimHierarchyFromSkinClump(clump);
    const float *matrices = hierarchy ? api.RpHAnimHierarchyGetMatrixArray(hierarchy) : nullptr;
    if (!matrices) return;
    float rh[3], rf[3], lh[3], lf[3], neck[3], head[3];
    if (!bone(hierarchy, matrices, vc::kBoneRightHand, rh) || !bone(hierarchy, matrices, vc::kBoneRightFinger, rf) ||
        !bone(hierarchy, matrices, vc::kBoneLeftHand, lh) || !bone(hierarchy, matrices, vc::kBoneLeftFinger, lf) ||
        !bone(hierarchy, matrices, vc::kBoneNeck, neck) || !bone(hierarchy, matrices, vc::kBoneHead, head))
        return;
    float right[3], left[3], face[3];
    for (int i = 0; i < 3; ++i) {
        right[i] = (rh[i] + rf[i]) * 0.5f;
        left[i] = (lh[i] + lf[i]) * 0.5f;
        face[i] = (neck[i] + head[i]) * 0.5f;
    }
    model::draw_radio(right, left, face);
}

}  // namespace pilot
