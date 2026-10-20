// Present pacer (pure function, shared by Mac / iOS; unit test tests/t_pace.c).
// Semantics as vsync: present slot = previous present slot + period. Early → wait until the present slot; late by less than one period → present immediately and keep the original phase;
// behind by one period or more (stutter) → restart the cadence from the current time, no burst of presents to catch up. Period 0 = no wait (the present slot follows the current time).
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint64_t target; } nvn_pacer;

// now: monotonic clock (ns); period: this frame's period (ns, 0 = no wait). Returns the ns to wait (0 = present immediately) and updates the internal target time.
static inline uint64_t nvn_pace(nvn_pacer *p, uint64_t now, uint64_t period) {
    if (!period || !p->target) { p->target = now; return 0; }
    uint64_t next = p->target + period;
    if (now <= next) { p->target = next; return next - now; }
    p->target = now - next < period ? next : now;
    return 0;
}

// Display-link ticks one frame takes (plan item 123): the game's present interval counts refreshes of the present rate hz, the link ticks at link_hz
// (30 in the power-saving mode, 120 in pacing mode 2), so a frame takes interval x link_hz / hz ticks, rounded up, at least one. The step used to
// be the interval (x 2 in mode 2) whatever the link's rate: Tears of the Kingdom at interval 2 with the 30 fps setting got two 30 Hz ticks a frame,
// 15 fps. Rounded up, interval 3 at 30 Hz gives 2 ticks (15 fps for the 20 the game asks; never faster than asked). Unit test tests/t_pace.c
static inline unsigned nvn_link_step(unsigned itv, unsigned hz, unsigned link_hz) {
    if (!itv) itv = 1;
    if (!hz) hz = 60;
#ifndef DM_TEST_LINK_STEP_FIXED   // negative control of t_pace only: the interval, doubled at 120 Hz, as before
    unsigned s = (itv * link_hz + hz - 1) / hz;
    return s ? s : 1;
#else
    return itv * (link_hz >= 120 && hz < 120 ? 2 : 1);
#endif
}

// A frame-rate hold (plan item 130): while the host holds the game at hold_fps (hle_oe.c: Tears of the Kingdom's pre-rendered movies at 30),
// a frame takes at least the present interval that gives that rate at the present rate hz, rounded up (never faster than the hold): 2 at 60 Hz,
// 3 at 90, 4 at 120. A longer interval of the game's own stays. hold_fps 0 = no hold. Unit test tests/t_pace.c
static inline int nvn_hold_interval(int itv, int hz, int hold_fps) {
    if (itv < 1) itv = 1;
#ifndef DM_TEST_HOLD_IGNORED   // negative control of t_pace only: the game's interval whatever the hold
    if (hold_fps > 0 && hz > 0) { int h = (hz + hold_fps - 1) / hold_fps; if (h > itv) itv = h; }
#endif
    return itv;
}

// Uncapped pacing (plan item 53, after MacNativeGames' TOTK_UNCAPPED, ly05010419/MacNativeGames, MIT): for a game whose step follows the measured
// frame time (the host's dm_game_realtime_step), the game does not wait for the display. Our own clock paces it at up to `cap` Hz (its present
// interval × 1 / cap a frame) and the display link shows the newest finished frame at each refresh, so a scene slower than 60 fps shows at its own
// rate instead of the next step down (30). Off with DM_UNCAPPED=0, with DM_PRESENT_HZ (a fixed rate asked for) and on a display of 120 Hz or more
// (nothing to gain there). The cap: DM_UNCAPPED_HZ=30..120 or "display" (the display's rate, 60 without one); 120 at most, as the frame step counts a
// frame as at least 1/120 s. Returns the clock rate in Hz, 0 for the usual pacing.
static inline int nvn_uncapped_hz(int realtime_step, const char *present_hz, const char *uncapped, const char *cap, int display_hz) {
#ifdef DM_TEST_UNCAPPED_ANY_STEP   // negative control of t_pace only: a game stepping per frame paced uncapped too (it would run fast)
    (void)realtime_step;
    if (present_hz || (uncapped && !strcmp(uncapped, "0")) || display_hz >= 120) return 0;
#else
    if (!realtime_step || present_hz || (uncapped && !strcmp(uncapped, "0")) || display_hz >= 120) return 0;
#endif
    int v = !cap || !*cap ? 120 : !strcmp(cap, "display") ? (display_hz > 0 ? display_hz : 60) : atoi(cap);
    return v < 30 ? 30 : v > 120 ? 120 : v;
}
