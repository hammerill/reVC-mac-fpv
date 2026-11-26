#include "vc.h"

#include <dlfcn.h>

#include <cstdio>
#include <cstring>

namespace vc {

Api api;

namespace {

// Field offsets in the arm64 reVC executable, each re-checked in kChecks.
namespace off {
constexpr size_t CameraMatrix = 0x00;  // CPlaceable has no vtable: its CMatrix comes first
constexpr size_t EntityMatrix = 0x08;
constexpr size_t EntityType = 0x60;    // low 3 bits: type, high 5: status
constexpr size_t PedHealth = 0x44C;
constexpr size_t PedRotationCur = 0x46C;  // m_fRotationDest follows
constexpr size_t PadDisablePlayerControls = 0xF0;
constexpr size_t SceneCamera = 1;  // Scene.camera, in pointers
constexpr size_t CameraLodMultiplier = 0xF8;
constexpr size_t RsGlobalWidth = 0x8;  // height follows
constexpr size_t EntityRwObject = 0x58;
constexpr size_t EntityFlags = 0x60;  // m_type, m_status, flagsA..E: 48 bits
constexpr size_t PhysicalMoveSpeed = 0x84;
constexpr size_t PhysicalTurnSpeed = 0x90;
constexpr size_t AnimBlendDelta = 0x2C;
constexpr size_t AnimFlags = 0x3E;
constexpr size_t PedIK = 0x2C0;
constexpr size_t VehicleHandling = 0x160;
constexpr size_t HandlingFlags = 0xCC;
constexpr uint32_t HandlingIsHeli = 0x20000;
constexpr size_t PhysicalMass = 0xD0;
constexpr size_t AutomobileRotorSpeed = 0x5F0;  // m_aWheelSpeed[1]
constexpr size_t PoolEntries = 0x0, PoolFlags = 0x8, PoolSize = 0x10;
constexpr size_t VehicleSize = 0x6F0;  // a vehicle pool entry
constexpr size_t Fires = 0x8, FireSize = 0x38, FireOngoing = 0x0, FirePos = 0x4, FireEntity = 0x10;
constexpr size_t ExplosionRadius = 0x10, ExplosionIteration = 0x2C;  // from m_ExplosionType, 0x40 each
}  // namespace off

struct OffsetCheck {
    const char *symbol;
    uint32_t offset;
    uint32_t instruction;
};

// Read with objdump from the macOS reVC build. Comments name the field whose
// offset the instruction encodes.
constexpr OffsetCheck kChecks[] = {
    {"_ZN4CPad20GetSteeringLeftRightEv", 0x0, 0x7941e008},     // ldrh w8, [x0, #0xf0]: DisablePlayerControls
    {"_ZN4CPed20CalculateNewVelocityEv", 0x38, 0xbd444e60},   // ldr s0, [x19, #0x44c]: m_fHealth
    {"_ZN4CPed20CalculateNewVelocityEv", 0x50, 0xbd446e60},   // ldr s0, [x19, #0x46c]: m_fRotationCur
    {"_ZN4CPed20CalculateNewVelocityEv", 0xdc, 0xbd447261},   // ldr s1, [x19, #0x470]: m_fRotationDest
    {"_Z15FindPlayerCoorsv", 0x5c, 0x9100e128},               // add x8, x9, #0x38: entity matrix position
    {"_Z8SpawnCari", 0x244, 0x9278e929},                      // CEntity::m_type / m_status bits
    {"_Z16RwCameraGetFramePKN2rw6CameraE", 0x0, 0xf9400400},  // ldr x0, [x0, #0x8]
    {"_ZN10CPlaceable10SetHeadingEf", 0x14, 0xbd403808},      // ldr s8, [x0, #0x38]: takes the CMatrix
    {"_ZN7CClouds16RenderBackgroundEsssssss", 0x58, 0x91008280},  // add x0, x20, #0x20: TheCamera.GetUp()
    {"_ZN7CClouds16RenderBackgroundEsssssss", 0xb4, 0xbd400b41},  // ldr s1, [x26, #0x8]: SCREEN_WIDTH
    {"_ZN9CRenderer9ScanWorldEv", 0x108, 0xbd40faa0},             // ldr s0, [x21, #0xf8]: LODDistMultiplier
    {"_ZN9CRenderer21SetupEntityVisibilityEP7CEntity", 0x9c, 0xf9402e69},  // ldr x9, [x19, #0x58]: m_rwObject
    {"_ZN9CRenderer21SetupEntityVisibilityEP7CEntity", 0xa0, 0x926e0108},  // and x8, x8, #0x40000: bIsVisible
    {"_ZN6CWorld28ProcessLineOfSightSectorListER8CPtrListRK8CColLineR9CColPointRfRP7CEntitybbb", 0x1b8,
     0x374001c8},                                                   // tbnz w8, #0x8: bUsesCollision
    {"_ZN6CWorld7ProcessEv", 0x18c, 0xb75001e8},                    // tbnz x8, #0x2a: bOffscreen
    {"_ZN9CPhysical14ApplyMoveForceEfff", 0x18, 0xfc484001},        // ldur d1, [x0, #0x84]: m_vecMoveSpeed
    {"_ZN9CPhysical14ApplyTurnForceEffffff", 0x84, 0xfd404a61},     // ldr d1, [x19, #0x90]: m_vecTurnSpeed
    {"_ZN21CAnimBlendAssociation11UpdateBlendEf", 0x10, 0x2d450402},  // ldp s2, s1, [x0, #0x28]: blendDelta
    {"_ZN21CAnimBlendAssociation11UpdateBlendEf", 0x30, 0x3940fa68},  // ldrb w8, [x19, #0x3e]: flags
    {"_ZN4CPed16InvestigateEventEv", 0x4c0, 0x528000e2},            // mov w2, #0x7: ANIM_STD_IDLE_CAM
    {"_ZN7CWeapon14FireInstantHitEP7CEntityP7CVector", 0x540, 0x910b02e0},  // add x0, x23, #0x2c0: m_pedIK
    {"_ZN11CAutomobile14ProcessControlEv", 0x22b8, 0xf940b268},     // ldr x8, [x19, #0x160]: pHandling
    {"_ZN11CAutomobile14ProcessControlEv", 0x22bc, 0xb940cd08},     // ldr w8, [x8, #0xcc]: handling Flags
    {"_ZN11CAutomobile14ProcessControlEv", 0x2eb8, 0xbd40d260},     // ldr s0, [x19, #0xd0]: m_fMass
    {"_ZN11CAutomobile14ProcessControlEv", 0x3df4, 0xbd45f260},     // ldr s0, [x19, #0x5f0]: m_aWheelSpeed[1]
    {"_ZN6CPools10GetVehicleEi", 0x28, 0x5280de0a},          // mov w10, #0x6f0: vehicle pool entry size
    {"_ZN12CFireManager15FindNearestFireE7CVectorPf", 0x4, 0x9100400a},   // add x10, x0, #0x10: m_aFires
    {"_ZN12CFireManager15FindNearestFireE7CVectorPf", 0x20, 0x9100e14a},  // add x10, x10, #0x38: sizeof(CFire)
    {"_ZN10CExplosion25GetExplosionActiveCounterEh", 0x10, 0x39c0b500},   // ldrsb w0, [x8, #0x2d]
    {"_ZN10CExplosion20GetExplosionPositionEh", 0xc, 0x8b081928},         // add x8, x9, x8, lsl #6: 0x40 each
};

template <typename T>
void resolve(T &out, const char *symbol, std::string &missing) {
    void *p = dlsym(RTLD_MAIN_ONLY, symbol);
    if (!p) missing += std::string("  missing symbol ") + symbol + "\n";
    out = reinterpret_cast<T>(p);
}

template <typename T>
T &field(void *base, size_t offset) {
    return *reinterpret_cast<T *>(static_cast<uint8_t *>(base) + offset);
}

}  // namespace

bool init(std::string &error) {
    std::string missing;
#define R(member, symbol) resolve(api.member, symbol, missing)
    R(CGame_Process, "_ZN5CGame7ProcessEv");
    R(CCamera_Process, "_ZN7CCamera7ProcessEv");
    R(RenderScene, "_Z11RenderScenev");
    R(CHud_Draw, "_ZN4CHud4DrawEv");
    R(CHud_DrawAfterFade, "_ZN4CHud13DrawAfterFadeEv");
    R(CMessages_Display, "_ZN9CMessages7DisplayEv");
    R(CDarkel_DrawMessages, "_ZN7CDarkel12DrawMessagesEv");
    R(CGarages_PrintMessages, "_ZN8CGarages13PrintMessagesEv");
    R(CPickups_RenderPickUpText, "_ZN8CPickups16RenderPickUpTextEv");
    R(DisplayRadioStationName, "_ZN13cMusicManager23DisplayRadioStationNameEv");
    R(CColStore_LoadCollision, "_ZN9CColStore13LoadCollisionERK9CVector2D");
    R(FindPlayerCentreOfWorld, "_Z23FindPlayerCentreOfWorldi");
    R(FindPlayerCentreOfWorld_NoSniperShift, "_Z37FindPlayerCentreOfWorld_NoSniperShiftv");
    R(FindPlayerCoors, "_Z15FindPlayerCoorsv");
    R(CClouds_RenderBackground, "_ZN7CClouds16RenderBackgroundEsssssss");
    R(CClouds_RenderHorizon, "_ZN7CClouds13RenderHorizonEv");
    R(CRenderer_ScanWorld, "_ZN9CRenderer9ScanWorldEv");
    R(CRenderer_ScanSectorPoly, "_ZN9CRenderer14ScanSectorPolyEPN2rw3V2dEiPFvP8CPtrListE");
    R(RenderEffects, "_Z13RenderEffectsv");
    R(CPlayerPed_ProcessControl, "_ZN10CPlayerPed14ProcessControlEv");
    R(cAudioManager_Service, "_ZN13cAudioManager7ServiceEv");
    R(CWeapon_DoBulletImpact, "_ZN7CWeapon14DoBulletImpactEP7CEntityS1_P7CVectorS3_P9CColPoint9CVector2D");

    R(FindPlayerPed, "_Z13FindPlayerPedv");
    R(FindPlayerVehicle, "_Z17FindPlayerVehiclev");
    R(FindPlayerEntity, "_Z16FindPlayerEntityv");
    R(ProcessLineOfSight, "_ZN6CWorld18ProcessLineOfSightERK7CVectorS2_R9CColPointRP7CEntitybbbbbbbb");
    R(CalculateDerivedValues, "_ZN7CCamera22CalculateDerivedValuesEv");
    R(SetFOV, "_ZN5CDraw6SetFOVEf");
    R(LoadScene, "_ZN10CStreaming9LoadSceneERK7CVector");
    R(AddCollisionNeededAtPosn, "_ZN9CColStore24AddCollisionNeededAtPosnERK9CVector2D");
    R(SetHeading, "_ZN10CPlaceable10SetHeadingEf");
    R(GetPedRef, "_ZN6CPools9GetPedRefEP4CPed");
    R(GetVehicleRef, "_ZN6CPools13GetVehicleRefEP8CVehicle");
    R(GetObjectRef, "_ZN6CPools12GetObjectRefEP7CObject");
    R(GetPed, "_ZN6CPools6GetPedEi");
    R(GetVehicle, "_ZN6CPools10GetVehicleEi");
    R(GetObject, "_ZN6CPools9GetObjectEi");
    R(PedInflictDamage, "_ZN4CPed13InflictDamageEP7CEntity11eWeaponTypef14ePedPieceTypesh");
    R(VehicleInflictDamage, "_ZN8CVehicle13InflictDamageEP7CEntity11eWeaponTypef7CVector");
    R(ApplyMoveForce, "_ZN9CPhysical14ApplyMoveForceEfff");
    R(AddExplosion, "_ZN10CExplosion12AddExplosionEP7CEntityS1_14eExplosionTypeRK7CVectorjb");
    R(PlayFrontEndSound, "_ZN8cDMAudio17PlayFrontEndSoundEtj");
    R(ReportCollision, "_ZN8cDMAudio15ReportCollisionEP7CEntityS1_hhff");
    R(TeleportFns[0], "_ZN4CPed8TeleportE7CVector");
    R(TeleportFns[1], "_ZN11CAutomobile8TeleportE7CVector");
    R(TeleportFns[2], "_ZN5CBike8TeleportE7CVector");
    R(TeleportFns[3], "_ZN5CBoat8TeleportE7CVector");
    R(ScanSectorList_Priority, "_ZN9CRenderer23ScanSectorList_PriorityEP8CPtrList");
    R(DrawAnyRect, "_ZN9CSprite2d11DrawAnyRectEffffffffRK5CRGBAS2_S2_S2_");
    R(WorldAdd, "_ZN6CWorld3AddEP7CEntity");
    R(WorldRemove, "_ZN6CWorld6RemoveEP7CEntity");
    R(MatrixUpdateRW, "_ZN7CMatrix8UpdateRWEv");
    R(UpdateRwFrame, "_ZN7CEntity13UpdateRwFrameEv");
    R(UpdateRpHAnim, "_ZN7CEntity13UpdateRpHAnimEv");
    R(BlendAnimation, "_ZN12CAnimManager14BlendAnimationEPN2rw5ClumpE12AssocGroupId11AnimationIdf");
    R(GetAssociation, "_Z30RpAnimBlendClumpGetAssociationPN2rw5ClumpEj");
    R(GetAnimHierarchyFromSkinClump, "_Z29GetAnimHierarchyFromSkinClumpPN2rw5ClumpE");
    R(RpHAnimIDGetIndex, "_Z17RpHAnimIDGetIndexPN2rw14HAnimHierarchyEi");
    R(RpHAnimHierarchyGetMatrixArray, "_Z30RpHAnimHierarchyGetMatrixArrayPN2rw14HAnimHierarchyE");
    R(RemoveBuildingsNotInArea, "_ZN10CStreaming24RemoveBuildingsNotInAreaEi");
    R(GetWaterLevel, "_ZN11CWaterLevel13GetWaterLevelEfffPfb");
    R(GetExplosionPosition, "_ZN10CExplosion20GetExplosionPositionEh");
    R(GetComponentPosition, "_ZN6CPedIK20GetComponentPositionERN2rw3V3dEj");

    R(RwCameraSetNearClipPlane, "_Z24RwCameraSetNearClipPlanePN2rw6CameraEf");
    R(RwCameraGetFarClipPlane, "_Z23RwCameraGetFarClipPlanePKN2rw6CameraE");
    R(RwCameraGetFrame, "_Z16RwCameraGetFramePKN2rw6CameraE");
    R(RwCameraGetViewWindow, "_Z21RwCameraGetViewWindowPKN2rw6CameraE");
    R(RwFrameGetMatrix, "_Z16RwFrameGetMatrixPN2rw5FrameE");
    R(RwMatrixGetRight, "_Z16RwMatrixGetRightPN2rw6MatrixE");
    R(RwMatrixGetUp, "_Z13RwMatrixGetUpPN2rw6MatrixE");
    R(RwMatrixGetAt, "_Z13RwMatrixGetAtPN2rw6MatrixE");
    R(RwMatrixGetPos, "_Z14RwMatrixGetPosPN2rw6MatrixE");
    R(RwMatrixUpdate, "_Z14RwMatrixUpdatePN2rw6MatrixE");
    R(RwFrameUpdateObjects, "_Z20RwFrameUpdateObjectsPN2rw5FrameE");
    R(RwFrameOrthoNormalize, "_Z21RwFrameOrthoNormalizePN2rw5FrameE");
    R(RwIm3DTransform, "_Z15RwIm3DTransformPN2rw3gl310Im3DVertexEjPNS_6MatrixEj");
    R(RwIm3DRenderIndexedPrimitive, "_Z28RwIm3DRenderIndexedPrimitive15RwPrimitiveTypePti");
    R(RwIm3DEnd, "_Z9RwIm3DEndv");
    R(RwRenderStateSet, "_Z16RwRenderStateSet13RwRenderStatePv");
    R(RwRenderStateGet, "_Z16RwRenderStateGet13RwRenderStatePv");

    R(TheCamera, "TheCamera");
    R(Scene, "Scene");
    R(Pads, "Pads");
    R(DMAudio, "DMAudio");
    R(MusicManager, "MusicManager");
    R(pIgnoreEntity, "_ZN6CWorld13pIgnoreEntityE");
    R(ms_fFOV, "_ZN5CDraw7ms_fFOVE");
    R(ms_fNearClipZ, "_ZN5CDraw13ms_fNearClipZE");
    R(CloudsCameraRoll, "_ZN7CClouds13ms_cameraRollE");
    R(CloudsHorizonTilt, "_ZN7CClouds14ms_HorizonTiltE");
    R(CloudsColourTop, "_ZN7CClouds12ms_colourTopE");
    R(CloudsColourBottom, "_ZN7CClouds15ms_colourBottomE");
    R(currArea, "_ZN5CGame8currAreaE");
    R(RsGlobal, "RsGlobal");
    R(UserPause, "_ZN6CTimer11m_UserPauseE");
    R(CodePause, "_ZN6CTimer11m_CodePauseE");
    R(TimeInMilliseconds, "_ZN6CTimer22m_snTimeInMillisecondsE");
    R(ReplayMode, "_ZN7CReplay4ModeE");
    R(AmbientRed, "_ZN10CTimeCycle20m_fCurrentAmbientRedE");
    R(AmbientGreen, "_ZN10CTimeCycle22m_fCurrentAmbientGreenE");
    R(AmbientBlue, "_ZN10CTimeCycle21m_fCurrentAmbientBlueE");
    R(DirectionalRed, "_ZN10CTimeCycle24m_fCurrentDirectionalRedE");
    R(DirectionalGreen, "_ZN10CTimeCycle26m_fCurrentDirectionalGreenE");
    R(DirectionalBlue, "_ZN10CTimeCycle25m_fCurrentDirectionalBlueE");
    R(Wind, "_ZN8CWeather4WindE");
    R(FireManager, "gFireManager");
    R(VehiclePool, "_ZN6CPools15ms_pVehiclePoolE");
    R(AutomobileVtable, "_ZTV11CAutomobile");
    R(HeliVtable, "_ZTV5CHeli");
#undef R
    // A vptr points two slots into the vtable (offset to top, typeinfo).
    if (api.AutomobileVtable) api.AutomobileVtable = static_cast<uint8_t *>(api.AutomobileVtable) + 16;
    if (api.HeliVtable) api.HeliVtable = static_cast<uint8_t *>(api.HeliVtable) + 16;

    for (const OffsetCheck &check : kChecks) {
        auto *code = static_cast<const uint8_t *>(dlsym(RTLD_MAIN_ONLY, check.symbol));
        if (!code) {
            missing += std::string("  missing symbol ") + check.symbol + "\n";
            continue;
        }
        uint32_t instruction;
        std::memcpy(&instruction, code + check.offset, sizeof instruction);
        if (instruction != check.instruction) {
            char line[256];
            std::snprintf(line, sizeof line, "  %s+0x%x: expected %08x, found %08x\n", check.symbol, check.offset,
                          check.instruction, instruction);
            missing += line;
        }
    }
    if (!missing.empty()) {
        error = "this reVC build doesn't match the one the mod was made for:\n" + missing;
        api = Api{};
        return false;
    }
    return true;
}

CMatrix &camera_matrix() { return field<CMatrix>(api.TheCamera, off::CameraMatrix); }

void *scene_camera() { return api.Scene[off::SceneCamera]; }

float lod_distance() { return 300.0f * field<float>(api.TheCamera, off::CameraLodMultiplier); }

int screen_width() { return field<int32_t>(api.RsGlobal, off::RsGlobalWidth); }

int screen_height() { return field<int32_t>(api.RsGlobal, off::RsGlobalWidth + 4); }

CMatrix *entity_matrix(CEntity *e) { return &field<CMatrix>(e, off::EntityMatrix); }

CVector entity_position(CEntity *e) { return entity_matrix(e)->pos; }

EntityType type_of(CEntity *e) { return EntityType(field<uint8_t>(e, off::EntityType) & 7); }

void *rw_object(CEntity *e) { return field<void *>(e, off::EntityRwObject); }

// The flags are bytes (6 of them): touch only the byte that holds the bit.
bool flag(CEntity *e, uint64_t bit) {
    int i = __builtin_ctzll(bit);
    return field<uint8_t>(e, off::EntityFlags + i / 8) & (1u << (i % 8));
}

void set_flag(CEntity *e, uint64_t bit, bool on) {
    int i = __builtin_ctzll(bit);
    uint8_t &b = field<uint8_t>(e, off::EntityFlags + i / 8);
    b = on ? uint8_t(b | 1u << (i % 8)) : uint8_t(b & ~(1u << (i % 8)));
}

CVector &move_speed(CEntity *physical) { return field<CVector>(physical, off::PhysicalMoveSpeed); }

CVector &turn_speed(CEntity *physical) { return field<CVector>(physical, off::PhysicalTurnSpeed); }

float &anim_blend_delta(void *association) { return field<float>(association, off::AnimBlendDelta); }

uint16_t &anim_flags(void *association) { return field<uint16_t>(association, off::AnimFlags); }

float &ped_health(CEntity *ped) { return field<float>(ped, off::PedHealth); }

float &ped_rotation(CEntity *ped) { return field<float>(ped, off::PedRotationCur); }

uint16_t &pad_disabled_controls() { return field<uint16_t>(api.Pads, off::PadDisablePlayerControls); }

void *vehicle_at(int index) {
    void *pool = *api.VehiclePool;
    if (!pool || index < 0 || index >= field<int32_t>(pool, off::PoolSize)) return nullptr;
    if (field<uint8_t *>(pool, off::PoolFlags)[index] & 0x80) return nullptr;  // free
    return field<uint8_t *>(pool, off::PoolEntries) + size_t(index) * off::VehicleSize;
}

int vehicle_pool_size() {
    void *pool = *api.VehiclePool;
    return pool ? field<int32_t>(pool, off::PoolSize) : 0;
}

bool is_real_heli(CEntity *v) {
    void *vptr = *reinterpret_cast<void **>(v);
    if (vptr == api.HeliVtable) return true;
    if (vptr != api.AutomobileVtable) return false;
    uint8_t *handling = field<uint8_t *>(v, off::VehicleHandling);
    return handling && (field<uint32_t>(handling, off::HandlingFlags) & off::HandlingIsHeli);
}

float heli_rotor_speed(CEntity *v) {
    if (*reinterpret_cast<void **>(v) == api.HeliVtable) return 0.22f;  // they only exist flying
    return field<float>(v, off::AutomobileRotorSpeed);
}

bool heli_is_rc(CEntity *v) {
    return *reinterpret_cast<void **>(v) != api.HeliVtable && field<float>(v, off::PhysicalMass) <= 150.0f;
}

float heli_lift_mass(CEntity *v) {
    if (*reinterpret_cast<void **>(v) == api.HeliVtable) return 4500.0f;  // a police Maverick (handling.cfg POLMAV)
    if (heli_is_rc(v)) return 3.0f;
    float m = field<float>(v, off::PhysicalMass);
    return m > 0 ? (m < 10000.0f ? m : 10000.0f) : 0.0f;  // the Hunter is the heaviest, 10 t
}

float &mass(CEntity *physical) { return field<float>(physical, off::PhysicalMass); }

bool fire_at(int index, CVector *pos, bool *on_entity) {
    uint8_t *fire = api.FireManager + off::Fires + size_t(index) * off::FireSize;
    if (!field<uint8_t>(fire, off::FireOngoing)) return false;
    *pos = field<CVector>(fire, off::FirePos);
    *on_entity = field<void *>(fire, off::FireEntity) != nullptr;
    return true;
}

Explosion explosion_at(int index) {
    auto *e = reinterpret_cast<uint8_t *>(api.GetExplosionPosition(uint8_t(index))) - 4;
    return {field<int32_t>(e, 0), field<CVector>(e, 4), field<float>(e, off::ExplosionRadius),
            field<uint8_t>(e, off::ExplosionIteration) != 0};
}

CVector aim_point(CEntity *ped) {
    CVector p{};
    api.GetComponentPosition(static_cast<uint8_t *>(static_cast<void *>(ped)) + off::PedIK, p, 1);  // PED_MID
    return p;
}

bool paused() { return *api.UserPause || *api.CodePause; }

bool replay_playing() { return *api.ReplayMode == 1; }

bool teleport(CEntity *entity, CVector pos) {
    // Teleport() is virtual (vtable slot 12). Only call it if the slot holds
    // one of the implementations we know.
    Api::TeleportFn fn = (*reinterpret_cast<Api::TeleportFn **>(entity))[12];
    for (Api::TeleportFn known : api.TeleportFns)
        if (known == fn) {
            fn(entity, pos);
            return true;
        }
    return false;
}

}  // namespace vc
