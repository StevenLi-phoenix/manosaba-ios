// Frame-rate trials (plan item 216): helping Tears of the Kingdom's frame-rate controller out of a 30 fps hold that the GPU's own clock keeps it
// in. The controller (games/totk/rules.py, option fps60) keeps the slower of the frame's CPU and GPU time, the GPU time taken from the counter
// reports (gfx/nvn/nvn_ts.h). At 30 fps an iPhone lowers the GPU clock until the frame's work fills most of the 33 ms, so the GPU time read stays
// over 16.7 ms and the game never tries 60, though the same frames take far less once it runs at 60 and the clock comes up (iPhone 17 Pro, Great
// Sky Island, 2026-10-22: 20.65 ms of pass time a frame at 30 fps, 11.09 ms at 60 in the same run, every kind of pass about 1.9 times faster;
// 4 runs of 5 stayed at 30 fps with GPU 25-28 ms, the fifth picked 60 by itself when its GPU time dipped to 16.6 ms).
// A trial, while the player's target is 60 and nothing holds the rate: after FPT_WAIT0 s at interval 2 or 3 the report times of each submission
// are drawn towards its GPU start by FPT_FACTOR until the game picks interval 1, and for FPT_GRACE_S s after that while the clock comes up; then the
// real times again. A game that steps back up before FPT_WATCH_S s on the real times has failed the trial (too slow even at the higher clock, or held
// back by its CPU time, which no factor touches), and the wait before the next trial doubles, up to FPT_WAIT_MAX.
// Unit test gfx/tests/t_fptrial.c. Pure: no clock, no I/O.
#pragma once
#include <stdint.h>

enum { FPT_IDLE, FPT_PUSH, FPT_GRACE, FPT_WATCH };
#define FPT_FACTOR 0.35      // a report's distance from its submission's GPU start during a trial (DM_NVN_TS_SCALE=0.35 took the phone to 60 fps)
#define FPT_WAIT0 5.0        // s at interval 2 or 3 with the target at 60 before a trial
#define FPT_WAIT_MAX 32.0    // the longest wait, after failed trials
#define FPT_PUSH_MAX 3.0     // s of a trial in which the game does not pick interval 1: failed
#define FPT_GRACE_S 1.0      // s the factor stays after the game picked interval 1
#define FPT_WATCH_S 4.0      // s on the real times after that; stepping up in them fails the trial

typedef struct {
    int st;                       // FPT_*
    double since, wait;           // when the state began (in FPT_IDLE: when the game was last at interval 1, or the last trial ended); the wait
    uint64_t trials, held, failed, unlowered;   // for the log: trials begun, held at 60, failed (of them without interval 1)
} nvn_fptrial;

// One present at now (s): the game's present interval itv and want (the player's target is 60 and nothing holds the rate). Returns the factor on
// the report times of the submissions from now on (1: the real times)
static inline double nvn_fptrial_step(nvn_fptrial *s, double now, int itv, int want) {
    if (s->wait <= 0) { s->wait = FPT_WAIT0; s->since = now; }
    if (!want) { s->st = FPT_IDLE; s->since = now; return 1.0; }   // a hold or a target of 30: no trial, and the wait starts again after it
    switch (s->st) {
    case FPT_IDLE:
        if (itv < 2) { s->since = now; return 1.0; }   // already at 60 fps
        if (now - s->since < s->wait) return 1.0;
        s->st = FPT_PUSH; s->since = now; s->trials++;
        return FPT_FACTOR;
    case FPT_PUSH:
        if (itv == 1) { s->st = FPT_GRACE; s->since = now; return FPT_FACTOR; }
        if (now - s->since < FPT_PUSH_MAX) return FPT_FACTOR;
        s->unlowered++;
        break;   // failed: the game did not pick 60
    case FPT_GRACE:
#ifndef DM_TEST_FPT_NO_GRACE   // negative control of t_fptrial only: the real times as soon as the game picks interval 1
        if (itv >= 2) break;   // failed: back up even on the shorter times (its CPU time)
        if (now - s->since < FPT_GRACE_S) return FPT_FACTOR;
#endif
        s->st = FPT_WATCH; s->since = now;
        return 1.0;
    case FPT_WATCH:
        if (itv >= 2) break;   // failed: too slow at 60 on the real times
        if (now - s->since >= FPT_WATCH_S) { s->held++; s->wait = FPT_WAIT0; s->st = FPT_IDLE; s->since = now; }
        return 1.0;
    }
    s->failed++;
#ifndef DM_TEST_FPT_NO_BACKOFF   // negative control of t_fptrial only: the same wait after every failure
    s->wait = s->wait * 2 < FPT_WAIT_MAX ? s->wait * 2 : FPT_WAIT_MAX;
#endif
    s->st = FPT_IDLE; s->since = now;
    return 1.0;
}
