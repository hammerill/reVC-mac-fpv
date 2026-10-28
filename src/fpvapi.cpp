#include "fpvapi.h"

#include <dlfcn.h>

#include <string>

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

}  // namespace

bool loaded() { return g_loaded; }

bool kamikaze() {
#if defined(LIBFPV_KAMIKAZE)
    return g_loaded && fpv.fpv_kamikaze_poll_detonation && fpv.fpv_kamikaze_get_state && fpv.fpv_kamikaze_get_config;
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
#define FPV_FN(name) fpv.name = reinterpret_cast<decltype(fpv.name)>(dlsym(lib, #name));
    FPV_KAMIKAZE_FUNCTIONS(FPV_FN)
#undef FPV_FN

    std::string data_dir = paths::data_dir();
    fpv_global_config cfg;
    fpv.fpv_global_config_defaults(&cfg);
    cfg.data_dir = data_dir.c_str();
    // Vice City: metres, X east, Y north, Z up.
    cfg.coords = {FPV_AXIS_POS_X, FPV_AXIS_POS_Z, FPV_AXIS_POS_Y, 1.0f};
    cfg.log = &on_fpv_log;
    cfg.log_level = FPV_LOG_INFO;
    fpv_result r = fpv.fpv_global_init(&cfg);
    if (r != FPV_OK && r != FPV_ERROR_ALREADY_INITIALIZED) {
        Log("fpv_global_init: %s (%s)", fpv.fpv_result_string(r), fpv.fpv_last_error());
        return false;
    }
    g_failed = false;
    g_loaded = true;
    Log("libfpv %s initialised%s, data in %s", fpv.fpv_version_string(), kamikaze() ? " (warhead)" : "",
        data_dir.c_str());
    return true;
}

}  // namespace fpvapi
