// DM_PWRTEST's spec (host/pwrspec.h, plan item 230): threads, the GPU and its busy share, seconds. The share ignored (-DDM_TEST_PWRSPEC_NO_DUTY) fails 1.
#include <stdio.h>
#include "../pwrspec.h"
int main(void) {
    const struct { const char *e; int ok, threads, gpu, duty, secs; const char *why; } c[] = {
        {"2", 1, 2, 0, 100, 240, "2: two CPU threads, no GPU, 240 s"},
        {"2:240", 1, 2, 0, 100, 240, "2:240: the same, seconds given"},
        {"2:gpu:120", 1, 2, 1, 100, 120, "2:gpu:120: the GPU fully busy, 120 s"},
        {"2:gpu60:240", 1, 2, 1, 60, 240, "2:gpu60:240: the GPU 60 % busy [-DDM_TEST_PWRSPEC_NO_DUTY: 100]"},
        {"0:gpu", 1, 0, 1, 100, 240, "0:gpu: the GPU alone"},
        {"12:gpu150", 1, 8, 1, 100, 240, "12:gpu150: threads capped at 8, a share outside 1-99 is full"},
        {"", 0, 0, 0, 0, 0, "empty: no test, the game starts"},
    };
    int n = sizeof c / sizeof *c, fail = 0;
    for (int i = 0; i < n; i++) {
        pwr_spec s = {0}; int ok = pwr_spec_parse(c[i].e, &s);
        int good = ok == c[i].ok && (!ok || (s.threads == c[i].threads && s.gpu == c[i].gpu && s.duty == c[i].duty && s.secs == c[i].secs)); fail += !good;
        printf("%-74s %s (got %d: %d threads, gpu %d at %d %%, %d s)\n", c[i].why, good ? "✓" : "✗", ok, s.threads, s.gpu, s.duty, s.secs);
    }
    printf("== %d checks, %d failed ==\n", n, fail); return fail != 0;
}
