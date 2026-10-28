#include "hook.h"

#include <libkern/OSCacheControl.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <pthread.h>
#include <sys/mman.h>

#include <cstdint>
#include <cstring>

#include "log.h"

namespace hook {
namespace {

constexpr int kPatched = 4;           // instructions replaced at the target
constexpr uint32_t kLdrX16 = 0x58000050;  // ldr x16, #8
constexpr uint32_t kBrX16 = 0xd61f0200;   // br x16
constexpr size_t kPage = 0x4000;

// Trampolines: one MAP_JIT block, handed out in order.
uint8_t *g_jit = nullptr;
size_t g_jit_used = 0;
constexpr size_t kJitSize = 0x4000;

// ldr xN, #8 ; b #12 ; .quad value  (puts an absolute value in xN)
int emit_load(uint32_t *out, int reg, uint64_t value) {
    out[0] = 0x58000040u | uint32_t(reg);
    out[1] = 0x14000003u;
    std::memcpy(&out[2], &value, 8);
    return 4;
}

// Copies one instruction to the trampoline, rewriting the PC-relative ones it
// knows (adr, adrp). Returns the number of words written, 0 if it can't.
int relocate(uint32_t insn, uint64_t pc, uint32_t *out) {
    if ((insn & 0x1f000000u) == 0x10000000u) {  // adr / adrp
        int reg = int(insn & 0x1f);
        int64_t imm = int64_t(((insn >> 5) & 0x7ffff) << 2 | ((insn >> 29) & 3));
        imm = (imm << 43) >> 43;  // sign-extend 21 bits
        bool page = insn & 0x80000000u;
        uint64_t value = page ? (pc & ~uint64_t(0xfff)) + uint64_t(imm << 12) : pc + uint64_t(imm);
        return emit_load(out, reg, value);
    }
    bool pc_relative = (insn & 0x7c000000u) == 0x14000000u ||  // b, bl
                       (insn & 0xff000010u) == 0x54000000u ||  // b.cond
                       (insn & 0x7e000000u) == 0x34000000u ||  // cbz, cbnz
                       (insn & 0x7e000000u) == 0x36000000u ||  // tbz, tbnz
                       (insn & 0x3b000000u) == 0x18000000u;    // ldr (literal)
    if (pc_relative) return 0;
    out[0] = insn;
    return 1;
}

bool write_code(void *at, const void *code, size_t size) {
    mach_vm_address_t page = mach_vm_address_t(at) & ~mach_vm_address_t(kPage - 1);
    mach_vm_size_t len = (mach_vm_address_t(at) + size - page + kPage - 1) & ~mach_vm_size_t(kPage - 1);
    if (mach_vm_protect(mach_task_self(), page, len, FALSE, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS)
        return false;
    std::memcpy(at, code, size);
    kern_return_t kr = mach_vm_protect(mach_task_self(), page, len, FALSE, VM_PROT_READ | VM_PROT_EXECUTE);
    sys_icache_invalidate(at, size);
    return kr == KERN_SUCCESS;
}

}  // namespace

void *detour(void *target, void *replacement, const char *name) {
    if (!target) {
        Log("hook %s: no target", name);
        return nullptr;
    }
    if (!g_jit) {
        void *mem = mmap(nullptr, kJitSize, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
        if (mem == MAP_FAILED) {
            Log("hook %s: no JIT memory for trampolines", name);
            return nullptr;
        }
        g_jit = static_cast<uint8_t *>(mem);
    }

    uint32_t tramp[kPatched * 4 + 4];
    int n = 0;
    const uint32_t *code = static_cast<const uint32_t *>(target);
    for (int i = 0; i < kPatched; ++i) {
        int w = relocate(code[i], uint64_t(&code[i]), &tramp[n]);
        if (!w) {
            Log("hook %s: instruction %d (%08x) can't be relocated, not hooked", name, i, code[i]);
            return nullptr;
        }
        n += w;
    }
    tramp[n++] = kLdrX16;
    tramp[n++] = kBrX16;
    uint64_t back = uint64_t(target) + kPatched * 4;
    std::memcpy(&tramp[n], &back, 8);
    n += 2;

    size_t size = size_t(n) * 4;
    if (g_jit_used + size > kJitSize) {
        Log("hook %s: out of trampoline space", name);
        return nullptr;
    }
    uint8_t *slot = g_jit + g_jit_used;
    pthread_jit_write_protect_np(0);
    std::memcpy(slot, tramp, size);
    pthread_jit_write_protect_np(1);
    sys_icache_invalidate(slot, size);
    g_jit_used += (size + 15) & ~size_t(15);

    uint32_t jump[4] = {kLdrX16, kBrX16};
    uint64_t to = uint64_t(replacement);
    std::memcpy(&jump[2], &to, 8);
    if (!write_code(target, jump, sizeof jump)) {
        Log("hook %s: can't write to the game's code", name);
        return nullptr;
    }
    return slot;
}

}  // namespace hook
