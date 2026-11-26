#include "fpvapi.h"

#include <dlfcn.h>

#include <filesystem>
#include <string>

#include "build_info.h"
#include "log.h"
#include "paths.h"

namespace fpvapi {

Api fpv;

namespace {

bool g_loaded = false;
bool g_failed = false;

void on_fpv_log(void *, fpv_log_level level, const char *message) {
    static const char *names[] = {"debug", "info", "warn", "error"};
    Log("libfpv %s: %s", names[level & 3], message);
}

// libfpv's shared folder, so the radio setup and the tune follow the pilot
// between libfpv games. The first time, it takes over what earlier versions
// kept in the bundle (unless another game already created it). Falls back to
// the bundle folder when there is no home folder.
std::string pick_data_dir() {
    namespace fs = std::filesystem;
    char shared[1024];
    fpv_result r = fpv.fpv_shared_data_dir(shared, sizeof shared);
    if (r != FPV_OK) {
        Log("fpv_shared_data_dir: %s, using the bundle folder", fpv.fpv_result_string(r));
        return paths::legacy_data_dir();
    }
    std::error_code ec;
    fs::path old_dir = paths::legacy_data_dir();
    if (!fs::exists(shared, ec) && fs::is_directory(old_dir, ec)) {
        fs::create_directories(shared, ec);
        for (const auto &entry : fs::directory_iterator(old_dir, ec)) {
            if (entry.path().filename() == "shots") continue;  // the mod's screenshots stay
            fs::copy(entry.path(), fs::path(shared) / entry.path().filename(),
                     fs::copy_options::recursive | fs::copy_options::skip_existing, ec);
            if (ec) Log("cannot copy %s: %s", entry.path().c_str(), ec.message().c_str());
        }
        Log("moved settings from %s to %s (the old folder is kept)", old_dir.c_str(), shared);
    }
    return shared;
}

}  // namespace

bool loaded() { return g_loaded; }

fpv_pilot_config pilot_config() {
    fpv_pilot_config pc{};
    pc.struct_size = sizeof pc;
    if (!g_loaded || fpv.fpv_pilot_get_config(&pc) != FPV_OK) {
        pc.features = FPV_PILOT_STAND_IN | FPV_PILOT_POSE;
        pc.stand_in_distance = kStandInDistance;
        pc.stand_in_return_distance = kStandInReturnDistance;
    }
    return pc;
}

bool kamikaze() {
#if defined(LIBFPV_KAMIKAZE)
    return g_loaded && fpv.fpv_kamikaze_poll_detonation && fpv.fpv_kamikaze_get_state && fpv.fpv_kamikaze_get_config &&
           fpv.fpv_kamikaze_detonate;
#else
    return false;
#endif
}

bool load() {
    if (g_loaded) return true;
    if (g_failed) return false;
    g_failed = true;  // until proven otherwise; don't retry every key press

    std::string path = paths::frameworks_dir() + "/libfpv.dylib";
    void *lib = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        Log("cannot load %s: %s", path.c_str(), dlerror());
        return false;
    }
    bool ok = true;
#define FPV_FN(name)                                                     \
    fpv.name = reinterpret_cast<decltype(fpv.name)>(dlsym(lib, #name)); \
    if (!fpv.name) {                                                     \
        Log("libfpv.dylib has no %s", #name);                            \
        ok = false;                                                      \
    }
    FPV_FUNCTIONS(FPV_FN)
#undef FPV_FN
    if (!ok) return false;
    if (fpv.fpv_abi_version() != LIBFPV_ABI_VERSION) {
        Log("libfpv.dylib has ABI %u, this plugin was built for %u", fpv.fpv_abi_version(), LIBFPV_ABI_VERSION);
        return false;
    }
    // libfpv.h lays fpv_frame out differently with LIBFPV_KAMIKAZE (12 radio
    // channels instead of 10), and the ABI version is the same for both: a
    // libfpv of the other kind would fill the frame 8 bytes off from where the
    // plugin reads it (wind, impacts, the video feed). Refuse it instead
    // (libfpv docs/INTEGRATION.md "Build the game for the same kind of libfpv").
    bool lib_kamikaze = dlsym(lib, "fpv_kamikaze_poll_detonation") != nullptr;
#if defined(LIBFPV_KAMIKAZE)
    const bool mod_kamikaze = true;
#else
    const bool mod_kamikaze = false;
#endif
    if (lib_kamikaze != mod_kamikaze) {
        Log("libfpv.dylib is a %s build but this plugin was built for a %s one: not loaded (build it with "
            "-DREVC_FPV_KAMIKAZE=%s)",
            lib_kamikaze ? "kamikaze" : "plain", mod_kamikaze ? "kamikaze" : "plain", lib_kamikaze ? "ON" : "OFF");
        return false;
    }
#define FPV_FN(name) fpv.name = reinterpret_cast<decltype(fpv.name)>(dlsym(lib, #name));
    FPV_KAMIKAZE_FUNCTIONS(FPV_FN)
#undef FPV_FN

    std::string data_dir = pick_data_dir();
    fpv_global_config cfg;
    fpv.fpv_global_config_defaults(&cfg);
    cfg.data_dir = data_dir.c_str();
    // Vice City: metres, X east, Y north, Z up.
    cfg.coords = {FPV_AXIS_POS_X, FPV_AXIS_POS_Z, FPV_AXIS_POS_Y, 1.0f};
    cfg.log = &on_fpv_log;
    cfg.log_level = FPV_LOG_INFO;
    // render.cpp draws the video feed with libfpv's fpv_video_gl3.h (the
    // game's context is OpenGL 3.2+ core): F8 > Camera offers its effects.
    cfg.flags |= FPV_INIT_VIDEO_FEED;
    // F8 > About: the mod's build next to libfpv's, for bug reports.
    cfg.host_name = "GTA Vice City FPV (macOS)";
    cfg.host_build = REVC_FPV_BUILD;
    cfg.host_url = "https://github.com/hammerill/reVC-mac-fpv";
    fpv_result r = fpv.fpv_global_init(&cfg);
    if (r != FPV_OK && r != FPV_ERROR_ALREADY_INITIALIZED) {
        Log("fpv_global_init: %s (%s)", fpv.fpv_result_string(r), fpv.fpv_last_error());
        return false;
    }
    // Tommy: carried along with a far drone (standin.cpp) and posed with a
    // radio (pilot.cpp). Low distances: Vice City's doors are script triggers
    // on the player, and he often launches right outside one.
    fpv.fpv_pilot_set_support(FPV_PILOT_STAND_IN | FPV_PILOT_POSE, kStandInDistance, kStandInReturnDistance);
    // Rough ground and car paint hold a perched quad better than libfpv's smooth default.
    fpv_material rough{0.2f, 0.6f, 0}, paint{0.2f, 0.5f, 0};
    fpv.fpv_material_set(kMaterialRough, &rough);
    fpv.fpv_material_set(kMaterialVehicle, &paint);
    g_failed = false;
    g_loaded = true;
    Log("libfpv %s initialised%s, data in %s", fpv.fpv_version_string(), kamikaze() ? " (warhead)" : "",
        data_dir.c_str());
    return true;
}

}  // namespace fpvapi
