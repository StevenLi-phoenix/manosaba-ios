// A frame step in whole vsyncs decided by the host (plan item 133; the design and its numbers are nxnative's nvn_fstep.h, seedds, MIT, written
// anew here). For a game whose step the host sets (Tears of the Kingdom with the realtime option: games/totk/frame_step.c) the host keeps each
// frame on screen k vsyncs (1/60 s) and hands the game k as its step, so the step is the time the frame is shown and motion is even; real time
// the game falls behind (a late frame, a hitch) is a debt paid back one vsync a frame (the step k + 1 while the frame still shows k), so game
// time follows real time. k follows the frames' cost: three late frames in a row step up to the cost; 30 frames in a row whose cost fits
// 0.85 × (k − 1) try k − 1 for 30 frames, and a trial that fails doubles the wait before the next (1 s up to 8 s), so a device on the edge does
// not flap. kmin: 1 at 60 fps, 2 while a movie is held at 30 (item 130); kmax: 3, the largest interval the game's own logic knows.
// With opt (plan item 222, after 216): an iPhone lowers its GPU clock while frames are held at 2 vsyncs, so the cost measured there never fits
// k − 1 though the frames would take far less at 60 (Great Sky Island: 20.65 ms of GPU at 30 fps, 11.09 ms at 60). After FSTEP_OPT0 vsyncs at
// k > kmin a trial of k − 1 starts whatever the cost, and its first FSTEP_GRACE frames do not count as late while the clock comes up; a failed
// one doubles that wait (up to FSTEP_OPT_MAX), one that holds sets it back.
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
#define FSTEP_OPT0 300.0      // with opt: vsyncs at k > kmin before a trial whatever the cost (5 s), doubled after each failed one
#define FSTEP_OPT_MAX 1920.0  // up to 32 s
enum { FSTEP_GRACE = 60 };    // with opt: frames of a trial that do not count as late (1 s at 60 fps)

typedef struct {
    int k, step, kmin;          // vsyncs a frame stays on screen; the step for the next frame; the last call's kmin
    double debt;                // real time the game is behind, in vsyncs (at most one vsync ahead is remembered)
    int late_run, fit_run, trial;
    double hold, since;         // the wait before a trial; vsyncs since the last step up
    int opt, grace, otrial;     // opt: trials whatever the cost (the caller sets it); frames of the trial still in its grace period; such a trial runs
    double opt_wait;            // with opt: the wait before such a trial
    uint64_t frames, late, paid, ups, downs, failed; double dropped;   // for the log
} nvn_fstep;

// One frame ended: shown = real time since the previous frame's start, cost = its work (the larger of CPU and GPU), both in vsyncs. Returns the
// step for the next frame; s->k is how long that frame stays on screen
static inline int nvn_fstep_next(nvn_fstep *s, double shown, double cost, int kmin, int kmax) {
    if (kmin < 1) kmin = 1;
    if (kmax > FSTEP_MAX) kmax = FSTEP_MAX;
    if (kmax < kmin) kmax = kmin;
    if (s->hold <= 0) s->hold = FSTEP_HOLD0;
    if (s->opt_wait <= 0) s->opt_wait = FSTEP_OPT0;
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
#ifndef DM_TEST_FSTEP_NO_GRACE   // negative control of t_fstep only: a trial's first frames count as late as any
    if (s->grace > 0) { s->grace--; late = 0; }   // the GPU clock still coming up (opt)
#endif
    if (late) { s->late++; s->late_run++; s->fit_run = 0; } else s->late_run = 0;
    if (s->late_run >= 3 && s->k < kmax) {   // step up to the cost, at least one vsync
        int want = (int)ceil(cost - 0.05);
        if (want < s->k + 1) want = s->k + 1;
        if (want > kmax) want = kmax;
        if (s->trial) { s->failed++; s->trial = 0; s->hold = s->hold * 2 < FSTEP_HOLD_MAX ? s->hold * 2 : FSTEP_HOLD_MAX;
            if (s->otrial) s->opt_wait = s->opt_wait * 2 < FSTEP_OPT_MAX ? s->opt_wait * 2 : FSTEP_OPT_MAX; }
        s->k = want; s->ups++; s->late_run = s->fit_run = 0; s->since = 0; s->otrial = s->grace = 0;
    } else if (s->trial && !--s->trial) { s->hold = FSTEP_HOLD0; if (s->otrial) s->opt_wait = FSTEP_OPT0; s->otrial = 0; }   // the trial held
#ifndef DM_TEST_FSTEP_NO_MARGIN   // negative control of t_fstep only: any cost below k − 1 counts as fitting
    double fit = FSTEP_FIT;
#else
    double fit = 1.0;
#endif
    if (!late) s->fit_run = s->k > kmin && cost <= fit * (s->k - 1) ? s->fit_run + 1 : 0;
    if (s->fit_run >= FSTEP_RUN && s->since >= s->hold && !s->trial) { s->k--; s->downs++; s->trial = FSTEP_RUN; s->fit_run = 0; }
    else if (s->opt && !s->trial && s->k > kmin && s->since >= s->opt_wait) {   // a trial whatever the cost, with a grace period (opt)
        s->k--; s->downs++; s->trial = FSTEP_RUN + FSTEP_GRACE; s->grace = FSTEP_GRACE; s->otrial = 1; s->fit_run = 0; }
    s->step = s->k + (s->debt >= 1 && s->k < FSTEP_MAX);
    s->paid += s->step > s->k;
    return s->step;
}
