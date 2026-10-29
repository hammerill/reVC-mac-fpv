// The reVC (GTA Vice City for macOS, arm64) executable.
//
// Functions and globals are found by their exported symbol names with
// dlsym(). Struct field offsets can't be found that way: they were read from
// the game's own code, and init() re-checks each one against the instruction
// that uses it, so a different build turns the mod off instead of corrupting
// the game.
#pragma once

#include <cstdint>
#include <string>

namespace vc {

struct CVector {
    float x, y, z;
};

// CMatrix: right (actually left for the camera), forward, up, position.
struct CMatrix {
    CVector right;
    float pad0;
    CVector forward;
    float pad1;
    CVector up;
    float pad2;
    CVector pos;
    float pad3;
};

struct CColPoint {
    CVector point;
    int pad1;
    CVector normal;
    int pad2;
    uint8_t surfaceA, pieceA, surfaceB, pieceB;
    float depth;
};

struct CRGBA {
    uint8_t r, g, b, a;
};

struct RwV2d {
    float x, y;
};

struct CEntity;  // opaque; fields through the accessors below

enum class EntityType : uint8_t { Nothing = 0, Building = 1, Vehicle = 2, Ped = 3, Object = 4, Dummy = 5 };

// eSound: the frontend menu's sounds.
enum FrontendSound : uint16_t { kSoundHighlight = 195, kSoundSelect = 196, kSoundBack = 197, kSoundError = 198 };
constexpr int kWeaponRammedByCar = 39;
constexpr int kPedPieceTorso = 0;
constexpr int kExplosionGrenade = 0;
constexpr uint8_t kSurfacePlastic = 24;
constexpr int kAssocGroupStd = 0;
constexpr int kAnimIdleCam = 7;   // ANIM_STD_IDLE_CAM: a tourist holding a camera in both hands (partial, looped)
constexpr int kAnimIdle = 3;      // ANIM_STD_IDLE
constexpr int kBoneHead = 5, kBoneNeck = 4, kBoneRightHand = 24, kBoneRightFinger = 25, kBoneLeftHand = 34,
              kBoneLeftFinger = 35;

// Resolves everything and checks the offsets. False (and the reasons) when
// the running executable isn't the build the mod was made for.
bool init(std::string &error);

// --- function pointers (null until init() succeeded) ---------------------------
struct Api {
    // game functions the mod detours
    void (*CGame_Process)();
    void (*CCamera_Process)(void *camera);
    void (*RenderScene)();
    void (*CHud_Draw)();
    void (*CHud_DrawAfterFade)();
    void (*CMessages_Display)();
    void (*CDarkel_DrawMessages)();
    void (*CGarages_PrintMessages)();
    void (*CPickups_RenderPickUpText)();
    void (*DisplayRadioStationName)(void *music_manager);
    void (*CColStore_LoadCollision)(const float pos2d[2]);
    const CVector *(*FindPlayerCentreOfWorld)(int player);
    const CVector *(*FindPlayerCentreOfWorld_NoSniperShift)();
    CVector (*FindPlayerCoors)();
    void (*CClouds_RenderBackground)(int16_t top_r, int16_t top_g, int16_t top_b, int16_t bottom_r,
                                     int16_t bottom_g, int16_t bottom_b, int16_t alpha);
    void (*CClouds_RenderHorizon)();
    void (*CRenderer_ScanWorld)();
    void (*CRenderer_ScanSectorPoly)(RwV2d *poly, int count, void (*scan)(void *list));
    void (*RenderEffects)();
    void (*CPlayerPed_ProcessControl)(CEntity *ped);
    // PedInflictDamage (below) is detoured too

    // game functions the mod calls
    CEntity *(*FindPlayerPed)();
    CEntity *(*FindPlayerVehicle)();
    CEntity *(*FindPlayerEntity)();
    bool (*ProcessLineOfSight)(const CVector &from, const CVector &to, CColPoint &point, CEntity *&entity,
                               bool buildings, bool vehicles, bool peds, bool objects, bool dummies,
                               bool ignoreSeeThrough, bool ignoreSomeObjects, bool ignoreShootThrough);
    void (*CalculateDerivedValues)(void *camera);
    void (*SetFOV)(float fov);
    void (*LoadScene)(const CVector &pos);
    void (*AddCollisionNeededAtPosn)(const float pos2d[2]);
    void (*SetHeading)(void *matrix, float heading);
    int (*GetPedRef)(CEntity *);
    int (*GetVehicleRef)(CEntity *);
    int (*GetObjectRef)(CEntity *);
    CEntity *(*GetPed)(int);
    CEntity *(*GetVehicle)(int);
    CEntity *(*GetObject)(int);
    bool (*PedInflictDamage)(CEntity *ped, CEntity *by, int weapon, float damage, int piece, uint8_t direction);
    void (*VehicleInflictDamage)(CEntity *vehicle, CEntity *by, int weapon, float damage, CVector at);
    void (*ApplyMoveForce)(CEntity *physical, float x, float y, float z);
    bool (*AddExplosion)(CEntity *victim, CEntity *creator, int type, const CVector &pos, uint32_t lifetime,
                         bool sound);
    void (*PlayFrontEndSound)(void *dmaudio, uint16_t sound, uint32_t volume);
    void (*ReportCollision)(void *dmaudio, CEntity *a, CEntity *b, uint8_t surfaceA, uint8_t surfaceB, float power,
                            float velocity);
    using TeleportFn = void (*)(CEntity *entity, CVector pos);
    TeleportFn TeleportFns[4];  // CPed, CAutomobile, CBike, CBoat
    void (*ScanSectorList_Priority)(void *list);  // only compared with
    void (*DrawAnyRect)(float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4,
                        const CRGBA &c1, const CRGBA &c2, const CRGBA &c3, const CRGBA &c4);
    void (*WorldAdd)(CEntity *);
    void (*WorldRemove)(CEntity *);
    void (*MatrixUpdateRW)(CMatrix *matrix);  // an entity's matrix to its RwFrame
    void (*UpdateRwFrame)(CEntity *);
    void (*UpdateRpHAnim)(CEntity *);
    void *(*BlendAnimation)(void *clump, int group, int anim, float delta);
    void *(*GetAssociation)(void *clump, uint32_t anim);
    void *(*GetAnimHierarchyFromSkinClump)(void *clump);
    int (*RpHAnimIDGetIndex)(void *hierarchy, int bone);
    float *(*RpHAnimHierarchyGetMatrixArray)(void *hierarchy);  // RwMatrix[], 16 floats each
    void (*RemoveBuildingsNotInArea)(int area);

    // RenderWare (librw through re3's wrappers)
    void (*RwCameraSetNearClipPlane)(void *camera, float near_plane);
    float (*RwCameraGetFarClipPlane)(void *camera);
    void *(*RwCameraGetFrame)(void *camera);
    const float *(*RwCameraGetViewWindow)(void *camera);
    void *(*RwFrameGetMatrix)(void *frame);
    float *(*RwMatrixGetRight)(void *matrix);
    float *(*RwMatrixGetUp)(void *matrix);
    float *(*RwMatrixGetAt)(void *matrix);
    float *(*RwMatrixGetPos)(void *matrix);
    void (*RwMatrixUpdate)(void *matrix);
    void (*RwFrameUpdateObjects)(void *frame);
    void (*RwFrameOrthoNormalize)(void *frame);
    void *(*RwIm3DTransform)(void *vertices, uint32_t count, void *matrix, uint32_t flags);
    void (*RwIm3DRenderIndexedPrimitive)(int type, uint16_t *indices, int count);
    void (*RwIm3DEnd)();
    int (*RwRenderStateSet)(int state, void *value);
    int (*RwRenderStateGet)(int state, void *value);

    // globals
    uint8_t *TheCamera;
    void **Scene;  // {RpWorld *world; RwCamera *camera}
    uint8_t *Pads;
    void *DMAudio;
    void *MusicManager;
    CEntity **pIgnoreEntity;
    float *ms_fFOV;
    float *ms_fNearClipZ;  // CDraw's copy of the near plane: sprite depths and frustum tests
    float *CloudsCameraRoll, *CloudsHorizonTilt;
    CRGBA *CloudsColourTop, *CloudsColourBottom;
    int *currArea;
    uint8_t *RsGlobal;
    bool *UserPause;
    bool *CodePause;
    uint32_t *TimeInMilliseconds;
    uint8_t *ReplayMode;
    float *AmbientRed, *AmbientGreen, *AmbientBlue;
    float *DirectionalRed, *DirectionalGreen, *DirectionalBlue;
};
extern Api api;

// --- fields -------------------------------------------------------------------
CMatrix &camera_matrix();       // TheCamera's
void *scene_camera();           // the RwCamera the world renders through
float lod_distance();           // CRenderer's LOD_DISTANCE: 300 m times TheCamera's LOD multiplier
int screen_width();             // RsGlobal: the game's 2D coordinates
int screen_height();
CMatrix *entity_matrix(CEntity *e);
CVector entity_position(CEntity *e);
EntityType type_of(CEntity *e);
void *rw_object(CEntity *e);    // RpClump for peds
// CEntity's flag bits, counted from m_type at +0x60 (6 bytes of bit fields)
constexpr uint64_t kUsesCollision = 1ull << 8, kVisible = 1ull << 18, kOffscreen = 1ull << 42;
bool flag(CEntity *e, uint64_t flag);
void set_flag(CEntity *e, uint64_t flag, bool on);
CVector &move_speed(CEntity *physical);
CVector &turn_speed(CEntity *physical);
float &ped_health(CEntity *ped);
float &ped_rotation(CEntity *ped);  // m_fRotationCur, followed by m_fRotationDest
// CAnimBlendAssociation
float &anim_blend_delta(void *association);
uint16_t &anim_flags(void *association);
constexpr uint16_t kAnimDeleteFadedOut = 0x4;
uint16_t &pad_disabled_controls();  // CPad::DisablePlayerControls of pad 0
constexpr uint16_t kPadDisabledByFpv = 0x8000;  // our bit; scripts use the low ones

bool paused();          // pause menu or code pause
bool replay_playing();

// Moves the player (or their vehicle) through the entity's own Teleport.
bool teleport(CEntity *entity, CVector pos);

}  // namespace vc
