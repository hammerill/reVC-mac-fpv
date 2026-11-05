// paths.h for the web build: everything in the page's memory file system.
// libfpv's own settings are elsewhere (FPV_WEB_DATA_DIR, kept in IndexedDB).
#include "paths.h"

#include <sys/stat.h>

namespace paths {

std::string frameworks_dir() { return resources_dir(); }

std::string resources_dir() {
    static bool made = (mkdir("/fpv", 0755), mkdir("/fpv/data", 0755), true);
    (void)made;
    return "/fpv";
}

std::string data_dir() { return resources_dir() + "/data"; }

}  // namespace paths
