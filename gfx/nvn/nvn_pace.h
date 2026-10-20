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

// Dropped and slow frames judged against each frame's own hold (plan item 156, after nxnative e7a84c7, MIT, written anew): a frame is meant to stay
// on screen its present period × interval (hold_ms, 1/60 s when unpaced); the on-screen interval between presents n - 1 and n shows frame n - 1,
// judged against the larger of their two holds (a change of interval between them is not a drop). Dropped: discarded (-1) or on screen over 1.5 ×
// that; slow (the frame interval line): over 1.1 × its hold. Against a fixed 16.7 / 25 ms every frame of a 30 fps game counted (299 of 300).
// Unit test tests/t_pace.c
static inline double nvn_hold_ms(uint64_t period_ns) { return period_ns ? period_ns / 1e6 : 1000.0 / 60; }
static inline double nvn_expect_ms(double hold_prev, double hold_cur) { return hold_prev > hold_cur ? hold_prev : hold_cur; }
static inline int nvn_dropped(double iv_ms, double expect_ms) {
#ifdef DM_TEST_DROP_60HZ   // negative control of t_pace only: against 60 fps whatever the hold, as before
    expect_ms = 1000.0 / 60;
#endif
    return iv_ms < 0 || iv_ms > 1.5 * expect_ms;
}
static inline int nvn_slow(double iv_ms, double hold_ms) { return iv_ms > 1.1 * hold_ms; }

// FIFO consumption at the display link (plan item 140; acnh-mobile c2af194 consumes FIFO at the game's period, MIT, written anew): the link shows
// the oldest queued frame instead of the newest, for a game that steps its logic once per frame (game.json present_consume "fifo": manosaba); the
// newest drops the frames queued before it, each a step of the game never shown (manosaba on an iPhone: 14 by tick 1,549). A frame stays at least
// its present period from the tick that showed it, as the console's swap interval counts vsyncs from the previous flip (acnh-mobile keeps the
// phase instead, which shows the frame after a late one for a single tick at interval 2). now = the tick's target time, p->target = the last
// shown. 1: the head frame is due; 0: the frame on screen stays this tick. A tick up to 2 ms early is due (timestamps wander; ticks are 8.3 ms
// apart at 120 Hz). Unit test tests/t_pace.c
static inline int nvn_fifo_due(nvn_pacer *p, uint64_t now, uint64_t period) {
#ifndef DM_TEST_FIFO_NO_PERIOD   // negative control of t_pace only: one frame every tick whatever its period
    if (period && p->target && now + 2000000 < p->target + period) return 0;
#endif
    p->target = now;
    return 1;
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

// The iOS frame rate the device can afford (plan item 135, after super-mario-odyssey-ios's frame pacer, MIT): the player's setting, held at 30 in
// Low Power Mode or from thermal state Serious up (NSProcessInfoThermalStateSerious = 2, Critical = 3), where iOS throttles the device anyway and a
// game paced for 60 misses frames unevenly. Unit test tests/t_pace.c
static inline int nvn_fps_policy(int user_fps, int low_power, int thermal) {
#ifdef DM_TEST_POLICY_USER_ONLY   // negative control of t_pace only: the setting alone, as before
    (void)low_power; (void)thermal; return user_fps;
#endif
    return (low_power || thermal >= 2) && user_fps > 30 ? 30 : user_fps;
}

// Uncapped pacing (plan item 53, after MacNativeGames' TOTK_UNCAPPED, ly05010419/MacNativeGames, MIT): for a game whose step follows the measured
// frame time (the host's dm_game_realtime_step), the game does not wait for the display. Our own clock paces it at up to `cap` Hz (its present
// interval × 1 / cap a frame) and the display link shows the newest finished frame at each refresh, so a scene slower than 60 fps shows at its own
// rate instead of the next step down (30). Off with DM_UNCAPPED=0, with DM_PRESENT_HZ (a fixed rate asked for) and on a display of 120 Hz or more
// (nothing to gain there). The cap: DM_UNCAPPED_HZ=30..120 or "display" (the display's rate, 60 without one); 120 at most, as the frame step counts a
// frame as at least 1/120 s. Returns the clock rate in Hz, 0 for the usual pacing.
// ProMotion (plan item 172, after Touchable's SS_DISPLAY_HZ=120, MIT): on a screen of 120 Hz or more a game whose step follows the measured frame time
// presents at 120 while the screen runs at 120 (the front end asks it for 80-120 Hz and reports the rate it runs at: iOS lowers it in Low Power Mode),
// at 60 otherwise; a game that steps per frame (manosaba) stays at 60, as it would run twice as fast. DM_PROMOTION=0 or DM_PRESENT_HZ (a fixed rate)
// leave the rate alone. Returns the present rate for the screen's current rate now_hz, 0 to leave it as it is
static inline int nvn_promotion_hz(int realtime_step, const char *present_hz, const char *promotion, int display_hz, int now_hz) {
    if (present_hz || (promotion && !strcmp(promotion, "0")) || display_hz < 120) return 0;
#ifndef DM_TEST_PROMOTION_ANY_STEP   // negative control of t_pace only: a game stepping per frame at 120 too
    if (!realtime_step) return 0;
#endif
    return now_hz >= 110 ? 120 : 60;
}
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
