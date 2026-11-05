// The web build's Demolition Man demo.
//
//  - A new game starts Avery Carrington's "Demolition Man" (mission 19)
//    where the main script would start the intro.
//  - The mission's RC Goblin is the FPV drone. Once the player gets the
//    helicopter (remote control, HUD back), a drone flight is bound to it:
//    every frame the helicopter (frozen, invisible, silent) takes the drone's
//    pose, so the script's checks (picking a bomb up within 0.7 m, the drop
//    height, the targets, the range) and the workers' gunfire all see the
//    drone. Its props cut the peds they touch, as the RC rotor does; hard
//    crashes cost the helicopter health instead of its own collision damage.
//    Left Ctrl, Space or the beeper switch (AUX3) is the fire button that
//    drops the bomb.
//  - A failed mission starts again by itself; a passed one shows the stats
//    and leaves Vice City to free flight (F7).
//  - The game's texts say "FPV Drone" where they said "RC Helicopter".
#include "common.h"
#include "fpvhooks.h"

#include <emscripten.h>
#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "Automobile.h"
#include "Camera.h"
#include "Clock.h"
#include "DMAudio.h"
#include "Frontend.h"
#include "Messages.h"
#include "ModelIndices.h"
#include "Pad.h"
#include "Ped.h"
#include "PlayerInfo.h"
#include "Pools.h"
#include "Script.h"
#include "Stats.h"
#include "Text.h"
#include "Timer.h"
#include "World.h"

#include "drone.h"
#include "effects.h"
#include "input.h"
#include "log.h"
#include "world.h"

namespace mission {
namespace {

constexpr int kIntro = 1;            // "Intro", the first mission of a new game
constexpr int kDemolitionMan = 19;   // Avery Carrington's RC helicopter mission
constexpr uint32_t kRestartDelayMs = 4000;
constexpr float kPropReach = 0.35f;  // centre to prop tip, a 5" quad, with a little margin
constexpr float kPropMinRpm = 3000;  // spinning fast enough to cut
constexpr float kCrashSpeed = 6.0f;  // m/s: harder hits hurt the helicopter (a hard landing is ~5)
constexpr float kCrashDamage = 40;   // health per m/s above that: the mission gives the Goblin 1400

enum class State { Waiting, Running, Failed, Passed };
State g_state = State::Waiting;
bool g_launched = false;       // the intro was swapped for Demolition Man
bool g_seen_on_mission = false;
int32 g_passed_before = 0;
uint32_t g_failed_at = 0;      // game time

// The bound helicopter.
CVehicle *g_heli = nullptr;
bool g_bound_once = false;     // this attempt: the help toast was shown
bool g_camera_taken = false;   // this attempt: the script's ending took the camera; no flying after that

// This attempt's numbers.
int g_attempt = 0;
double g_flight_start = 0;     // page time, ms
int g_cuts = 0;                // peds the props cut down
int g_crashes = 0;
bool g_aux3_was = false;
bool g_test_fire = false;  // testing aid (?testbomb): drop now

double now_ms() { return emscripten_get_now(); }

void js_toast(const char *text, int ms) {
    EM_ASM({ if (window.revcFpv) window.revcFpv.toast(UTF8ToString($0), $1); }, text, ms);
}

void js_result(const std::string &html) {
    EM_ASM({ if (window.revcFpv) window.revcFpv.result(UTF8ToString($0)); }, html.c_str());
}

// --- the drone as the RC helicopter ------------------------------------------

// libfpv's rotation maps the host's right/up/forward (east, up, north) to the world.
void rotate(const float q[4], const float v[3], float out[3]) {
    float tx = 2 * (q[1] * v[2] - q[2] * v[1]), ty = 2 * (q[2] * v[0] - q[0] * v[2]),
          tz = 2 * (q[0] * v[1] - q[1] * v[0]);
    out[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
    out[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
    out[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
}

bool controls_free() {
    uint16 bits = CPad::GetPad(0)->DisablePlayerControls & ~0x8000;  // 0x8000: the drone's own bit
    return bits == 0;
}

// heli_alive: the helicopter is still the player's remote vehicle (else it
// may already be deleted).
void unbind(const char *why, bool heli_alive) {
    if (!g_heli) return;
    Log("mission: drone unbound from the helicopter (%s)", why);
    drone::rc_stop();
    world::ignore(nullptr);
    if (heli_alive && g_heli->GetStatus() != STATUS_WRECKED) DMAudio.SetEntityStatus(g_heli->m_audioEntityId, TRUE);
    g_heli = nullptr;
    effects::set_prop_strikes(true);
}

void bind(CVehicle *heli) {
    const CVector &p = heli->GetPosition();
    const CVector &f = heli->GetForward();
    float heading = std::atan2(-f.x, f.y);
    CVector tommy = FindPlayerPed()->GetPosition();
    vc::CVector pos{p.x, p.y, p.z}, pilot{tommy.x, tommy.y, tommy.z};
    world::ignore(reinterpret_cast<vc::CEntity *>(heli));  // the drone flies inside it (and starts under it)
    if (!drone::rc_start(pos, heading, pilot)) {
        world::ignore(nullptr);
        Log("mission: the drone could not start");
        return;
    }
    g_heli = heli;
    effects::set_prop_strikes(false);  // the helicopter's rotor cuts the peds (follow_drone), and counts them
    heli->bIsFrozen = true;      // our pose only: no flying model, no physics
    heli->bInfiniteMass = true;
    DMAudio.SetEntityStatus(heli->m_audioEntityId, FALSE);  // the drone's motors are the sound
    if (g_flight_start == 0) g_flight_start = now_ms();
    Log("mission: drone bound to the RC Goblin at %.2f %.2f %.2f", p.x, p.y, p.z);
    if (!g_bound_once) {
        g_bound_once = true;
        js_toast("You are the FPV Drone. F8: radio setup · Left Ctrl / beeper switch: drop the bomb · C: camera", 9000);
    }
}

// Peds alive within reach of the props, so the ones they cut can be counted.
int alive_near(const CVector &at, float r, CPed **out, int max) {
    int n = 0;
    CPedPool *pool = CPools::GetPedPool();
    for (int i = pool->GetSize() - 1; i >= 0 && n < max; --i) {
        CPed *ped = pool->GetSlot(i);
        if (!ped || ped->m_fHealth <= 0 || ped == FindPlayerPed()) continue;
        if ((ped->GetPosition() - at).MagnitudeSqr() < r * r) out[n++] = ped;
    }
    return n;
}

void follow_drone() {
    fpv_frame f;
    if (!drone::frame(&f)) return;
    static const float kRight[3] = {1, 0, 0}, kUp[3] = {0, 0, 1}, kForward[3] = {0, 1, 0};
    float r[3], u[3], fw[3];
    rotate(f.rotation, kRight, r);
    rotate(f.rotation, kUp, u);
    rotate(f.rotation, kForward, fw);
    CMatrix &m = g_heli->GetMatrix();
    m.GetRight() = CVector(r[0], r[1], r[2]);
    m.GetForward() = CVector(fw[0], fw[1], fw[2]);
    m.GetUp() = CVector(u[0], u[1], u[2]);
    m.GetPosition() = CVector(f.position[0], f.position[1], f.position[2]);
    g_heli->m_vecMoveSpeed = CVector(0.0f, 0.0f, 0.0f);
    g_heli->m_vecTurnSpeed = CVector(0.0f, 0.0f, 0.0f);
    g_heli->bIsVisible = false;   // the drone's own model shows in the third-person views
    g_heli->RemoveAndAdd();       // in the world sectors where it now is, for the workers' bullets
    g_heli->GetMatrix().UpdateRW();
    g_heli->UpdateRwFrame();

    // The props, while they spin.
    float rpm = 0;
    for (const fpv_motor_state &motor : f.motors) rpm = motor.rpm > rpm ? motor.rpm : rpm;
    if (rpm > kPropMinRpm) {
        CPed *near[16];
        int n = alive_near(g_heli->GetPosition(), 3.0f, near, 16);
        if (n) {
            // BladeColSectorList (and so DoDronePropCollision) always returns
            // false for peds, so the cuts are the peds it left dead.
            g_heli->DoDronePropCollision(kPropReach);
            for (int i = 0; i < n; ++i)
                if (near[i]->m_fHealth <= 0) {
                    ++g_cuts;
                    Log("mission: the props cut a ped down (%d so far)", g_cuts);
                }
        }
    }

    // Crashes: the hardest contact of a frame (a landing touches with every
    // motor at once), at most one every half second.
    float hardest = 0;
    for (uint32_t i = 0; i < f.impact_count && i < FPV_MAX_IMPACTS; ++i)
        hardest = f.impacts[i].speed > hardest ? f.impacts[i].speed : hardest;
    static double last_crash = 0;
    if (hardest >= kCrashSpeed && now_ms() - last_crash > 500) {
        last_crash = now_ms();
        float damage = (hardest - kCrashSpeed) * kCrashDamage;
        ++g_crashes;
        g_heli->m_fHealth = Max(g_heli->m_fHealth - damage, 0.0f);
        Log("mission: crash at %.1f m/s, helicopter health %.0f", hardest, g_heli->m_fHealth);
    }
}

void update_binding() {
    CVehicle *rc = CWorld::Players[0].m_pRemoteVehicle;
    bool usable = rc && rc->GetModelIndex() == MI_RCGOBLIN && rc->GetStatus() != STATUS_WRECKED &&
                  rc->m_fHealth > 0 && FindPlayerPed();
    if (g_heli) {
        if (!usable || rc != g_heli) return unbind("the helicopter is gone", false);
        if (TheCamera.m_WideScreenOn) {
            g_camera_taken = true;  // the bombs are in: the demolition's cinematic
            return unbind("the script took the camera", true);
        }
        if (!drone::rc_active()) {
            g_heli = nullptr;
            effects::set_prop_strikes(true);
            world::ignore(nullptr);
            return;
        }
        follow_drone();
        return;
    }
    if (usable && g_state == State::Running && !g_camera_taken && !TheCamera.m_WideScreenOn && controls_free() &&
        !CTimer::GetIsPaused())
        bind(rc);
}

// --- the texts ------------------------------------------------------------------

// "RC Helicopter" (any case) becomes "FPV Drone", always with a capital D
// ("RC HELICOPTER" -> "FPV DRONE").
// In place: the string gets shorter and the rest of its room is NULs.
void reword_texts(wchar *chars, int count) {
    static const char kFrom[] = "rc helicopter", kTo[] = "fpv drone";
    constexpr int kFromLen = sizeof kFrom - 1, kToLen = sizeof kTo - 1;
    auto lower = [](wchar c) { return c >= 'A' && c <= 'Z' ? wchar(c + 32) : c; };
    auto upper = [](wchar c) { return c >= 'a' && c <= 'z'; };
    int changed = 0;
    for (int start = 0; start < count;) {
        int end = start;
        while (end < count && chars[end]) ++end;
        int out = start;
        for (int i = start; i < end;) {
            bool match = i + kFromLen <= end;
            for (int j = 0; match && j < kFromLen; ++j) match = lower(chars[i + j]) == kFrom[j];
            if (!match) {
                chars[out++] = chars[i++];
                continue;
            }
            bool all_upper = !upper(chars[i + 4]);  // the "E" of "HELICOPTER"
            for (int j = 0; j < kToLen; ++j) {
                bool up = j < 3 || j == 4 || all_upper;
                chars[out++] = up && kTo[j] != ' ' ? wchar(kTo[j] - 32) : wchar(kTo[j]);
            }
            i += kFromLen;
            ++changed;
        }
        while (out < end) chars[out++] = 0;
        start = end + 1;
    }
    if (changed) Log("mission: %d texts say FPV Drone", changed);
}

// Before the game loads its texts (at start, before the first tick).
[[maybe_unused]] const bool g_text_hook = (gFpvTextLoaded = reword_texts, true);

// --- hooks ---------------------------------------------------------------------

int on_launch(int mission) {
    if (mission != kIntro || g_launched) return mission;
    g_launched = true;
    Log("mission: new game: Demolition Man instead of the intro");
    CClock::SetGameClock(13, 0);  // the city by day
    CMessages::AddBigMessage(TheText.Get("TEX_3"), 5000, 1);  // the mission's title, as its contact point shows it
    return kDemolitionMan;
}

bool on_car_fire() {
    if (!g_heli) return false;
    fpv_frame f;
    bool aux3 = drone::frame(&f) && f.channels[6] > 0.3f;  // AETR, AUX1, AUX2, AUX3: the beeper switch
    bool flip = aux3 && !g_aux3_was;  // flipping it drops; holding it on doesn't drop the next bomb at once
    g_aux3_was = aux3;
    return flip || g_test_fire || input::down(GLFW_KEY_LEFT_CONTROL) || input::down(GLFW_KEY_RIGHT_CONTROL) ||
           input::down(GLFW_KEY_SPACE);
}

bool on_vehicle_damage(CVehicle *vehicle) { return vehicle && vehicle == g_heli; }

// The mission's help about the RC helicopter's own controls (throttle on a
// button, tilting, turning, looking behind): a radio flies the drone.
bool on_help(const char *key) {
    static const char *kRcControls[] = {"TEX3_22", "TEX3_23", "TEX3_24", "TEX3_25", "TEX3_26", "TEX3_32",
                                        "TEX3_37", "TEX3_38"};
    for (const char *k : kRcControls)
        if (!std::strncmp(key, k, 8)) return true;
    return false;
}

// --- testing aids (URL parameters; nothing happens without them) ----------------

bool url_has(const char *word) {
    return EM_ASM_INT({ return location.search.indexOf(UTF8ToString($0)) >= 0 ? 1 : 0; }, word) != 0;
}

float script_float(int var) { return *(float *)&CTheScripts::ScriptSpace[var * 4]; }
int script_int(int var) { return *(int32 *)&CTheScripts::ScriptSpace[var * 4]; }

// ?testbomb: carries the four bombs to the four targets by teleporting the
// drone (pickup, drop, plant). ?testfail: blows the helicopter up.
// ?testcut: arms the drone and puts its props into a worker.
void test_sequence() {
    static int mode = -1;
    if (mode < 0) mode = url_has("testbomb") ? 1 : url_has("testfail") ? 2 : url_has("testcut") ? 3 : 0;
    if (!mode) return;
    static uint32_t bound_at = 0;
    static CVehicle *seen = nullptr;
    g_test_fire = false;
    if (!g_heli) return;
    uint32_t now = CTimer::GetTimeInMilliseconds();
    if (seen != g_heli) seen = g_heli, bound_at = now;
    uint32_t t = now - bound_at;
    if (mode == 2) {
        if (t > 3000 && g_heli->GetStatus() != STATUS_WRECKED) {
            Log("mission: test: blowing the helicopter up");
            g_heli->BlowUpCar(nil);
        }
        return;
    }
    if (mode == 3) {
        float ch[10] = {0, 0, -1, 0, 1, 1, -1, -1, -1, -1};  // idle throttle, armed, angle mode
        drone::inject_channels(ch, 10);
        CPed *worker = CPools::GetPedPool()->GetAt(script_int(3311));
        if (t > 3000 && worker && worker->m_fHealth > 0) {
            CVector p = worker->GetPosition();
            drone::rc_place({p.x, p.y, p.z + 0.5f});
        }
        return;
    }
    static uint32_t logged = 0;
    if (now - logged > 1000) {
        logged = now;
        Log("mission: test: carry %d %d %d %d planted %d %d %d %d targets left %d timer %d", script_int(3392),
            script_int(3393), script_int(3394), script_int(3395), script_int(3396), script_int(3397),
            script_int(3398), script_int(3399), script_int(3307), script_int(3402));
    }
    // testbomb: 2 s to settle, then 6 s per bomb
    if (t < 2000) return;
    int bomb = (t - 2000) / 6000, phase = (t - 2000) % 6000;
    if (bomb > 3) return;
    if (phase < 2000) {
        CVector b(script_float(3355 + 3 * bomb), script_float(3356 + 3 * bomb), script_float(3357 + 3 * bomb));
        drone::rc_place({b.x, b.y, b.z + 0.3f});
    } else if (phase < 2400) {
        CVector g(script_float(3367 + 3 * bomb), script_float(3368 + 3 * bomb), script_float(3369 + 3 * bomb));
        drone::rc_place({g.x, g.y, g.z + 3.0f});
        g_test_fire = phase > 2100;
    }
}

// Testing aid: ?testvan in the page's URL puts Tommy in the Top Fun van as
// soon as the mission parks it, which starts the RC part.
void test_van() {
    static int wanted = -1;
    if (wanted < 0) wanted = url_has("testvan") || url_has("testbomb") || url_has("testfail") || url_has("testcut");
    if (!wanted || FindPlayerVehicle() || CWorld::Players[0].m_pRemoteVehicle || TheCamera.m_WideScreenOn) return;
    CVehiclePool *pool = CPools::GetVehiclePool();
    for (int i = pool->GetSize() - 1; i >= 0; --i) {
        CVehicle *v = pool->GetSlot(i);
        if (v && v->GetModelIndex() == MI_TOPFUN && v->GetStatus() != STATUS_WRECKED) {
            Log("mission: test: Tommy into the van");
            FindPlayerPed()->SetObjective(OBJECTIVE_ENTER_CAR_AS_DRIVER, v);
            FindPlayerPed()->WarpPedIntoCar(v);
            wanted = 0;
            return;
        }
    }
}

// --- the attempt ---------------------------------------------------------------------

void start_attempt() {
    ++g_attempt;
    g_seen_on_mission = false;
    g_passed_before = CStats::MissionsPassed;
    g_flight_start = 0;
    g_cuts = g_crashes = 0;
    g_bound_once = false;
    g_camera_taken = false;
    g_state = State::Running;
    js_result("");
}

void show_result() {
    double secs = g_flight_start > 0 ? (now_ms() - g_flight_start) / 1000.0 : 0;
    char html[1024];
    std::snprintf(html, sizeof html,
                  "<h2>Demolition Man: passed</h2><dl>"
                  "<dt>Time on the sticks</dt><dd>%d:%02d</dd>"
                  "<dt>Workers cut down by the props</dt><dd>%d</dd>"
                  "<dt>Crashes</dt><dd>%d</dd>"
                  "<dt>Attempts</dt><dd>%d</dd></dl>"
                  "<p>Vice City is yours: <b>F7</b> launches the quad anywhere, <b>F8</b> tunes it.</p>",
                  int(secs) / 60, int(secs) % 60, g_cuts, g_crashes, g_attempt);
    js_result(html);
}

}  // namespace

void tick() {
    static bool installed = false;
    if (!installed) {
        installed = true;
        gFpvMissionLaunch = on_launch;
        gFpvCarFire = on_car_fire;
        gFpvSkipVehicleDamage = on_vehicle_damage;
        gFpvSkipHelp = on_help;
        FrontEndMenuManager.m_PrefsShowSubtitles = true;  // the cutscene's lines
    }
    if (g_launched && g_state == State::Waiting) start_attempt();

    bool on_mission = CTheScripts::IsPlayerOnAMission();
    drone::set_launch_allowed(!on_mission && g_state != State::Running);
    update_binding();
    if (g_state == State::Running) test_van();
    test_sequence();

    switch (g_state) {
        case State::Running:
            if (on_mission) g_seen_on_mission = true;
            if (CStats::MissionsPassed > g_passed_before) {
                unbind("mission passed", CWorld::Players[0].m_pRemoteVehicle == g_heli);
                g_state = State::Passed;
                Log("mission: passed (attempt %d, %d cut down, %d crashes)", g_attempt, g_cuts, g_crashes);
                show_result();
                js_toast("Mission passed! F7: fly the quad anywhere in Vice City", 10000);
            } else if (g_seen_on_mission && !on_mission) {
                unbind("mission over", CWorld::Players[0].m_pRemoteVehicle == g_heli);
                g_state = State::Failed;
                g_failed_at = CTimer::GetTimeInMilliseconds();
                Log("mission: failed (attempt %d)", g_attempt);
                js_toast("Mission failed. Back to Avery in a moment…", int(kRestartDelayMs) + 2000);
            }
            break;
        case State::Failed: {
            CPlayerInfo &player = CWorld::Players[0];
            bool ready = FindPlayerPed() && player.m_WBState == WBSTATE_PLAYING && !on_mission &&
                         CTimer::GetTimeInMilliseconds() - g_failed_at > kRestartDelayMs && !CTimer::GetIsPaused();
            if (ready) {
                Log("mission: restarting Demolition Man");
                start_attempt();
                CTheScripts::SwitchToMission(kDemolitionMan);
            }
            break;
        }
        default: break;
    }
}

}  // namespace mission
