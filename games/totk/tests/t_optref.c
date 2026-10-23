// Tears of the Kingdom's optional reference with no address (games/totk/optref.c, plan item 151): the game's three instructions (ldr x8, [x19, #0x6b0];
// cbz x8; ldr x21, [x8]) run here at a "main" whose base puts the last at main+0x1bd01c4, under a SIGSEGV / SIGBUS handler that asks the fault hook
// the way crash.c does. The iPhone's 0xffffffffffffffff and MacNativeGames' 0xffff0000ffffffff are read as absent and cleared, a real pointer and 0 go
// through untouched, and the hook leaves a fault elsewhere, a write and an address inside the 39-bit space alone. Built and run by host/tests
// (make run). MacNativeGames' value alone (-DDM_TEST_OPTREF_ONE_VALUE) fails 1: the iPhone's crash.
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include "../optref.c"

__asm__(".text\n.p2align 2\n"
        ".globl _optref_probe\n_optref_probe:\n"   // uint64_t optref_probe(void *obj): what the field points at, 0 when it is absent
        "  stp x19, x21, [sp, #-16]!\n"
        "  mov x19, x0\n"
        "  ldr x8, [x19, #0x6b0]\n"
        "  cbz x8, 1f\n"
        ".globl _optref_use\n_optref_use:\n"
        "  ldr x21, [x8]\n"
        "  mov x0, x21\n"
        "  b 2f\n"
        "1: mov x0, #0\n"
        "2: ldp x19, x21, [sp], #16\n"
        "  ret\n");
uint64_t optref_probe(void *obj); extern char optref_use[];

static module_t g_main = {.name = "main"};
module_t *module_of(const void *p) { return (const uint8_t *)p >= g_main.base && (const uint8_t *)p < g_main.base + g_main.size ? &g_main : NULL; }
static int (*g_hook)(void *, int, void *);
void dm_fault_hook_register(int (*fn)(void *addr, int is_write, void *uc)) { g_hook = fn; }

static sigjmp_buf g_jb; static volatile sig_atomic_t g_guard;
static void on_fault(int sig, siginfo_t *si, void *uc) {
    (void)sig;
    if (g_hook && g_hook(si->si_addr, (int)((((ucontext_t *)uc)->uc_mcontext->__es.__esr >> 6) & 1), uc)) return;
    if (g_guard) { g_guard = 0; siglongjmp(g_jb, 1); }   // not handled: the game would have crashed
    _exit(2);
}
static int fail, count;
#define EXPECT(name, want, got) do { long long w = (want), g = (got); count++; printf("%-84s expected %-6lld → got %-6lld %s\n", name, w, g, w == g ? "✓" : "✗"); fail += w != g; } while (0)
static uint64_t obj[0x6b8 / 8];   // the object: its field at +0x6b0
// Runs the game's instructions on a field holding v: 1 when the result is `want`, the field ends as `field` and the hook fixed `fixes` times; 0 on a crash
static int run(uint64_t v, uint64_t want, uint64_t field, uint64_t fixes) {
    obj[0x6b0 / 8] = v; uint64_t f0 = totk_optref_fixes(), got;
    g_guard = 1; if (sigsetjmp(g_jb, 1)) return 0;
    got = optref_probe(obj); g_guard = 0;
    return got == want && obj[0x6b0 / 8] == field && totk_optref_fixes() - f0 == fixes;
}
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    g_main.base = (uint8_t *)optref_use - OPTREF_USE; g_main.size = OPTREF_USE + 0x100;
    struct sigaction sa = {0}; sa.sa_sigaction = on_fault; sa.sa_flags = SA_SIGINFO | SA_NODEFER; sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL); sigaction(SIGBUS, &sa, NULL);
    EXPECT("the hook registers itself (a constructor)", 1, g_hook == totk_optref_fault);
    static uint64_t cell = 0x1234;
    EXPECT("a pointer: read through, the field kept, no fix", 1, run((uint64_t)&cell, 0x1234, (uint64_t)&cell, 0));
    EXPECT("0: absent by the game's own cbz, no fix", 1, run(0, 0, 0, 0));
    EXPECT("0xffff0000ffffffff (MacNativeGames'): read as absent, the field cleared, one fix", 1, run(0xffff0000ffffffffull, 0, 0, 1));
    EXPECT("0xffffffffffffffff (the iPhone's) [-DDM_TEST_OPTREF_ONE_VALUE: a crash]", 1, run(~0ull, 0, 0, 1));
    // The hook called directly: what it must leave alone (no state changes either)
    _STRUCT_MCONTEXT64 mc = {0}; ucontext_t uc = {0}; uc.uc_mcontext = &mc;
    obj[0x6b0 / 8] = 0x400000000ull; mc.__ss.__x[19] = (uint64_t)obj; mc.__ss.__x[8] = 0x400000000ull; mc.__ss.__pc = (uint64_t)optref_use;
    EXPECT("an address inside the 39-bit space (0x400000000, unmapped): left alone", 0, totk_optref_fault((void *)0x400000000ull, 0, &uc));
    mc.__ss.__x[8] = ~0ull; obj[0x6b0 / 8] = ~0ull;
    EXPECT("... a write fault at the site: left alone", 0, totk_optref_fault((void *)~0ull, 1, &uc));
    mc.__ss.__pc = (uint64_t)optref_use + 4;
    EXPECT("... a fault at another pc of main: left alone", 0, totk_optref_fault((void *)~0ull, 0, &uc));
    g_main.base -= 4; mc.__ss.__pc = (uint64_t)optref_use - 4;   // main+0x1bd01c4 is now the cbz: the instructions do not match
    EXPECT("... main+0x1bd01c4 with other instructions there: left alone", 0, totk_optref_fault((void *)~0ull, 0, &uc));
    EXPECT("... and the field kept", 1, obj[0x6b0 / 8] == ~0ull);
    printf("== %d checks, %d failed ==\n", count, fail);
    return fail != 0;
}
