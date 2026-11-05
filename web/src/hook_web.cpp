// hook.h for the web build: reVC is compiled with FPV_HOOKS, so a detour is
// a function pointer the hooked function checks at its top
// (build/re3/src/extras/fpvhooks.h). The original is the target itself: the
// hook's guard runs the function's own body when the replacement calls it.
#include "hook.h"

#include <cstring>

#include <emscripten.h>

#include "common.h"
#include "fpvhooks.h"
#include "log.h"

namespace hook {

void *detour(void *target, void *replacement, const char *name) {
    // Debugging aid: ?nohooks in the page's URL runs the game without the mod's hooks.
    if (std::strcmp(name, "CGame_Process") && EM_ASM_INT({ return location.search.indexOf('nohooks') >= 0 ? 1 : 0; })) return target;
    for (int i = 0; i < FPVH_COUNT; ++i)
        if (!std::strcmp(gFpvHookNames[i], name)) {
            gFpvHooks[i] = replacement;
            return target;
        }
    Log("hook: reVC has no hook point named %s", name);
    return nullptr;
}

}  // namespace hook
