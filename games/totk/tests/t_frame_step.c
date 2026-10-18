// The host half of the real-time frame step (games/totk/frame_step.c): the mean of the present intervals and the page layout. Built and run by
// games/totk/check_frame_rate.py (clang -DFRAME_STEP_TEST -I host).
#include <stdio.h>
#include <math.h>
#define FRAME_STEP_TEST 1
#include "../frame_step.c"

static int n, fail;
static void expect(const char *what, double want, double got, double tol) {
    n++; int ok = fabs(want - got) <= tol; fail += !ok;
    printf("%-88s expected %-12.6f got %-12.6f %s\n", what, want, got, ok ? "✓" : "✗");
}
int main(void) {
    frame_mean m = {0}; uint64_t t = 1000000000ull; double s = 0;
    expect("first present: no interval yet, the step stays", 0, frame_mean_add(&m, t), 0);
    for (int i = 0; i < 10; i++) s = frame_mean_add(&m, t += 11111111);           // 90 fps
    expect("steady 90 fps: 1/90 s", 1.0 / 90, s, 1e-6);
    for (int i = 0; i < 5; i++) s = frame_mean_add(&m, t += 33333333);            // 30 fps: five intervals replace the window
    expect("five 30 fps intervals: 1/30 s", 1.0 / 30, s, 1e-6);
    s = frame_mean_add(&m, t += 16666667);
    expect("one 60 fps interval after them: mean of the last five", (4 / 30.0 + 1 / 60.0) / 5, s, 1e-6);
    s = frame_mean_add(&m, t += 5000000);                                           // 200 fps
    expect("an interval below 1/120 s counts as 1/120", (3 / 30.0 + 1 / 60.0 + 1 / 120.0) / 5, s, 1e-6);
    s = frame_mean_add(&m, t += 200000000);                                         // 5 fps, below 0.25 s
    expect("an interval above 1/15 s counts as 1/15", (2 / 30.0 + 1 / 60.0 + 1 / 120.0 + 1 / 15.0) / 5, s, 1e-6);
    expect("a pause above 0.25 s keeps the step", 0, frame_mean_add(&m, t += 400000000), 0);
    expect("... and restarts the mean", 1.0 / 60, frame_mean_add(&m, t += 16666667), 1e-6);
    _Atomic uint64_t page = 0; frame_step_store(&page, 1.0 / 90);
    uint64_t bits = atomic_load(&page); float v[2]; memcpy(v, &bits, sizeof v);
    expect("page +0: ratio = seconds x 30", 1.0 / 3, v[0], 1e-6);
    expect("page +4: ticks = seconds x 60", 2.0 / 3, v[1], 1e-6);
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
