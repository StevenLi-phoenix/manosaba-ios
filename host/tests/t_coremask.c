// The thread core mask (hle_thread.c svc 0x0E / 0x0F / 0x10, plan item 128): totk-mobile's tests/test_kernel.c vectors (Apache-2.0, as data) where
// they agree with Atmosphère's KThread checks, which ours follows: the main thread starts at ideal core 0 with only that core in its mask (totk-mobile
// expects 7, the process's mask; Atmosphère's KThread::Initialize sets 1 << the core);
// "don't care" (-1) keeps no ideal core and the thread runs on the mask's core; masks outside the process's cores are InvalidCoreId, an empty mask
// and an ideal core outside the mask InvalidCombination, other negative values InvalidCoreId, -3 ("no update") with no ideal core
// InvalidCombination, and a refused call changes nothing; -3 keeps ideal core 1 under a new mask; -2 takes the process's ideal core with only that
// core. (totk-mobile's ideal core 3 with mask 7 is InvalidCoreId in its kernel; Atmosphère says InvalidCombination, as ours.) Cores outside the
// process's mask let through (-DDM_TEST_CORE_NO_PROCMASK): 4 fail (the thread then runs on core 3).
#include "../dm.h"
#include <stdio.h>
#include <string.h>
int svc_thread(int num, uint64_t *regs);
uint32_t svc_thread_main_init(void);
void (*dm_test_cv_after_release)(void);   // DM_TEST_HOOKS (t_condvar uses it)
static int n, fail;
#define CHECK(c, ...) do { n++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
static uint64_t r[32];
static uint64_t sv(int num) { svc_thread(num, r); return r[0]; }
int main(void) {
    uint32_t h = svc_thread_main_init();
    memset(r, 0, sizeof r); r[2] = h; sv(0x0E);
    CHECK(r[0] == 0 && r[1] == 0 && r[2] == 1, "GetThreadCoreMask of the main thread: ideal core 0, mask 1 (Atmosphère's KThread::Initialize; got %#llx %#llx %#llx)", r[0], r[1], r[2]);
    int ok = 1;
    for (unsigned cpu = 0; cpu < 3; cpu++) {
        memset(r, 0, sizeof r); r[0] = h; r[1] = (uint32_t)-1; r[2] = 1u << cpu; ok &= sv(0x0F) == 0;
        ok &= sv(0x10) == cpu;
        memset(r, 0, sizeof r); r[2] = h; ok &= sv(0x0E) == 0 && (uint32_t)r[1] == UINT32_MAX && r[2] == (1u << cpu);
    }
    CHECK(ok, "don't care (-1) with mask 1 / 2 / 4: no ideal core, the thread runs on that core");
    const struct { int core; uint64_t mask; uint32_t want; const char *why; } bad[] = {
        {3, 7, 0xe801, "ideal core 3 outside mask 7: InvalidCombination (Atmosphère)"}, {-4, 7, 0xe201, "ideal -4: InvalidCoreId"},
        {-1, 8, 0xe201, "mask 8, outside the process's cores: InvalidCoreId"}, {-1, 0, 0xe801, "an empty mask: InvalidCombination"},
        {0, 2, 0xe801, "ideal core 0 outside mask 2: InvalidCombination"}, {-3, 4, 0xe801, "no update with no ideal core: InvalidCombination"}};
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        memset(r, 0, sizeof r); r[0] = h; r[1] = (uint32_t)bad[i].core; r[2] = bad[i].mask; uint64_t got = sv(0x0F); uint64_t at = sv(0x10);
        CHECK(got == bad[i].want && at == 2, "%s (got %#llx; still on core %llu)", bad[i].why, got, at);
    }
    memset(r, 0, sizeof r); r[0] = h; r[1] = 1; r[2] = 6; uint64_t a = sv(0x0F);
    memset(r, 0, sizeof r); r[0] = h; r[1] = (uint32_t)-3; r[2] = 2; uint64_t b = sv(0x0F); uint64_t at = sv(0x10);
    CHECK(a == 0 && b == 0 && at == 1, "ideal 1 with mask 6, then no update (-3) with mask 2: the thread on core 1");
    memset(r, 0, sizeof r); r[0] = h; r[1] = (uint32_t)-2; r[2] = 8; uint64_t c = sv(0x0F);
    memset(r, 0, sizeof r); r[2] = h; sv(0x0E);
    CHECK(c == 0 && r[1] == 0 && r[2] == 1, "the process's ideal core (-2): ideal 0, mask 1 whatever mask was passed");
    printf("t_coremask: %d checks, %d failed\n", n, fail); return fail != 0;
}
