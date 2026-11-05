// vc.h for the web build: the mod is compiled into reVC, so the game's
// functions, globals and fields come straight from its headers. The macOS
// version (src/vc.cpp) finds them in a finished executable instead.
#include "vc.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// A few of the fields the mod reads are private to their classes (the timer,
// the replay mode, CDraw's near plane): this file reaches them as the macOS
// build does, through the memory layout, only by name.
#define private public
#define protected public
#include "common.h"
#include "fpvhooks.h"

#include "AnimManager.h"
#include "AnimBlendAssociation.h"
#include "Automobile.h"
#include "Bike.h"
#include "Boat.h"
#include "Camera.h"
#include "Clouds.h"
#include "ColStore.h"
#include "DMAudio.h"
#include "Darkel.h"
#include "Draw.h"
#include "Explosion.h"
#include "Game.h"
#include "Garages.h"
#include "Hud.h"
#include "Messages.h"
#include "MusicManager.h"
#include "Object.h"
#include "Pad.h"
#include "Pickups.h"
#include "PlayerPed.h"
#include "Pools.h"
#include "Renderer.h"
#include "Replay.h"
#include "RpAnimBlend.h"
#include "RwHelper.h"
#include "Sprite2d.h"
#include "Streaming.h"
#include "Timecycle.h"
#include "Timer.h"
#include "World.h"
#include "main.h"
#include "skeleton.h"
#undef private
#undef protected

void RenderScene(void);
void RenderEffects(void);
CVector FindPlayerCoors(void);

namespace vc {

Api api;

namespace {

::CVector to_game(const CVector &v) { return ::CVector(v.x, v.y, v.z); }
CVector from_game(const ::CVector &v) { return {v.x, v.y, v.z}; }
::CEntity *game(CEntity *e) { return reinterpret_cast<::CEntity *>(e); }
CEntity *mod(::CEntity *e) { return reinterpret_cast<CEntity *>(e); }

// The hooked functions, as the plain functions the mod's hooks call to run
// the original (each enters the game function, whose FPV_HOOK guard lets it
// through to its own body).
void w_CGame_Process() { CGame::Process(); }
void w_CCamera_Process(void *camera) { static_cast<CCamera *>(camera)->Process(); }
void w_RenderScene() { RenderScene(); }
void w_CHud_Draw() { CHud::Draw(); }
void w_CHud_DrawAfterFade() { CHud::DrawAfterFade(); }
void w_CMessages_Display() { CMessages::Display(); }
void w_CDarkel_DrawMessages() { CDarkel::DrawMessages(); }
void w_CGarages_PrintMessages() { CGarages::PrintMessages(); }
void w_CPickups_RenderPickUpText() { CPickups::RenderPickUpText(); }
void w_DisplayRadioStationName(void *music) { static_cast<cMusicManager *>(music)->DisplayRadioStationName(); }
void w_CColStore_LoadCollision(const float pos[2]) { CColStore::LoadCollision(CVector2D(pos[0], pos[1])); }
const CVector *w_FindPlayerCentreOfWorld(int player) {
    return reinterpret_cast<const CVector *>(&::FindPlayerCentreOfWorld(player));
}
const CVector *w_FindPlayerCentreOfWorld_NoSniperShift() {
    return reinterpret_cast<const CVector *>(&::FindPlayerCentreOfWorld_NoSniperShift());
}
CVector w_FindPlayerCoors() { return from_game(::FindPlayerCoors()); }
void w_CClouds_RenderBackground(int16_t tr, int16_t tg, int16_t tb, int16_t br, int16_t bg, int16_t bb, int16_t a) {
    CClouds::RenderBackground(tr, tg, tb, br, bg, bb, a);
}
void w_CClouds_RenderHorizon() { CClouds::RenderHorizon(); }
void w_CRenderer_ScanWorld() { CRenderer::ScanWorld(); }
void w_CRenderer_ScanSectorPoly(RwV2d *poly, int count, void (*scan)(void *list)) {
    CRenderer::ScanSectorPoly(reinterpret_cast<rw::V2d *>(poly), count, reinterpret_cast<void (*)(CPtrList *)>(scan));
}
void w_RenderEffects() { RenderEffects(); }
void w_CPlayerPed_ProcessControl(CEntity *ped) { reinterpret_cast<CPlayerPed *>(ped)->ProcessControl(); }
bool w_PedInflictDamage(CEntity *ped, CEntity *by, int weapon, float damage, int piece, uint8_t direction) {
    return reinterpret_cast<CPed *>(ped)->InflictDamage(game(by), eWeaponType(weapon), damage, ePedPieceTypes(piece),
                                                         direction);
}

// What the mod calls.
CEntity *w_FindPlayerPed() { return mod(::FindPlayerPed()); }
CEntity *w_FindPlayerVehicle() { return mod(::FindPlayerVehicle()); }
CEntity *w_FindPlayerEntity() { return mod(::FindPlayerEntity()); }
bool w_ProcessLineOfSight(const CVector &from, const CVector &to, CColPoint &point, CEntity *&entity, bool buildings,
                          bool vehicles, bool peds, bool objects, bool dummies, bool see_through, bool some_objects,
                          bool shoot_through) {
    ::CColPoint cp;
    ::CEntity *hit = nullptr;
    bool ok = CWorld::ProcessLineOfSight(to_game(from), to_game(to), cp, hit, buildings, vehicles, peds, objects,
                                         dummies, see_through, some_objects, shoot_through);
    if (ok) {
        point.point = from_game(cp.point);
        point.normal = from_game(cp.normal);
        point.surfaceA = cp.surfaceA, point.pieceA = cp.pieceA;
        point.surfaceB = cp.surfaceB, point.pieceB = cp.pieceB;
        point.depth = cp.depth;
        entity = mod(hit);
    }
    return ok;
}
void w_CalculateDerivedValues(void *camera) { static_cast<CCamera *>(camera)->CalculateDerivedValues(); }
void w_SetFOV(float fov) { CDraw::SetFOV(fov); }
void w_LoadScene(const CVector &pos) { CStreaming::LoadScene(to_game(pos)); }
void w_AddCollisionNeededAtPosn(const float pos[2]) { CColStore::AddCollisionNeededAtPosn(CVector2D(pos[0], pos[1])); }
void w_SetHeading(void *matrix, float heading) { static_cast<CPlaceable *>(matrix)->SetHeading(heading); }
int w_GetPedRef(CEntity *e) { return CPools::GetPedRef(reinterpret_cast<CPed *>(e)); }
int w_GetVehicleRef(CEntity *e) { return CPools::GetVehicleRef(reinterpret_cast<CVehicle *>(e)); }
int w_GetObjectRef(CEntity *e) { return CPools::GetObjectRef(reinterpret_cast<CObject *>(e)); }
CEntity *w_GetPed(int h) { return mod(CPools::GetPed(h)); }
CEntity *w_GetVehicle(int h) { return mod(CPools::GetVehicle(h)); }
CEntity *w_GetObject(int h) { return mod(CPools::GetObject(h)); }
void w_VehicleInflictDamage(CEntity *vehicle, CEntity *by, int weapon, float damage, CVector at) {
    reinterpret_cast<CVehicle *>(vehicle)->InflictDamage(game(by), eWeaponType(weapon), damage, to_game(at));
}
void w_ApplyMoveForce(CEntity *e, float x, float y, float z) { reinterpret_cast<CPhysical *>(e)->ApplyMoveForce(x, y, z); }
bool w_AddExplosion(CEntity *victim, CEntity *creator, int type, const CVector &pos, uint32_t lifetime, bool sound) {
    return CExplosion::AddExplosion(game(victim), game(creator), eExplosionType(type), to_game(pos), lifetime, sound);
}
void w_PlayFrontEndSound(void *, uint16_t sound, uint32_t volume) { DMAudio.PlayFrontEndSound(sound, volume); }
void w_ReportCollision(void *, CEntity *a, CEntity *b, uint8_t sa, uint8_t sb, float power, float velocity) {
    DMAudio.ReportCollision(game(a), game(b), sa, sb, power, velocity);
}
void w_DrawAnyRect(float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4, const CRGBA &c1,
                   const CRGBA &c2, const CRGBA &c3, const CRGBA &c4) {
    auto c = [](const CRGBA &v) { return ::CRGBA(v.r, v.g, v.b, v.a); };
    CSprite2d::DrawAnyRect(x1, y1, x2, y2, x3, y3, x4, y4, c(c1), c(c2), c(c3), c(c4));
}
void w_WorldAdd(CEntity *e) { CWorld::Add(game(e)); }
void w_WorldRemove(CEntity *e) { CWorld::Remove(game(e)); }
void w_MatrixUpdateRW(CMatrix *m) { reinterpret_cast<::CMatrix *>(m)->UpdateRW(); }
void w_UpdateRwFrame(CEntity *e) { game(e)->UpdateRwFrame(); }
void w_UpdateRpHAnim(CEntity *e) { game(e)->UpdateRpHAnim(); }
void *w_BlendAnimation(void *clump, int group, int anim, float delta) {
    return CAnimManager::BlendAnimation(static_cast<RpClump *>(clump), AssocGroupId(group), AnimationId(anim), delta);
}
void *w_GetAssociation(void *clump, uint32_t anim) {
    return RpAnimBlendClumpGetAssociation(static_cast<RpClump *>(clump), anim);
}
void *w_GetAnimHierarchyFromSkinClump(void *clump) {
    return ::GetAnimHierarchyFromSkinClump(static_cast<RpClump *>(clump));
}
int w_RpHAnimIDGetIndex(void *hierarchy, int bone) {
    return ::RpHAnimIDGetIndex(static_cast<RpHAnimHierarchy *>(hierarchy), bone);
}
float *w_RpHAnimHierarchyGetMatrixArray(void *hierarchy) {
    return reinterpret_cast<float *>(::RpHAnimHierarchyGetMatrixArray(static_cast<RpHAnimHierarchy *>(hierarchy)));
}
void w_RemoveBuildingsNotInArea(int area) { CStreaming::RemoveBuildingsNotInArea(area); }

void w_RwCameraSetNearClipPlane(void *cam, float n) { ::RwCameraSetNearClipPlane(static_cast<RwCamera *>(cam), n); }
float w_RwCameraGetFarClipPlane(void *cam) { return ::RwCameraGetFarClipPlane(static_cast<RwCamera *>(cam)); }
void *w_RwCameraGetFrame(void *cam) { return ::RwCameraGetFrame(static_cast<RwCamera *>(cam)); }
const float *w_RwCameraGetViewWindow(void *cam) {
    return reinterpret_cast<const float *>(::RwCameraGetViewWindow(static_cast<RwCamera *>(cam)));
}
void *w_RwFrameGetMatrix(void *frame) { return ::RwFrameGetMatrix(static_cast<RwFrame *>(frame)); }
float *w_RwMatrixGetRight(void *m) { return reinterpret_cast<float *>(::RwMatrixGetRight(static_cast<RwMatrix *>(m))); }
float *w_RwMatrixGetUp(void *m) { return reinterpret_cast<float *>(::RwMatrixGetUp(static_cast<RwMatrix *>(m))); }
float *w_RwMatrixGetAt(void *m) { return reinterpret_cast<float *>(::RwMatrixGetAt(static_cast<RwMatrix *>(m))); }
float *w_RwMatrixGetPos(void *m) { return reinterpret_cast<float *>(::RwMatrixGetPos(static_cast<RwMatrix *>(m))); }
void w_RwMatrixUpdate(void *m) { ::RwMatrixUpdate(static_cast<RwMatrix *>(m)); }
void w_RwFrameUpdateObjects(void *f) { ::RwFrameUpdateObjects(static_cast<RwFrame *>(f)); }
void w_RwFrameOrthoNormalize(void *f) { ::RwFrameOrthoNormalize(static_cast<RwFrame *>(f)); }
void *w_RwIm3DTransform(void *vertices, uint32_t count, void *matrix, uint32_t flags) {
    return ::RwIm3DTransform(static_cast<RwIm3DVertex *>(vertices), count, static_cast<RwMatrix *>(matrix), flags);
}
void w_RwIm3DRenderIndexedPrimitive(int type, uint16_t *indices, int count) {
    ::RwIm3DRenderIndexedPrimitive(RwPrimitiveType(type), indices, count);
}
void w_RwIm3DEnd() { ::RwIm3DEnd(); }
int w_RwRenderStateSet(int state, void *value) { return ::RwRenderStateSet(RwRenderState(state), value); }
int w_RwRenderStateGet(int state, void *value) { return ::RwRenderStateGet(RwRenderState(state), value); }

}  // namespace

bool init(std::string &) {
    api.CGame_Process = w_CGame_Process;
    api.CCamera_Process = w_CCamera_Process;
    api.RenderScene = w_RenderScene;
    api.CHud_Draw = w_CHud_Draw;
    api.CHud_DrawAfterFade = w_CHud_DrawAfterFade;
    api.CMessages_Display = w_CMessages_Display;
    api.CDarkel_DrawMessages = w_CDarkel_DrawMessages;
    api.CGarages_PrintMessages = w_CGarages_PrintMessages;
    api.CPickups_RenderPickUpText = w_CPickups_RenderPickUpText;
    api.DisplayRadioStationName = w_DisplayRadioStationName;
    api.CColStore_LoadCollision = w_CColStore_LoadCollision;
    api.FindPlayerCentreOfWorld = w_FindPlayerCentreOfWorld;
    api.FindPlayerCentreOfWorld_NoSniperShift = w_FindPlayerCentreOfWorld_NoSniperShift;
    api.FindPlayerCoors = w_FindPlayerCoors;
    api.CClouds_RenderBackground = w_CClouds_RenderBackground;
    api.CClouds_RenderHorizon = w_CClouds_RenderHorizon;
    api.CRenderer_ScanWorld = w_CRenderer_ScanWorld;
    api.CRenderer_ScanSectorPoly = w_CRenderer_ScanSectorPoly;
    api.RenderEffects = w_RenderEffects;
    api.CPlayerPed_ProcessControl = w_CPlayerPed_ProcessControl;

    api.FindPlayerPed = w_FindPlayerPed;
    api.FindPlayerVehicle = w_FindPlayerVehicle;
    api.FindPlayerEntity = w_FindPlayerEntity;
    api.ProcessLineOfSight = w_ProcessLineOfSight;
    api.CalculateDerivedValues = w_CalculateDerivedValues;
    api.SetFOV = w_SetFOV;
    api.LoadScene = w_LoadScene;
    api.AddCollisionNeededAtPosn = w_AddCollisionNeededAtPosn;
    api.SetHeading = w_SetHeading;
    api.GetPedRef = w_GetPedRef;
    api.GetVehicleRef = w_GetVehicleRef;
    api.GetObjectRef = w_GetObjectRef;
    api.GetPed = w_GetPed;
    api.GetVehicle = w_GetVehicle;
    api.GetObject = w_GetObject;
    api.PedInflictDamage = w_PedInflictDamage;
    api.VehicleInflictDamage = w_VehicleInflictDamage;
    api.ApplyMoveForce = w_ApplyMoveForce;
    api.AddExplosion = w_AddExplosion;
    api.PlayFrontEndSound = w_PlayFrontEndSound;
    api.ReportCollision = w_ReportCollision;
    for (auto &fn : api.TeleportFns) fn = nullptr;  // vc::teleport calls the virtual directly here
    api.ScanSectorList_Priority = reinterpret_cast<void (*)(void *)>(&CRenderer::ScanSectorList_Priority);
    api.DrawAnyRect = w_DrawAnyRect;
    api.WorldAdd = w_WorldAdd;
    api.WorldRemove = w_WorldRemove;
    api.MatrixUpdateRW = w_MatrixUpdateRW;
    api.UpdateRwFrame = w_UpdateRwFrame;
    api.UpdateRpHAnim = w_UpdateRpHAnim;
    api.BlendAnimation = w_BlendAnimation;
    api.GetAssociation = w_GetAssociation;
    api.GetAnimHierarchyFromSkinClump = w_GetAnimHierarchyFromSkinClump;
    api.RpHAnimIDGetIndex = w_RpHAnimIDGetIndex;
    api.RpHAnimHierarchyGetMatrixArray = w_RpHAnimHierarchyGetMatrixArray;
    api.RemoveBuildingsNotInArea = w_RemoveBuildingsNotInArea;

    api.RwCameraSetNearClipPlane = w_RwCameraSetNearClipPlane;
    api.RwCameraGetFarClipPlane = w_RwCameraGetFarClipPlane;
    api.RwCameraGetFrame = w_RwCameraGetFrame;
    api.RwCameraGetViewWindow = w_RwCameraGetViewWindow;
    api.RwFrameGetMatrix = w_RwFrameGetMatrix;
    api.RwMatrixGetRight = w_RwMatrixGetRight;
    api.RwMatrixGetUp = w_RwMatrixGetUp;
    api.RwMatrixGetAt = w_RwMatrixGetAt;
    api.RwMatrixGetPos = w_RwMatrixGetPos;
    api.RwMatrixUpdate = w_RwMatrixUpdate;
    api.RwFrameUpdateObjects = w_RwFrameUpdateObjects;
    api.RwFrameOrthoNormalize = w_RwFrameOrthoNormalize;
    api.RwIm3DTransform = w_RwIm3DTransform;
    api.RwIm3DRenderIndexedPrimitive = w_RwIm3DRenderIndexedPrimitive;
    api.RwIm3DEnd = w_RwIm3DEnd;
    api.RwRenderStateSet = w_RwRenderStateSet;
    api.RwRenderStateGet = w_RwRenderStateGet;

    api.TheCamera = reinterpret_cast<uint8_t *>(&TheCamera);
    api.Scene = reinterpret_cast<void **>(&Scene);
    api.Pads = reinterpret_cast<uint8_t *>(CPad::GetPad(0));
    api.DMAudio = &DMAudio;
    api.MusicManager = &MusicManager;
    api.pIgnoreEntity = reinterpret_cast<CEntity **>(&CWorld::pIgnoreEntity);
    api.ms_fFOV = &CDraw::ms_fFOV;
    api.ms_fNearClipZ = &CDraw::ms_fNearClipZ;
    api.CloudsCameraRoll = &CClouds::ms_cameraRoll;
    api.CloudsHorizonTilt = &CClouds::ms_HorizonTilt;
    api.CloudsColourTop = reinterpret_cast<CRGBA *>(&CClouds::ms_colourTop);
    api.CloudsColourBottom = reinterpret_cast<CRGBA *>(&CClouds::ms_colourBottom);
    api.currArea = &CGame::currArea;
    api.RsGlobal = reinterpret_cast<uint8_t *>(&RsGlobal);
    api.UserPause = &CTimer::m_UserPause;
    api.CodePause = &CTimer::m_CodePause;
    api.TimeInMilliseconds = &CTimer::m_snTimeInMilliseconds;
    api.ReplayMode = &CReplay::Mode;
    api.AmbientRed = &CTimeCycle::m_fCurrentAmbientRed;
    api.AmbientGreen = &CTimeCycle::m_fCurrentAmbientGreen;
    api.AmbientBlue = &CTimeCycle::m_fCurrentAmbientBlue;
    api.DirectionalRed = &CTimeCycle::m_fCurrentDirectionalRed;
    api.DirectionalGreen = &CTimeCycle::m_fCurrentDirectionalGreen;
    api.DirectionalBlue = &CTimeCycle::m_fCurrentDirectionalBlue;
    return true;
}

CMatrix &camera_matrix() { return *reinterpret_cast<CMatrix *>(&TheCamera.GetMatrix()); }

void *scene_camera() { return Scene.camera; }

float lod_distance() { return 300.0f * TheCamera.LODDistMultiplier; }

int screen_width() { return RsGlobal.width; }

int screen_height() { return RsGlobal.height; }

CMatrix *entity_matrix(CEntity *e) { return reinterpret_cast<CMatrix *>(&game(e)->GetMatrix()); }

CVector entity_position(CEntity *e) { return from_game(game(e)->GetPosition()); }

EntityType type_of(CEntity *e) { return EntityType(game(e)->GetType()); }

void *rw_object(CEntity *e) { return game(e)->m_rwObject; }

bool flag(CEntity *e, uint64_t bit) {
    ::CEntity *g = game(e);
    if (bit == kUsesCollision) return g->bUsesCollision;
    if (bit == kVisible) return g->bIsVisible;
    if (bit == kOffscreen) return g->bOffscreen;
    return false;
}

void set_flag(CEntity *e, uint64_t bit, bool on) {
    ::CEntity *g = game(e);
    if (bit == kUsesCollision) g->bUsesCollision = on;
    if (bit == kVisible) g->bIsVisible = on;
    if (bit == kOffscreen) g->bOffscreen = on;
}

CVector &move_speed(CEntity *physical) {
    return *reinterpret_cast<CVector *>(&reinterpret_cast<CPhysical *>(physical)->m_vecMoveSpeed);
}

CVector &turn_speed(CEntity *physical) {
    return *reinterpret_cast<CVector *>(&reinterpret_cast<CPhysical *>(physical)->m_vecTurnSpeed);
}

float &anim_blend_delta(void *association) { return static_cast<CAnimBlendAssociation *>(association)->blendDelta; }

uint16_t &anim_flags(void *association) {
    return *reinterpret_cast<uint16_t *>(&static_cast<CAnimBlendAssociation *>(association)->flags);
}

float &ped_health(CEntity *ped) { return reinterpret_cast<CPed *>(ped)->m_fHealth; }

float &ped_rotation(CEntity *ped) { return reinterpret_cast<CPed *>(ped)->m_fRotationCur; }

uint16_t &pad_disabled_controls() { return CPad::GetPad(0)->DisablePlayerControls; }

bool paused() { return CTimer::m_UserPause || CTimer::m_CodePause; }

bool replay_playing() { return CReplay::IsPlayingBack(); }

bool teleport(CEntity *entity, CVector pos) {
    ::CEntity *e = game(entity);
    if (!e->IsPed() && !e->IsVehicle()) return false;
    e->Teleport(to_game(pos));
    return true;
}

}  // namespace vc
