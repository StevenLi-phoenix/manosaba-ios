// A frame step in whole vsyncs decided by the host (plan item 133; the design and its numbers are nxnative's nvn_fstep.h, seedds, MIT, written
// anew here). For a game whose step the host sets (Tears of the Kingdom with the realtime option: games/totk/frame_step.c) the host keeps each
// frame on screen k vsyncs (1/60 s) and hands the game k as its step, so the step is the time the frame is shown and motion is even; real time
// the game falls behind (a late frame, a hitch) is a debt paid back one vsync a frame (the step k + 1 while the frame still shows k), so game
// time follows real time. k follows the frames' cost: three late frames in a row step up to the cost; 30 frames in a row whose cost fits
// 0.85 × (k − 1) try k − 1 for 30 frames, and a trial that fails doubles the wait before the next (1 s up to 8 s), so a device on the edge does
// not flap. kmin: 1 at 60 fps, 2 while a movie is held at 30 (item 130); kmax: 3, the largest interval the game's own logic knows.
// Unit test gfx/tests/t_fstep.c. Pure: no clock, no I/O.
#pragma once
#include <math.h>
#include <stdint.h>

enum { FSTEP_MAX = 3, FSTEP_RUN = 30 };
#define FSTEP_LATE 0.1        // late: shown or cost over k by more than a tenth of a vsync (1.7 ms)
#define FSTEP_FIT 0.85        // fits k − 1: cost at most this share of it
#define FSTEP_DEBT_MAX 15.0   // the debt kept at most (0.25 s): the rest of a long stall (a load) is dropped, not replayed
#define FSTEP_HOLD0 60.0      // vsyncs after a step up before a trial (1 s), doubled after each failed trial up to FSTEP_HOLD_MAX
#define FSTEP_HOLD_MAX 480.0

typedef struct {
    int k, step, kmin;          // vsyncs a frame stays on screen; the step for the next frame; the last call's kmin
    double debt;                // real time the game is behind, in vsyncs (at most one vsync ahead is remembered)
    int late_run, fit_run, trial;
    double hold, since;         // the wait before a trial; vsyncs since the last step up
    uint64_t frames, late, paid, ups, downs, failed; double dropped;   // for the log
} nvn_fstep;

// One frame ended: shown = real time since the previous frame's start, cost = its work (the larger of CPU and GPU), both in vsyncs. Returns the
// step for the next frame; s->k is how long that frame stays on screen
static inline int nvn_fstep_next(nvn_fstep *s, double shown, double cost, int kmin, int kmax) {
    if (kmin < 1) kmin = 1;
    if (kmax > FSTEP_MAX) kmax = FSTEP_MAX;
    if (kmax < kmin) kmax = kmin;
    if (s->hold <= 0) s->hold = FSTEP_HOLD0;
    s->frames++;
    if (!s->k) { s->k = s->step = s->kmin = kmin; return s->step; }   // the first frame: nothing measured yet
    int held = s->k;
#ifndef DM_TEST_FSTEP_NO_DEBT   // negative control of t_fstep only: real time lost is never paid back
    s->debt += shown - s->step;
#endif
    if (s->debt > FSTEP_DEBT_MAX) { s->dropped += s->debt - FSTEP_DEBT_MAX; s->debt = FSTEP_DEBT_MAX; }
    if (s->debt < -1) s->debt = -1;
    s->since += shown;
    if (kmin < s->kmin || s->k < kmin) s->k = kmin;   // a hold ended (the lower kmin takes effect at once) or began
    if (s->k > kmax) s->k = kmax;
    s->kmin = kmin;
    int late = shown > held + FSTEP_LATE || cost > held + FSTEP_LATE;
    if (late) { s->late++; s->late_run++; s->fit_run = 0; } else s->late_run = 0;
    if (s->late_run >= 3 && s->k < kmax) {   // step up to the cost, at least one vsync
        int want = (int)ceil(cost - 0.05);
        if (want < s->k + 1) want = s->k + 1;
        if (want > kmax) want = kmax;
        if (s->trial) { s->failed++; s->trial = 0; s->hold = s->hold * 2 < FSTEP_HOLD_MAX ? s->hold * 2 : FSTEP_HOLD_MAX; }
        s->k = want; s->ups++; s->late_run = s->fit_run = 0; s->since = 0;
    } else if (s->trial && !--s->trial) s->hold = FSTEP_HOLD0;   // the trial held
#ifndef DM_TEST_FSTEP_NO_MARGIN   // negative control of t_fstep only: any cost below k − 1 counts as fitting
    double fit = FSTEP_FIT;
#else
    double fit = 1.0;
#endif
    if (!late) s->fit_run = s->k > kmin && cost <= fit * (s->k - 1) ? s->fit_run + 1 : 0;
    if (s->fit_run >= FSTEP_RUN && s->since >= s->hold && !s->trial) { s->k--; s->downs++; s->trial = FSTEP_RUN; s->fit_run = 0; }
    s->step = s->k + (s->debt >= 1 && s->k < FSTEP_MAX);
    s->paid += s->step > s->k;
    return s->step;
}
