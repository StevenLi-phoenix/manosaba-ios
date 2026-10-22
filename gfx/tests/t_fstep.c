// The frame step in whole vsyncs (gfx/nvn/nvn_fstep.h, plan item 133; scenarios after nxnative's t_fstep, MIT, numbers our own model's). A
// frame is shown k vsyncs, or up to the next whole vsync after its cost when it took longer (a missed vsync); the game steps by the step handed
// at the end of the previous frame. Steady 10 ms frames: 60 fps, game time = real time; steady 22 ms: 30 fps at once and for good; a 100 ms
// hitch: paid back within a few frames; a 0.5 s stall: 15 vsyncs paid back, the rest dropped; heavy then light: back to 60 fps after a trial;
// a movie hold (kmin 2): 30 fps during, 60 the frame after; costs jittering just under a vsync at 30 fps: no flapping. Real time lost never paid
// back (-DDM_TEST_FSTEP_NO_DEBT): 3 fail; no margin on "fits k - 1" (-DDM_TEST_FSTEP_NO_MARGIN): 1 fail.
// With opt (plan item 222) against a GPU whose clock follows the frame rate (as t_fptrial's: 0.45 to 1 of the top, heading for the clock that fills
// 3/4 of the interval, falling over 1 s, rising over 0.5 s): a light scene (11 ms of work at the top clock) held at 30 fps stays there without opt
// and reaches 60 for good with it; a heavy one (22 ms) stays at 30 with its trials failing at growing waits. Without the grace period
// (-DDM_TEST_FSTEP_NO_GRACE) the light scene's trials fail while the clock is low: 2 fail.
#include "../nvn/nvn_fstep.h"
#include <stdio.h>
#include <string.h>
static int n, fail;
static void check(const char *what, int ok, const char *fmt, double a, double b) {
    n++; fail += !ok; printf("%-78s ", what); printf(fmt, a, b); printf(" %s\n", ok ? "✓" : "✗");
}
typedef struct { double game, real; int changes, k_end; } sim;
// cost(i) and kmin(i) per frame
static sim run(nvn_fstep *s, int frames, double (*cost)(int), int (*kmin)(int), int *ks) {
    sim r = {0}; int step = kmin(0), prevk = -1;
    memset(s, 0, sizeof *s); nvn_fstep_next(s, 0, 0, kmin(0), FSTEP_MAX);
    for (int i = 0; i < frames; i++) {
        int k = s->k; double c = cost(i);
        double shown = c > k ? ceil(c - 1e-9) : k;
        r.game += step; r.real += shown;
        step = nvn_fstep_next(s, shown, c, kmin(i + 1), FSTEP_MAX);
        if (ks) ks[i] = s->k;
        if (prevk >= 0 && s->k != prevk) r.changes++;
        prevk = s->k;
    }
    r.k_end = s->k; return r;
}
static int km1(int i) { (void)i; return 1; }
static double c10ms(int i) { (void)i; return 0.6; }
static double c22ms(int i) { (void)i; return 1.32; }
static double hitch(int i) { return i == 100 ? 5.5 : 0.6; }
static double stall(int i) { return i == 100 ? 30 : 0.6; }
static double heavy_light(int i) { return i < 200 ? 1.5 : 0.5; }
static int movie(int i) { return i < 100 ? 2 : 1; }
static double light(int i) { (void)i; return 0.5; }
static double jitter(int i) { return i < 10 ? 1.5 : (i % 43) >= 40 ? 1.2 : 0.92; }   // 30 fps, then 0.92 vsync with three 1.2 in a row every 43 frames
// A scene of w vsyncs of GPU work at the top clock and c of CPU, started at k = 2 with the clock low; returns the share of time at k = 1 in the
// last half and k at the end
typedef struct { double at1_late_half; int k_end; uint64_t downs, failed; } dsim;
static dsim run_dvfs(nvn_fstep *s, int opt, double w, double c, double seconds) {
    memset(s, 0, sizeof *s); s->opt = opt; nvn_fstep_next(s, 0, 0, 1, FSTEP_MAX); s->k = 2;
    double clock = 0.45, t = 0, at1 = 0, half = seconds / 2; dsim r; memset(&r, 0, sizeof r);
    while (t < seconds) {
        int k = s->k; double gpu = w / clock, cost = gpu > c ? gpu : c;
        double shown = cost > k ? ceil(cost - 1e-9) : k;
        double want = w / (0.75 * k); if (want < 0.45) want = 0.45; if (want > 1) want = 1;
        if (want > clock) { clock += 0.55 * shown / 60 / 0.5; if (clock > want) clock = want; }
        else { clock -= 0.55 * shown / 60 / 1.0; if (clock < want) clock = want; }
        t += shown / 60; if (t > half && k == 1) at1 += shown / 60;
        nvn_fstep_next(s, shown, cost, 1, FSTEP_MAX);
    }
    r.at1_late_half = at1 / (seconds - half); r.k_end = s->k; r.downs = s->downs; r.failed = s->failed; return r;
}
int main(void) {
    nvn_fstep s; sim r; static int ks[2000];
    r = run(&s, 600, c10ms, km1, NULL);
    check("10 ms frames: k = 1 (60 fps) throughout", r.k_end == 1 && r.changes == 0, "k %.0f, changes %.0f", r.k_end, r.changes);
    check("... game time = real time", r.game == r.real, "game %.0f real %.0f vsyncs", r.game, r.real);
    r = run(&s, 600, c22ms, km1, ks);
    check("22 ms frames: k = 2 (30 fps) within 4 frames and for good", ks[3] == 2 && r.k_end == 2 && r.changes == 1, "k at frame 3 %.0f, changes %.0f", ks[3], r.changes);
    check("... game time within 2 vsyncs of real time", r.real - r.game <= 2 && r.game - r.real <= 1, "game %.0f real %.0f", r.game, r.real);
    r = run(&s, 300, hitch, km1, NULL);
    check("a 100 ms hitch: paid back (game time within a vsync of real time)", r.real - r.game <= 1 && r.game - r.real <= 1 && s.paid >= 4, "real - game %.0f, paid %.0f", r.real - r.game, (double)s.paid);
    r = run(&s, 300, stall, km1, NULL);
    check("a 0.5 s stall: 15 vsyncs paid back, the rest dropped", s.dropped >= 14 && r.real - r.game - s.dropped <= 1 && r.real - r.game - s.dropped >= -1, "dropped %.0f, real - game %.0f", s.dropped, r.real - r.game);
    r = run(&s, 600, heavy_light, km1, NULL);
    check("heavy (25 ms) then light (8 ms): back to k = 1 after a trial", r.k_end == 1 && s.downs >= 1, "k %.0f, downs %.0f", r.k_end, (double)s.downs);
    r = run(&s, 200, light, movie, ks);
    check("a movie hold (kmin 2 for frames 0-99): 30 fps during, 60 from frame 100", ks[50] == 2 && ks[98] == 2 && ks[99] == 1, "k of frame 99 %.0f, of frame 100 %.0f", ks[98], ks[99]);   // ks[i]: frame i + 1
    r = run(&s, 2000, jitter, km1, NULL);
    check("costs jittering under a vsync at 30 fps (0.92, three 1.2 in every 43): no flapping", r.changes <= 2, "changes %.0f, failed trials %.0f", r.changes, (double)s.failed);
    dsim d = run_dvfs(&s, 0, 0.66, 0.5, 60);
    check("opt off, a light scene (11 ms at the top clock) held at 30 fps with the clock low: stays at 30", d.k_end == 2 && d.at1_late_half == 0, "k %.0f, share at 60 %.2f", d.k_end, d.at1_late_half);
    d = run_dvfs(&s, 1, 0.66, 0.5, 60);
    check("opt on, the same scene: 60 fps at the end", d.k_end == 1, "k %.0f%.0s", d.k_end, 0);
    check("... and through the second half of the minute", d.at1_late_half > 0.95, "share at 60 %.2f, failed trials %.0f", d.at1_late_half, (double)d.failed);
    d = run_dvfs(&s, 1, 1.32, 0.5, 300);
    check("opt on, a heavy scene (22 ms at the top clock): 30 fps at the end, under a tenth of the second half in trials", d.k_end == 2 && d.at1_late_half < 0.10, "k %.0f, share at 60 %.2f", d.k_end, d.at1_late_half);
    check("... the waits double: at most 14 trials in 5 minutes", d.downs >= 3 && d.downs <= 14, "%.0f trials%.0s", (double)d.downs, 0);
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
