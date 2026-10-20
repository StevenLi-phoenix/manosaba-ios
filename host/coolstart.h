// A run started only when the phone is cool enough (plan item 62f: T5eng measured the same scene at 60 fps on a cool phone and 30 on a hot one, so
// two runs of an A/B on the phone compare their thermal states unless each starts from the same one). DM_COOL_START=nominal or fair: the app
// (ios/Sources/main.m boot_thread) waits before the game starts until NSProcessInfo's thermal state is at most that (0 nominal, 1 fair, 2 serious,
// 3 critical), checking every 5 s, at most 10 minutes, and logs how long it waited and the state it started at.
#pragma once
#include <string.h>

// The level DM_COOL_START names: 0 nominal, 1 fair, -1 none (unset or anything else)
static inline int cool_level(const char *e) { return !e ? -1 : !strcmp(e, "nominal") ? 0 : !strcmp(e, "fair") ? 1 : -1; }
// Wait until state() <= level or max_s seconds have passed, sleeping step_s between looks: the seconds waited (sleep and state given, for tests)
static inline double cool_wait(int level, int (*state)(void), void (*sleep_s)(double), double step_s, double max_s, int *at) {
    double waited = 0; int s = state();
#ifndef DM_TEST_COOL_ONE_LOOK   // negative control of host/tests t_coolstart only: one look, then on
    while (level >= 0 && s > level && waited + step_s <= max_s) { sleep_s(step_s); waited += step_s; s = state(); }
#endif
    if (at) *at = s;
    return waited;
}
