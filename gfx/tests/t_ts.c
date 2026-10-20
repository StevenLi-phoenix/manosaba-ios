// Counter timestamps inside their submission (gfx/nvn/nvn_ts.h, plan item 131): a report takes the point of its part's GPU start to end that
// matches the work recorded before it; a report between two parts is the earlier part's end, a TIMESTAMP_TOP the later part's start; a part
// whose feedback is missing falls back to the last part; feedback times move onto the guest clock. Every report at the end (-DDM_TEST_TS_AT_END,
// as before): 7 fail.
#include "../nvn/nvn_ts.h"
#include <stdio.h>
static int n, fail;
static void eq(const char *what, uint64_t want, uint64_t got) { n++; int ok = want == got; fail += !ok; printf("%-80s expected %10llu got %10llu %s\n", what, (unsigned long long)want, (unsigned long long)got, ok ? "✓" : "✗"); }
int main(void) {
    eq("ts_at: no work before it: the GPU start", 1000, ts_at(1000, 2000, 0, 10));
    eq("ts_at: all the work before it: the GPU end", 2000, ts_at(1000, 2000, 10, 10));
    eq("ts_at: half the work: half way", 1500, ts_at(1000, 2000, 5, 10));
    eq("ts_at: a submission without work: the end", 2000, ts_at(1000, 2000, 0, 0));
    eq("ts_at: start unknown: the end", 2000, ts_at(0, 2000, 3, 10));
    eq("ts_at: end before start: the end", 900, ts_at(1000, 900, 3, 10));
    eq("ts_guest_ns: no feedback (0 s): unknown", 0, ts_guest_ns(0, 10000000000ull, 100000000000ull));
    eq("ts_guest_ns: a time after now: unknown", 0, ts_guest_ns(11.0, 10000000000ull, 100000000000ull));
    eq("ts_guest_ns: 0.5 s ago on the host = 0.5 s ago on the guest clock", 99500000000ull, ts_guest_ns(9.5, 10000000000ull, 100000000000ull));
    uint32_t w1[] = {0, 10}; uint64_t s1[] = {1000}, e1[] = {2000};
    eq("ts_place: one part, 3 of 10", 1300, ts_place(w1, s1, e1, 1, 3, 0));
    uint32_t w[] = {0, 4, 10}; uint64_t s[] = {1000, 5000}, e[] = {2000, 8000};
    eq("ts_place: two parts, 2 of the first part's 4", 1500, ts_place(w, s, e, 2, 2, 0));
    eq("ts_place: between the parts: the first part's end", 2000, ts_place(w, s, e, 2, 4, 0));
    eq("ts_place: between the parts, TIMESTAMP_TOP: the second part's start", 5000, ts_place(w, s, e, 2, 4, 1));
    eq("ts_place: 3 of the second part's 6", 6500, ts_place(w, s, e, 2, 7, 0));
    eq("ts_place: the last report: the second part's end", 8000, ts_place(w, s, e, 2, 10, 0));
    uint64_t e0[] = {0, 8000};
    eq("ts_place: the first part's feedback missing: the last part's start", 5000, ts_place(w, s, e0, 2, 2, 0));
    uint64_t z[] = {0, 0};
    eq("ts_place: no feedback at all: unknown", 0, ts_place(w, z, z, 2, 2, 0));
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
