// Measurements by frame without a GPU (gfx/nvn/core/frame.c, plan item 264). A frame's GPU time is the union of its command buffers' spans:
// [0,10] [5,12] [20,25] ms give 17 where the sum gave 22; -1 while a buffer is still running; its earliest start and latest end; more spans than
// the list holds, all overlapping, still give the exact union; a frame GIVR later takes the slot. DM_NVN_UB: categories parsed with their keys,
// phases alternate between the baseline and each category, a phase's @set switch is set and restored after it, its keys (a prefix, or *) drop
// draws only in that phase; the present intervals are counted by band from frame 45 of a phase. DM_BOOTPROF read; the boot clock runs forward.
// The plain sum (-DDM_TEST_GIV_SUM, the broken variant): 3 fail.
#include "../nvn/core/frame.h"
#include "core_test.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_dm_present_link;   // the host's switch that DM_NVN_UB's table registers

int main(void) {
    uint64_t b0 = bp_now(); check("the boot clock: process start before now", 1, g_bp.t0 && g_bp.t0 <= b0);
    setenv("DM_BOOTPROF", "1", 1); check("DM_BOOTPROF set: on", 1, bootprof());

    uint64_t f = 5; for (int i = 0; i < 3; i++) gpu_tag_begin(f);
    gpu_tag_done(f, 1.000, 1.010); gpu_tag_done(f, 1.005, 1.012);
    check("a buffer still running: -1", -1, (long long)mtl_gpu_ms(f));
    gpu_tag_done(f, 1.020, 1.025);
    check("spans [0,10] [5,12] [20,25] ms: the union, 17 ms", 17000, (long long)(mtl_gpu_ms(f) * 1000 + 0.5));
    check("... the plain sum kept apart, 22 ms", 22000, (long long)(mtl_gpu_sum_ms(f) * 1000 + 0.5));
    uint64_t t0, t1; check("... earliest start and latest end, 25 ms apart", 25000, mtl_gpu_span(f, &t0, &t1) ? (long long)((t1 - t0 + 500) / 1000) : -1);
    f = 6; for (int i = 0; i < 300; i++) gpu_tag_begin(f);
    for (int i = 0; i < 300; i++) gpu_tag_done(f, 2.0 + i * 0.0001, 2.0 + i * 0.0001 + 0.0005);   // 300 overlapping spans, 0.1 ms apart, 0.5 ms long
    check("300 overlapping spans (more than the list holds): the exact union, 30.4 ms", 30400, (long long)(mtl_gpu_ms(f) * 1000 + 0.5));
    gpu_tag_begin(f + GIVR); gpu_tag_done(f + GIVR, 3.0, 3.001);
    check("a frame GIVR later takes the slot: the old frame is gone (-1)", -1, (long long)mtl_gpu_ms(f));

    setenv("DM_NVN_UB", "slow=abcd,@set:g_dm_present_link=7;all=*", 1); setenv("DM_NVN_UB_N", "60", 1);
    g_dm_present_link = 1; ub_frame(0);
    check("DM_NVN_UB: two categories, 60 frames a phase", 260, g_ub.n * 100 + g_ub.N);
    check("phase 0, the baseline: nothing dropped", 0, ub_skip("abcd0123"));
    ub_frame(45); mtl_ub_present(16.0); mtl_ub_present(24.0); mtl_ub_present(40.0); mtl_ub_present(-1);
    check("present intervals by band from frame 45: 16.7 / 25 / 33+ ms, dropped", 1111, atomic_load(&g_ub.p_c[0]) * 1000 + atomic_load(&g_ub.p_c[1]) * 100 + atomic_load(&g_ub.p_c[2]) * 10 + atomic_load(&g_ub.p_c[3]));
    ub_frame(60);
    check("phase 1, category slow: its @set switch set", 7, g_dm_present_link);
    check("... its key drops the draws it prefixes, not others", 1, ub_skip("abcd0123") && !ub_skip("ffff0000"));
    ub_frame(120); check("phase 2, the baseline: the switch restored, nothing dropped", 1, g_dm_present_link == 1 && !ub_skip("abcd0123"));
    ub_frame(180); check("phase 3, category all: every draw dropped", 1, ub_skip("ffff0000") && g_dm_present_link == 1);
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
