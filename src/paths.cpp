#include "paths.h"

#include <dlfcn.h>
#include <limits.h>
#include <stdlib.h>

namespace paths {

std::string frameworks_dir() {
    static std::string dir = [] {
        Dl_info info{};
        if (!dladdr(reinterpret_cast<const void *>(&frameworks_dir), &info) || !info.dli_fname) return std::string(".");
        char real[PATH_MAX];
        std::string path = realpath(info.dli_fname, real) ? real : info.dli_fname;
        size_t slash = path.rfind('/');
        return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
    }();
    return dir;
}

std::string resources_dir() { return frameworks_dir() + "/../Resources"; }

std::string data_dir() { return resources_dir() + "/fpv-data"; }

}  // namespace paths
