// Pacer unit tests: given a clock sequence, the expected wait durations are hand-calculated. The period 16.666 ms is written as P = 16666667 ns.
#include "../nvn/nvn_pace.h"
#include <stdio.h>

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
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
