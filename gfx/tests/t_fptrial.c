// Frame-rate trials (gfx/nvn/nvn_fptrial.h, plan item 216) against a model of Tears of the Kingdom's frame-rate controller and of a GPU whose clock
// follows the frame rate. The controller as games/totk/check_frame_rate.py runs it: desired = trunc(time * 60 / 1000) + 1 of the slower of CPU and
// GPU time, raised after 4 frames over, lowered when desired is lower and the 10-frame cooldown is over. The GPU: a frame of work w takes w / clock,
// and the clock heads for the one that fills 3/4 of the frame's interval (0.45 to 1 of the top: an iPhone's GPU 24 ms at 30 fps for 11 ms of work,
// 12.5 ms at 60), falling over 1 s and rising over `ramp` s; the controller reads the GPU time as 3 ms from the frame's start to the GPU's start
// plus the factor times the GPU time. A light scene (11 ms of work at the top clock) stays at 30 fps for
// good without trials and holds 60 after one with them; a heavy one (22 ms) and a CPU-bound one (18 ms of CPU) fail and wait longer each time; a
// hold or a target of 30 starts none; a game at 60 is left alone. Without the grace period (-DDM_TEST_FPT_NO_GRACE) both light scenes fail their
// trials while the clock is still low, as short probes do: 5 fail; without the back-off (-DDM_TEST_FPT_NO_BACKOFF) the heavy scene is tried 48
// times in 5 minutes: 1 fails.
#include "../nvn/nvn_fptrial.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static int n, fail;
static void check(const char *what, int ok, const char *fmt, double a, double b) {
    n++; fail += !ok; printf("%-80s ", what); printf(fmt, a, b); printf(" %s\n", ok ? "✓" : "✗");
}
typedef struct { double work, cpu, ramp; int want, start_itv; double start_clock; } scene;
typedef struct { int itv_end, st_end; double at60, first60; uint64_t trials, held, failed, unlowered; int changes; double max_factor_at60; } result;
static result run(const scene *sc, double seconds, int trials_on) {
    nvn_fptrial s; memset(&s, 0, sizeof s);
    int cur = sc->start_itv, over = 0, cool = 0, prev = cur; double clock = sc->start_clock, t = 0, factor = 1;
    result r; memset(&r, 0, sizeof r); r.first60 = -1;
    while (t < seconds) {
        double gpu = sc->work / clock;                          // ms
        double seen = 3.0 + factor * gpu, cost = sc->cpu > seen ? sc->cpu : seen;
        int desired = (int)(cost * 60 / 1000) + 1; if (desired > 3) desired = 3;
        if (desired > cur) { if (++over >= 4) { cur = desired; over = 0; cool = 10; } }
        else { over = 0; if (desired < cur && !cool) { cur = desired; cool = 10; } }
        if (cool) cool--;
        double frame = cur * 1000.0 / 60; if (gpu > frame) frame = ceil(gpu * 60 / 1000) * 1000.0 / 60;   // ms on screen
        t += frame / 1000;
        double want_clock = sc->work / (0.75 * cur * 1000.0 / 60); if (want_clock < 0.45) want_clock = 0.45; if (want_clock > 1) want_clock = 1;
        if (want_clock > clock) { clock += (1 - 0.45) * (frame / 1000) / sc->ramp; if (clock > want_clock) clock = want_clock; }
        else { clock -= (1 - 0.45) * (frame / 1000) / 1.0; if (clock < want_clock) clock = want_clock; }
        if (cur != prev) r.changes++;
        prev = cur;
        if (cur == 1) { r.at60 += frame / 1000; if (r.first60 < 0) r.first60 = t; if (factor > r.max_factor_at60) r.max_factor_at60 = factor; }
        factor = trials_on ? nvn_fptrial_step(&s, t, cur, sc->want) : 1.0;
    }
    r.itv_end = cur; r.st_end = s.st; r.trials = s.trials; r.held = s.held; r.failed = s.failed; r.unlowered = s.unlowered;
    return r;
}
int main(void) {
    result r;
    const scene light = {.work = 11, .cpu = 8, .ramp = 0.5, .want = 1, .start_itv = 2, .start_clock = 0.45};
    r = run(&light, 60, 0);
    check("light scene, no trials: the model keeps it at 30 fps (the trap)", r.itv_end == 2 && r.at60 == 0, "interval %.0f, %.1f s at 60", r.itv_end, r.at60);
    r = run(&light, 60, 1);
    check("light scene with trials: at 60 fps at the end", r.itv_end == 1, "interval %.0f", r.itv_end, 0);
    check("  the first trial after the wait, 60 fps reached within it", r.first60 > FPT_WAIT0 && r.first60 < FPT_WAIT0 + 2, "60 at %.2f s, wait %.0f s", r.first60, FPT_WAIT0);
    check("  one trial, held, none failed", r.trials == 1 && r.held == 1 && r.failed == 0, "trials %.0f, failed %.0f", r.trials, r.failed);
    check("  the real times again once the trial held (factor 1 at 60 fps after it)", r.at60 > 40, "%.1f s at 60 of 60, factor up to %.2f", r.at60, r.max_factor_at60);
    const scene slow_ramp = {.work = 11, .cpu = 8, .ramp = 0.8, .want = 1, .start_itv = 2, .start_clock = 0.45};
    r = run(&slow_ramp, 60, 1);
    check("light scene, the clock coming up over 0.8 s: holds 60 after the grace period", r.itv_end == 1 && r.failed == 0, "interval %.0f, failed %.0f", r.itv_end, r.failed);
    check("  and spends most of the minute at 60", r.at60 > 40, "%.1f s at 60", r.at60, 0);
    const scene heavy = {.work = 22, .cpu = 8, .ramp = 0.5, .want = 1, .start_itv = 2, .start_clock = 0.45};
    r = run(&heavy, 300, 1);
    check("heavy scene (22 ms at the top clock): 30 fps at the end, every trial failed", r.itv_end == 2 && r.held == 0 && r.failed == r.trials, "interval %.0f, held %.0f", r.itv_end, r.held);
    check("  the waits double: at most 14 trials in 5 minutes", r.trials >= 3 && r.trials <= 14, "%.0f trials", r.trials, 0);
    const scene cpu = {.work = 6, .cpu = 18, .ramp = 0.5, .want = 1, .start_itv = 2, .start_clock = 0.45};
    r = run(&cpu, 120, 1);
    check("CPU-bound scene (18 ms of CPU): 30 fps, no trial (the last may run on) got interval 1", r.itv_end == 2 && r.held == 0 && r.unlowered + (r.st_end == FPT_PUSH) == r.trials && r.trials > 0,
          "interval %.0f, unlowered %.0f", r.itv_end, r.unlowered);
    const scene held = {.work = 11, .cpu = 8, .ramp = 0.5, .want = 0, .start_itv = 2, .start_clock = 0.45};
    r = run(&held, 60, 1);
    check("a hold or a target of 30 (want 0): no trial, 30 fps", r.trials == 0 && r.itv_end == 2, "trials %.0f, interval %.0f", r.trials, r.itv_end);
    const scene at60 = {.work = 9, .cpu = 8, .ramp = 0.5, .want = 1, .start_itv = 1, .start_clock = 1};
    r = run(&at60, 60, 1);
    check("a game already at 60 fps: no trial, no change", r.trials == 0 && r.changes == 0 && r.itv_end == 1, "trials %.0f, changes %.0f", r.trials, r.changes);
    printf("%d checks, %d failed\n", n, fail);
    return fail != 0;
}
