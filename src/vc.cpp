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

    R(RwCameraSetNearClipPlane, "_Z24RwCameraSetNearClipPlanePN2rw6CameraEf");
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
#undef R

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

CMatrix *entity_matrix(CEntity *e) { return &field<CMatrix>(e, off::EntityMatrix); }

CVector entity_position(CEntity *e) { return entity_matrix(e)->pos; }

EntityType type_of(CEntity *e) { return EntityType(field<uint8_t>(e, off::EntityType) & 7); }

float &ped_health(CEntity *ped) { return field<float>(ped, off::PedHealth); }

float &ped_rotation(CEntity *ped) { return field<float>(ped, off::PedRotationCur); }

uint16_t &pad_disabled_controls() { return field<uint16_t>(api.Pads, off::PadDisablePlayerControls); }

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
