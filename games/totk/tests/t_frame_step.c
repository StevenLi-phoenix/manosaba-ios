// The host half of the real-time frame step (games/totk/frame_step.c): the mean of the present intervals and the page layout; the scene size the
// page carries at +16 (DM_RENDER_RES or the game's own for the operation mode, plan item 52). Built and run by
// games/totk/check_frame_rate.py (clang -DFRAME_STEP_TEST -I host). The heap overlay's note ignored (-DDM_TEST_SCENE_NO_HEAP) fails 1; +8 not
// written (-DDM_TEST_CLOTH_NO_SECONDS) fails 1.
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
    _Atomic uint64_t page[2] = {0}; frame_step_store(page, 1.0 / 90);
    uint64_t bits = atomic_load(&page[0]), b2 = atomic_load(&page[1]); float v[3]; memcpy(v, &bits, 8); memcpy(&v[2], &b2, 4);
    expect("page +0: ratio = seconds x 30", 1.0 / 3, v[0], 1e-6);
    expect("page +4: ticks = seconds x 60", 2.0 / 3, v[1], 1e-6);
    expect("page +8: seconds (the cloth and hair clock, plan item 163)", 1.0 / 90, v[2], 1e-6);
    float sz[2]; int r;
    r = scene_size(NULL, 1, NULL, sz); expect("scene size unset, docked: the game's 1600 ...", 1600, sz[0], 0); expect("... x 900, its own (0)", 900 * 10 + 0, sz[1] * 10 + r, 0);
    r = scene_size("", 0, NULL, sz); expect("unset, handheld: 1280 ...", 1280, sz[0], 0); expect("... x 720, its own (0)", 720 * 10 + 0, sz[1] * 10 + r, 0);
    r = scene_size("0", 1, NULL, sz); expect("DM_RENDER_RES=0: the game's own (0)", 1600 * 10 + 0, sz[0] * 10 + r, 0);
    r = scene_size("960x540", 1, NULL, sz); expect("960x540: width", 960, sz[0], 0); expect("... height, from the variable (1)", 540 * 10 + 1, sz[1] * 10 + r, 0);
    r = scene_size("1920x1080", 0, NULL, sz); expect("1920x1080, the largest: from the variable (1)", 1080 * 10 + 1, sz[1] * 10 + r, 0);
    const char *bad[] = {"960x", "32x32", "5000x100", "960x540x", "abc", "2560x1440", "1921x1080", "1920x1081"};
    for (int i = 0; i < 8; i++) { char what[64]; r = scene_size(bad[i], 0, NULL, sz); snprintf(what, sizeof what, "%s: refused (-1), the game's own 1280", bad[i]);
        expect(what, 1280 * 10 - 1, sz[0] * 10 + r, 0); }
    // The heap overlay's note (games/totk/heapdef.py, plan item 95) raises the largest size to its own, never above 3840x2160
    r = scene_size("2560x1440", 1, "2560x1440", sz); expect("2560x1440 with an overlay for 2560x1440: taken (1)", 1440 * 10 + 1, sz[1] * 10 + r, 0);
    r = scene_size("2560x1440", 1, "1920x1080", sz); expect("2560x1440 with an overlay for 1920x1080: refused (-1)", 1600 * 10 - 1, sz[0] * 10 + r, 0);
    r = scene_size("3200x1800", 1, "2560x1440", sz); expect("3200x1800 above the overlay's 2560x1440: refused (-1)", 1600 * 10 - 1, sz[0] * 10 + r, 0);
    r = scene_size("2560x1440", 1, "9999x9999", sz); expect("a note above 3840x2160 is ignored: 2560x1440 refused (-1)", 1600 * 10 - 1, sz[0] * 10 + r, 0);
    r = scene_size("1920x1080", 1, "junk", sz); expect("a note that is not a size: the output size still allowed (1)", 1080 * 10 + 1, sz[1] * 10 + r, 0);
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
