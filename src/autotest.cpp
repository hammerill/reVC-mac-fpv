// The autotest launches the drone, arms it in angle mode through injected
// channels, climbs, flies forward, and saves a screenshot from each camera
// (fpv-data/shots/), logging the drone's state as it goes. Then it lands the
// session (F7) and leaves the game as it was. A file reading "at x y z"
// first moves Tommy there and gives the city 3 s to stream in.
#include "autotest.h"

#include <GLFW/glfw3.h>
#include <dlfcn.h>
#include <sys/stat.h>

#include <chrono>
#include <cstdio>
#include <string>

#include "drone.h"
#include "fpvapi.h"
#include "log.h"
#include "paths.h"
#include "vc.h"

namespace autotest {
namespace {

using Clock = std::chrono::steady_clock;

bool g_running = false;
bool g_waiting = false;  // after an "at" teleport, before the launch
Clock::time_point g_start;
int g_step = 0;

void shot() {
    std::string path = paths::data_dir() + "/shot.txt";
    if (FILE *f = std::fopen(path.c_str(), "w")) std::fclose(f);
}

void channels(float roll, float pitch, float throttle, float yaw, bool arm) {
    float c[FPV_CHANNEL_COUNT] = {};
    c[0] = roll, c[1] = pitch, c[2] = throttle, c[3] = yaw;
    for (int i = 4; i < FPV_CHANNEL_COUNT; ++i) c[i] = -1;
    c[4] = arm ? 1 : -1;  // AUX1 arm
    c[5] = 1;             // AUX2 angle mode
    drone::inject_channels(c, FPV_CHANNEL_COUNT);
}

}  // namespace

void tick() {
    if (!g_running && !g_waiting) {
        static int counter = 0;
        if (++counter % 30) return;
        std::string trigger = paths::data_dir() + "/autotest.txt";
        struct stat st;
        if (stat(trigger.c_str(), &st) != 0 || !vc::api.FindPlayerPed() || vc::paused()) return;
        vc::CVector at{};
        bool teleport = false;
        if (FILE *f = std::fopen(trigger.c_str(), "r")) {
            teleport = std::fscanf(f, " at %f %f %f", &at.x, &at.y, &at.z) == 3;
            std::fclose(f);
        }
        std::remove(trigger.c_str());
        if (drone::active()) drone::toggle();
        if (teleport) {
            Log("autotest: Tommy to %.2f %.2f %.2f, outdoors", at.x, at.y, at.z);
            // As the script command SET_AREA_VISIBLE 0: leave any interior.
            static auto *area = static_cast<int *>(dlsym(RTLD_DEFAULT, "_ZN5CGame8currAreaE"));
            static auto *remove_buildings = reinterpret_cast<void (*)(int)>(
                dlsym(RTLD_DEFAULT, "_ZN10CStreaming24RemoveBuildingsNotInAreaEi"));
            if (area && remove_buildings) *area = 0, remove_buildings(0);
            vc::api.LoadScene(at);
            vc::teleport(vc::api.FindPlayerPed(), at);
            g_waiting = true;
            g_start = Clock::now();
            return;
        }
        g_waiting = true;
        g_start = Clock::now() - std::chrono::seconds(3);
    }
    if (g_waiting) {
        if (Clock::now() - g_start < std::chrono::seconds(3)) return;
        g_waiting = false;
        Log("autotest: start");
        drone::toggle();
        if (!drone::active()) {
            Log("autotest: the drone didn't launch");
            return;
        }
        g_running = true;
        g_start = Clock::now();
        g_step = 0;
        return;
    }
    if (!drone::active()) {
        Log("autotest: the flight ended early");
        g_running = false;
        return;
    }
    float t = std::chrono::duration<float>(Clock::now() - g_start).count();
    if (t < 1.5f) channels(0, 0, -1, 0, false);
    else if (t < 3.0f) channels(0, 0, -1, 0, true);
    else if (t < 6.0f) channels(0, 0, -0.35f, 0, true);  // climb
    else if (t < 10.0f) channels(0, 0.25f, -0.5f, 0, true);  // forward
    else channels(0, 0, -0.56f, 0, true);  // hover
    struct Event { float at; const char *what; };
    static const Event events[] = {{2.5f, "shot"}, {8.0f, "shot"}, {10.5f, "camera"}, {11.5f, "shot"},
                                   {12.5f, "camera"}, {13.5f, "shot"}, {14.5f, "camera"}, {15.5f, "end"}};
    while (g_step < int(sizeof events / sizeof events[0]) && t >= events[g_step].at) {
        std::string what = events[g_step++].what;
        vc::CVector p{};
        drone::drone_position(&p);
        Log("autotest: %.1f s %s, drone at %.2f %.2f %.2f", t, what.c_str(), p.x, p.y, p.z);
        if (what == "shot") shot();
        else if (what == "camera") drone::cycle_view();
        else if (what == "end") {
            drone::toggle();
            g_running = false;
            Log("autotest: done");
        }
    }
}

}  // namespace autotest
