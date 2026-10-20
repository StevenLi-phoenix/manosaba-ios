// Pacer unit tests: given a clock sequence, the expected wait durations are hand-calculated. The period 16.666 ms is written as P = 16666667 ns.
#include "../nvn/nvn_pace.h"
#include <stdio.h>

// FIFO consumption at the display link (nvn_fifo_due, plan item 140) against taking the newest. A 60 Hz link from t = 1 s whose tick timestamps
// wander up to 0.2 ms either way; a game presenting at its period, 2 ms before the tick, but every 10th frame 0.7 period late, so the next one is
// queued behind it by the following tick; at most 3 frames queued (the game waits at its acquire, then presents right after the tick that freed
// one); ticks [off_a, off_b) never come (a silent link); 600 ticks
typedef struct { int produced, shown, dropped, in_order, min_gap, held; } fifo_sim;   // held: ticks a frame was queued and its period over, not shown
static fifo_sim fifo_run(int fifo, int itv, int off_a, int off_b) {
    const uint64_t P = 16666667, period = (uint64_t)itv * P, T0 = 1000000000ull;
    int qid[4], qn = 0, next = 0, last_k = -1, last_id = -1; uint64_t ready_after = 0; nvn_pacer pc = {0};
    fifo_sim s = {0, 0, 0, 1, 1 << 30, 0};
    for (int k = 0; k < 600; k++) {
        uint64_t T = T0 + (uint64_t)k * P + (uint64_t)(((int64_t)((k * 7919) % 41) - 20) * 10000);
        for (;;) {   // frames ready by this tick join the queue while it has room
            uint64_t r = T0 + (uint64_t)next * period - 2000000 + (next % 10 == 9 ? period * 7 / 10 : 0);
            if (r < ready_after) r = ready_after;
            if (r > T || qn >= 3) break;
            qid[qn++] = next++; s.produced++;
        }
        if (k >= off_a && k < off_b) continue;
        if (!qn) continue;
        int take;
        if (fifo) { if (!nvn_fifo_due(&pc, T, period)) { s.held += last_k >= 0 && k - last_k >= itv; continue; } take = 0; }
        else { take = qn - 1; s.dropped += qn - 1; }
        if (last_k >= 0) { int gap = k - last_k; if (gap < s.min_gap) s.min_gap = gap; }
        s.in_order &= qid[take] > last_id; last_id = qid[take]; last_k = k; s.shown++;
        int full = qn >= 3;
        if (fifo) { for (int i = 1; i < qn; i++) qid[i - 1] = qid[i]; qn--; } else qn = 0;
        if (full) ready_after = T + 1;   // the game waited for room: its next frame comes right after this tick
    }
    return s;
}

int main(void) {
    const uint64_t P = 16666667, MS = 1000000;
    struct { uint64_t now, period, want; const char *why; } c[] = {
        {1000 * MS, P, 0, "first frame: start the cadence, no wait"},
        {1005 * MS, P, 1000 * MS + P - 1005 * MS, "5 ms early: wait until 1000+P"},
        {1000 * MS + 2 * P - 2 * MS, P, 2 * MS, "third frame 2 ms early: wait 2 ms"},
        {1000 * MS + 3 * P + 3 * MS, P, 0, "3 ms late (less than one period): present immediately, present slot stays 1000+3P"},
        {1000 * MS + 4 * P - 1 * MS, P, 1 * MS, "next frame 1 ms early: wait until 1000+4P on the original phase"},
        {1000 * MS + 7 * P, P, 0, "two periods behind (stutter): restart the cadence from now, no burst of catch-up presents"},
        {1000 * MS + 7 * P + 4 * MS, P, P - 4 * MS, "next frame after the restart: wait until the new target"},
        {1000 * MS + 9 * P, 0, 0, "period 0 (DM_PRESENT_HZ=0): no wait"},
        {1000 * MS + 9 * P + 1 * MS, 2 * P, 2 * P - 1 * MS, "switch back to interval 2: present slot = previous frame time 9P + 2P"},
        {1000 * MS + 11 * P + 3 * MS, 2 * P, 2 * P - 3 * MS, "interval 2, next frame: present slot 13P"},
    };
    nvn_pacer p = {0}; int fail = 0, n = sizeof c / sizeof *c;
    for (int i = 0; i < n; i++) {
        uint64_t got = nvn_pace(&p, c[i].now, c[i].period);
        int ok = got == c[i].want;
        printf("%-52s expected wait %8.3f ms → got %8.3f ms %s\n", c[i].why, c[i].want / 1e6, got / 1e6, ok ? "✓" : "✗");
        fail += !ok;
    }
    // Uncapped pacing (nvn_uncapped_hz, plan item 53): when our own clock paces the game, and at what rate
    struct { int rt; const char *phz, *unc, *cap; int disp, want; const char *why; } u[] = {
        {1, NULL, NULL, NULL, 0, 120, "a real-time step, no window: uncapped at 120"},
        {1, NULL, NULL, NULL, 60, 120, "... on a 60 Hz display: 120 (the display shows the newest frame)"},
        {1, NULL, NULL, NULL, 100, 120, "... on a 100 Hz display: 120"},
        {1, NULL, NULL, NULL, 120, 0, "... on a 120 Hz display: the usual pacing (nothing to gain)"},
        {0, NULL, NULL, NULL, 60, 0, "a step per frame (30 fps product, manosaba): the usual pacing"},
        {1, "90", NULL, NULL, 60, 0, "DM_PRESENT_HZ asked for: the usual pacing at that rate"},
        {1, NULL, "0", NULL, 60, 0, "DM_UNCAPPED=0: off"},
        {1, NULL, "1", NULL, 60, 120, "DM_UNCAPPED=1: on"},
        {1, NULL, NULL, "display", 60, 60, "DM_UNCAPPED_HZ=display on 60 Hz: 60"},
        {1, NULL, NULL, "display", 0, 60, "DM_UNCAPPED_HZ=display without one: 60"},
        {1, NULL, NULL, "90", 60, 90, "DM_UNCAPPED_HZ=90"},
        {1, NULL, NULL, "10", 60, 30, "DM_UNCAPPED_HZ=10: 30 at least"},
        {1, NULL, NULL, "240", 60, 120, "DM_UNCAPPED_HZ=240: 120 at most (the step's 1/120 s floor)"},
    };
    for (int i = 0; i < (int)(sizeof u / sizeof *u); i++) {
        int got = nvn_uncapped_hz(u[i].rt, u[i].phz, u[i].unc, u[i].cap, u[i].disp), ok = got == u[i].want; n++;
        printf("%-72s expected %3d → got %3d %s\n", u[i].why, u[i].want, got, ok ? "✓" : "✗"); fail += !ok;
    }
    // Display-link ticks a frame takes (nvn_link_step, plan item 123); the old step (-DDM_TEST_LINK_STEP_FIXED, the interval whatever the link's
    // rate) fails the three power-saving cases at interval 2, 3 and 4
    struct { unsigned itv, hz, lhz, want; const char *why; } k[] = {
        {1, 60, 60, 1, "interval 1, 60 Hz link: 1 tick (60 fps)"},
        {2, 60, 60, 2, "interval 2, 60 Hz link: 2 ticks (30 fps)"},
        {2, 60, 120, 4, "interval 2, 120 Hz link (mode 2): 4 ticks (30 fps)"},
        {2, 60, 30, 1, "interval 2, power-saving 30 Hz link: 1 tick (30 fps, was 2: 15 fps)"},
        {1, 60, 30, 1, "interval 1, 30 Hz link: 1 tick (30 fps, the cap)"},
        {3, 60, 30, 2, "interval 3, 30 Hz link: 2 ticks (15 fps; never faster than the 20 asked)"},
        {4, 60, 30, 2, "interval 4, 30 Hz link: 2 ticks (15 fps, as asked; was 4: 7.5 fps)"},
        {2, 120, 120, 2, "interval 2 of a 120 Hz present rate on a 120 Hz link: 2 ticks (60 fps)"},
    };
    for (int i = 0; i < (int)(sizeof k / sizeof *k); i++) {
        unsigned got = nvn_link_step(k[i].itv, k[i].hz, k[i].lhz); int ok = got == k[i].want; n++;
        printf("%-72s expected %3u → got %3u %s\n", k[i].why, k[i].want, got, ok ? "✓" : "✗"); fail += !ok;
    }
    // The frame-rate hold (nvn_hold_interval, plan item 130: Tears of the Kingdom's pre-rendered movies at 30 fps); the game's interval whatever the
    // hold (-DDM_TEST_HOLD_IGNORED) fails 4
    struct { int itv, hz, hold, want; const char *why; } h[] = {
        {1, 60, 30, 2, "the 60 fps product in a movie: interval 2 (30 fps)"},
        {2, 60, 30, 2, "the 30 fps product: its own 2"},
        {3, 60, 30, 3, "the game's longer interval stays (3: 20 fps)"},
        {1, 90, 30, 3, "DM_PRESENT_HZ=90: 3"},
        {1, 120, 30, 4, "uncapped pacing at 120: 4"},
        {1, 100, 30, 4, "100 Hz: 4 (25 fps, never faster than the hold)"},
        {1, 60, 0, 1, "no hold: the game's interval"},
        {0, 60, 0, 1, "interval 0 counts as 1"},
        {1, 0, 30, 1, "present rate 0 (DM_PRESENT_HZ=0, profiling): no hold"},
    };
    for (int i = 0; i < (int)(sizeof h / sizeof *h); i++) {
        int got = nvn_hold_interval(h[i].itv, h[i].hz, h[i].hold), ok = got == h[i].want; n++;
        printf("%-72s expected %3d → got %3d %s\n", h[i].why, h[i].want, got, ok ? "✓" : "✗"); fail += !ok;
    }
    // The iOS frame rate policy (nvn_fps_policy, plan item 135); the setting alone (-DDM_TEST_POLICY_USER_ONLY) fails 3
    struct { int user, lp, th, want; const char *why; } fp[] = {
        {60, 0, 0, 60, "60 fps set, nominal: 60"},
        {60, 0, 1, 60, "60 fps set, thermal Fair: 60"},
        {60, 0, 2, 30, "60 fps set, thermal Serious: 30"},
        {60, 0, 3, 30, "60 fps set, thermal Critical: 30"},
        {60, 1, 0, 30, "60 fps set, Low Power Mode: 30"},
        {30, 0, 0, 30, "30 fps set: 30"},
        {30, 1, 3, 30, "30 fps set, Low Power and Critical: 30 (never above the setting)"},
    };
    for (int i = 0; i < (int)(sizeof fp / sizeof *fp); i++) {
        int got = nvn_fps_policy(fp[i].user, fp[i].lp, fp[i].th), ok = got == fp[i].want; n++;
        printf("%-72s expected %3d → got %3d %s\n", fp[i].why, fp[i].want, got, ok ? "✓" : "✗"); fail += !ok;
    }
    // FIFO consumption (nvn_fifo_due, plan item 140, see fifo_run); one frame every tick whatever its period (-DDM_TEST_FIFO_NO_PERIOD) fails 2
    {   fifo_sim nw = fifo_run(0, 1, 0, 0), ff = fifo_run(1, 1, 0, 0), f2 = fifo_run(1, 2, 0, 0), fs = fifo_run(1, 2, 200, 230);
        struct { int ok; const char *why; } f[] = {
            {nw.dropped > 0, "interval 1 with late frames, the newest taken: frames dropped (the problem)"},
            {ff.dropped == 0 && ff.in_order && ff.shown >= ff.produced - 3, "... FIFO: none dropped, every frame shown in order"},
            {f2.min_gap >= 2 && f2.held == 0 && f2.in_order, "interval 2, FIFO: a frame 2 ticks at least, never held past them (early ticks due)"},
            {fs.min_gap >= 2 && fs.in_order, "... after a silent link (30 ticks, 3 frames queued): 2 ticks a frame, no burst"},
        };
        printf("  newest: %d produced, %d shown, %d dropped; FIFO: %d / %d / %d; interval 2: %d / %d, smallest gap %d ticks; after the silent link: smallest gap %d\n",
               nw.produced, nw.shown, nw.dropped, ff.produced, ff.shown, ff.dropped, f2.produced, f2.shown, f2.min_gap, fs.min_gap);
        for (int i = 0; i < 4; i++) { n++; printf("%-72s %s\n", f[i].why, f[i].ok ? "✓" : "✗"); fail += !f[i].ok; } }
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
