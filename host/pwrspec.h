// DM_PWRTEST=<threads>[:gpu[<busy %>]][:<seconds>] (ios/Sources/pwrtest.m, plan item 230): CPU threads 0-8, the GPU loaded (100 % busy unless a
// share 1-99 follows "gpu"), seconds (240 unless the last field is a number). Unit test tests/t_pwrspec.c
#pragma once
#include <stdlib.h>
#include <string.h>
typedef struct { int threads, gpu, duty, secs; } pwr_spec;
static inline int pwr_spec_parse(const char *e, pwr_spec *s) {   // 0: no spec (the game starts)
    if (!e || !*e) return 0;
    s->threads = atoi(e); if (s->threads < 0) s->threads = 0; if (s->threads > 8) s->threads = 8;
    const char *g = strstr(e, ":gpu"); s->gpu = g != NULL; s->duty = 100; s->secs = 240;
#ifndef DM_TEST_PWRSPEC_NO_DUTY   // negative control of t_pwrspec only: the share ignored
    if (g) { int d = atoi(g + 4); if (d > 0 && d < 100) s->duty = d; }
#endif
    const char *c = strrchr(e, ':'); if (c && c[1] >= '0' && c[1] <= '9') s->secs = atoi(c + 1);
    return 1;
}
