// fpvapi.h for the web build: libfpv is linked into the module (docs/WEB.md),
// so the table holds its functions directly, and its settings live in the
// browser's IndexedDB folder.
#include "fpvapi.h"

#include "log.h"

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

bool kamikaze() { return false; }

bool load() {
    if (g_loaded) return true;
    if (g_failed) return false;
    g_failed = true;
#define FPV_FN(name) fpv.name = &::name;
    FPV_FUNCTIONS(FPV_FN)
#undef FPV_FN
    fpv_global_config cfg;
    fpv_global_config_defaults(&cfg);
    cfg.data_dir = FPV_WEB_DATA_DIR;
    // Vice City: metres, X east, Y north, Z up.
    cfg.coords = {FPV_AXIS_POS_X, FPV_AXIS_POS_Z, FPV_AXIS_POS_Y, 1.0f};
    cfg.log = &on_fpv_log;
    cfg.log_level = FPV_LOG_INFO;
    fpv_result r = fpv_global_init(&cfg);
    if (r != FPV_OK && r != FPV_ERROR_ALREADY_INITIALIZED) {
        Log("fpv_global_init: %s (%s)", fpv_result_string(r), fpv_last_error());
        return false;
    }
    g_failed = false;
    g_loaded = true;
    Log("libfpv %s initialised, data in %s", fpv_version_string(), FPV_WEB_DATA_DIR);
    return true;
}

}  // namespace fpvapi
