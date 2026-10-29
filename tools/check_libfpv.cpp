// CI check without the game: loads a libfpv.dylib the way src/fpvapi.cpp
// does and fails if any function the plugin needs is missing or the ABI
// differs from the headers the plugin was built with.
//   check_libfpv path/to/libfpv.dylib
#include <dlfcn.h>

#include <cstdio>

#include "fpvapi.h"

int main(int argc, char **argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s libfpv.dylib\n", argv[0]);
        return 2;
    }
    void *lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        std::fprintf(stderr, "cannot load %s: %s\n", argv[1], dlerror());
        return 1;
    }
    int missing = 0, found = 0;
#define FPV_FN(name)                                         \
    if (dlsym(lib, #name)) {                                 \
        ++found;                                             \
    } else {                                                 \
        std::fprintf(stderr, "missing export: %s\n", #name); \
        ++missing;                                           \
    }
    FPV_FUNCTIONS(FPV_FN)
#undef FPV_FN
    if (missing) return 1;

    auto abi = reinterpret_cast<decltype(&::fpv_abi_version)>(dlsym(lib, "fpv_abi_version"));
    auto version = reinterpret_cast<decltype(&::fpv_version_string)>(dlsym(lib, "fpv_version_string"));
    if (abi() != LIBFPV_ABI_VERSION) {
        std::fprintf(stderr, "ABI %u, the plugin was built for %u\n", abi(), LIBFPV_ABI_VERSION);
        return 1;
    }
    std::printf("libfpv %s, ABI %u: all %d functions present\n", version(), abi(), found);
    return 0;
}
