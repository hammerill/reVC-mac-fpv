#include "log.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>

#include "paths.h"

namespace {
std::mutex g_mutex;
FILE *g_file = nullptr;
bool g_opened = false;
}  // namespace

void Log(const char *fmt, ...) {
    char line[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof line, fmt, args);
    va_end(args);

    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_opened) {  // a fresh log for every game start
        g_opened = true;
        g_file = std::fopen((paths::resources_dir() + "/reVC-fpv.log").c_str(), "w");
    }
    std::time_t now = std::time(nullptr);
    char stamp[16];
    std::strftime(stamp, sizeof stamp, "%H:%M:%S", std::localtime(&now));
    std::fprintf(stderr, "[reVC-fpv] %s\n", line);
    if (g_file) {
        std::fprintf(g_file, "%s %s\n", stamp, line);
        std::fflush(g_file);
    }
}
