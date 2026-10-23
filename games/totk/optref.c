// Tears of the Kingdom 1.4.3: an optional reference that holds no address (plan item 151, after MacNativeGames dd0e893, ly05010419/MacNativeGames,
// MIT; written anew). main+0x1bd01bc loads a field of an object (ldr x8, [x19, #0x6b0]), skips the block when it is 0 (cbz x8) and otherwise reads
// through it at main+0x1bd01c4 (ldr x21, [x8]). On the iPhone (2026-10-23, 7 minutes into play, thread BaseProcExecutor0) the field held
// 0xffffffffffffffff, on MacNativeGames' Mac 0xffff0000ffffffff while a save loaded: neither is an address of the game's 39-bit address space, and the
// read through it ended the game. The fault hook treats such a value as absent, as the game's own cbz treats 0: it clears the field (compare-and-swap
// from that value, so a pointer the game wrote meanwhile stays) and resumes at the load, which then reads 0 or the new pointer. Only at that pc with
// the two loads as the product has them, so another build or a rewritten site is left alone. Each fix writes one line to the log.
#include <stdatomic.h>
#include <string.h>
#include <sys/ucontext.h>
#include <unistd.h>
#include "dm.h"

#define OPTREF_USE 0x1bd01c4u   // ldr x21, [x8]: the read that faults; the field's load is two instructions before it
static const uint32_t k_use = 0xf9400115u, k_load = 0xf9435a68u;   // ldr x21, [x8] / ldr x8, [x19, #0x6b0] (main.patched.bin of both products)
static _Atomic uint64_t g_optref_fixes;
uint64_t totk_optref_fixes(void) { return atomic_load(&g_optref_fixes); }

static int optref_no_address(uint64_t v) {
#ifdef DM_TEST_OPTREF_ONE_VALUE   // negative control of t_optref only: MacNativeGames' value alone
    return v == 0xffff0000ffffffffull;
#else
    return (v >> 39) != 0;   // above the game's 39-bit address space: no object of the game is there
#endif
}
static void optref_note(uint64_t v) {   // signal context: write(2) only
    static const char a[] = "[dm] [totk] main+0x1bd01c4: an optional reference held 0x", b[] = ", no address: read as absent (plan item 151)\n";
    char l[sizeof a + sizeof b + 16]; size_t n = sizeof a - 1; memcpy(l, a, n);
    for (int i = 15; i >= 0; i--) l[n++] = "0123456789abcdef"[(v >> (i * 4)) & 15];
    memcpy(l + n, b, sizeof b - 1); n += sizeof b - 1;
    (void)!write(2, l, n);
}
int totk_optref_fault(void *addr, int is_write, void *uc_) {   // crash.c's fault hook: 1 = handled
    (void)addr;
    if (is_write) return 0;
    ucontext_t *uc = uc_; uint64_t pc = uc->uc_mcontext->__ss.__pc;
    module_t *m = module_of((const void *)pc);
    if (!m || strcmp(m->name, "main") || pc - (uint64_t)m->base != OPTREF_USE) return 0;
    const uint32_t *w = (const uint32_t *)pc;
    uint64_t v = uc->uc_mcontext->__ss.__x[8];
    if (w[0] != k_use || w[-2] != k_load || !optref_no_address(v)) return 0;
    uint64_t want = v; atomic_compare_exchange_strong((_Atomic uint64_t *)(uc->uc_mcontext->__ss.__x[19] + 0x6b0), &want, 0);
    uc->uc_mcontext->__ss.__pc = pc - 8;   // the field's load again: 0 now, or what the game wrote meanwhile
    atomic_fetch_add(&g_optref_fixes, 1); optref_note(v);
    return 1;
}
__attribute__((constructor)) static void optref_install(void) { dm_fault_hook_register(totk_optref_fault); }
