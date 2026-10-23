// Conditional rendering without a GPU (gfx/nvn/core/cond.c, plan item 264), the completed fence faked. SetRenderEnable: off and on; conditional on
// two counters, rendering when they are equal (mode 2) or not (mode 3); while a samples-passed result is still on the GPU on either counter it stays
// enabled and is counted, once complete the values decide; without counters it stays enabled. The samples-passed destinations: a later report to
// the same address takes its fence; a thousand addresses all kept through the table's growth.
// A result still on the GPU taken as complete (-DDM_TEST_COND_PENDING_DONE, the broken variant): 4 fail.
#include "../nvn/core/cond.h"
#include <stdio.h>
#include <string.h>

static uint64_t g_done;
uint64_t nvn_be_done_fence(void) { return g_done; }
static int n, fail;
static void check(const char *name, long long want, long long got) { n++; int ok = want == got; fail += !ok; printf("%-104s %lld (want %lld) %s\n", name, got, want, ok ? "✓" : "✗"); }
static uint64_t ctr[2][4] __attribute__((aligned(16)));   // two counter pairs: values at +0 and +16
static int off_after(int mode, int pair) { p_render_enable r = {mode, pair < 0 ? 0 : (uint64_t)(uintptr_t)ctr[pair]}; cond_render_enable(&r); return g_render_off; }

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    check("SetRenderEnable(false) turns rendering off, (true) on", 1, off_after(0, -1) == 1 && off_after(1, -1) == 0);
    ctr[0][0] = 7; ctr[0][2] = 7; ctr[1][0] = 7; ctr[1][2] = 9;
    check("conditional, render if equal: equal values render, unequal do not", 1, off_after(2, 0) == 0 && off_after(2, 1) == 1);
    check("conditional, render if not equal: the reverse", 1, off_after(3, 0) == 1 && off_after(3, 1) == 0);
    uint64_t a0 = (uint64_t)(uintptr_t)ctr[1], a1 = a0 + 16;
    occ_note(a1, 5); g_done = 4; uint64_t p0 = g_rc_pending;
    check("a samples-passed result still on the GPU (the second counter): rendering stays enabled [-DDM_TEST_COND_PENDING_DONE]", 0, off_after(2, 1));
    check("... counted as such", 1, g_rc_pending == p0 + 1 && g_rc_on_pending == 1);
    g_done = 5; check("once complete the values decide (unequal: off), and it no longer counts as pending", 1, off_after(2, 1) == 1 && g_rc_on_pending == 0);
    occ_note(a0, 8); check("a later report to the first counter: pending again", 0, off_after(2, 1));
    check("no counters address: rendering stays enabled", 0, off_after(3, -1));
    static uint64_t many[1000][2];
    for (int i = 0; i < 1000; i++) occ_note((uint64_t)(uintptr_t)many[i], 100 + (uint64_t)i);
    g_done = 600; int pend = 0; for (int i = 0; i < 1000; i++) pend += occ_pending((uint64_t)(uintptr_t)many[i]);
    check("a thousand destinations kept through the table's growth: those after fence 600 still pending", 499, pend);
    check("... an address never reported is not", 0, occ_pending((uint64_t)(uintptr_t)&pend));
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
