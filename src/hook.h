// Inline detours for arm64 functions inside the game executable.
//
// reVC's own calls between its functions are direct `bl`s, so dyld
// interposing can't reach them. A detour overwrites the first four
// instructions of the target with a jump to the replacement, and returns a
// trampoline (in MAP_JIT memory) that runs those four instructions and jumps
// back. The game is ad-hoc signed without the hardened runtime, so its code
// pages can be made writable (copy-on-write) for the patch.
//
// Install every detour while only the main thread runs (the plugin's
// constructor): a code page is not executable while it is being patched.
#pragma once

namespace hook {

// Returns the trampoline that calls the original, or nullptr (and logs why)
// if the prologue can't be relocated or the page can't be patched.
void *detour(void *target, void *replacement, const char *name);

template <typename Fn>
Fn detour(void *target, Fn replacement, const char *name) {
    return reinterpret_cast<Fn>(detour(target, reinterpret_cast<void *>(replacement), name));
}

}  // namespace hook
