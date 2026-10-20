// Counter timestamps inside their submission (plan item 131). The replay writes every report of a submission when its command buffer has run, and
// all of them took that one moment: Tears of the Kingdom measures its GPU time as the difference of two TIMESTAMP reports of the frame's submission
// (main 0xa1a50: the report near the start against the last one; its dynamic resolution and frame-rate controller read the result) and read
// 0 to 60 µs where the GPU ran 5 to 7 ms (a probe at main 0xa1b90 over 1,761 frames), so it never lowered its resolution. Now each report takes
// the point of the command buffer's GPU start to end that matches the share of the submission's work (draws, dispatches, copies, clears) recorded
// before it: the last report stays at the end, which the frame-rate controller compares with the frame's start tick. On hardware a report is
// written when the GPU reaches it; Breath of the Wild in nxnative (seedds, MIT) splits submissions instead (its "segment" timestamps).
// A submission committed in parts (mid-submission commits, nvn_mtl.m chunk_maybe_split: Tears of the Kingdom's frames on the Mac) gives each part's
// own GPU start and end, so a report takes its place inside the part that holds it, as nxnative's segments do.
// Unit test gfx/tests/t_ts.c; in t_nvn a report before 24 busy copies and one after them must differ.
#pragma once
#include <stdint.h>

// The time of a report pos work items into a submission of total, between the GPU start and end (ns, one clock); the end when unknown
static inline uint64_t ts_at(uint64_t start_ns, uint64_t end_ns, uint32_t pos, uint32_t total) {
#ifdef DM_TEST_TS_AT_END   // negative control of t_ts only: every report at the end, as before
    (void)start_ns; (void)pos; (void)total; return end_ns;
#endif
    if (!start_ns || end_ns < start_ns || !total || pos >= total) return end_ns;
    return start_ns + (uint64_t)((double)(end_ns - start_ns) * pos / total);
}
// A GPU feedback time (seconds on the host time base: MTLCommandBuffer / MTL4CommitFeedback GPUStartTime, GPUEndTime) on the guest clock, given
// both clocks now (host ns from mach_absolute_time); 0 when unknown or not in the past
static inline uint64_t ts_guest_ns(double s, uint64_t host_now_ns, uint64_t guest_now_ns) {
    uint64_t at = (uint64_t)(s * 1e9);
    if (s <= 0 || !guest_now_ns || at > host_now_ns || host_now_ns - at > guest_now_ns) return 0;
    return guest_now_ns - (host_now_ns - at);
}

// pos's time in a submission of parts command buffers: part k holds the work w[k] .. w[k + 1] (w[0] = 0, w[parts] = the total) and ran from s[k]
// to e[k] (0 when its feedback has not arrived: the last part's times stand in). A report between two parts is the earlier part's end, or with top
// (TIMESTAMP_TOP, taken when the GPU reaches it) the later part's start; 0 when nothing is known
static inline uint64_t ts_place(const uint32_t *w, const uint64_t *s, const uint64_t *e, int parts, uint32_t pos, int top) {
    if (parts < 1) return 0;
    int k = 0;
    while (k < parts - 1 && (top ? pos >= w[k + 1] : pos > w[k + 1])) k++;
    uint64_t a = s[k], b = e[k];
    if (!b) { a = s[parts - 1]; b = e[parts - 1]; if (!b) return 0; return ts_at(a, b, pos >= w[parts - 1] ? pos - w[parts - 1] : 0, w[parts] - w[parts - 1]); }
    return ts_at(a, b, pos - w[k], w[k + 1] - w[k]);
}
