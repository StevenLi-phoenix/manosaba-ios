// The wide picture's parsing and state (host/aspect.h, host/game/aspect.c, plan item 154):
//   DM_ASPECT: 21:9, 3440x1440, 2.39 and "window" taken; 0 / unset off; 16:0, 0.5, 5, "wide" and "21:9x" refused
//   the factor on a 16:9 camera: 21:9 → 1.3125, 32:9 → 2, off → 0
//   no game offering it: the switch does nothing; DM_ASPECT=21:9 at boot: on at 21:9, the front end told once; the switch: off, then on again
//   DM_ASPECT=window: on at the window's shape, following a resize while on, not while off (and on again at the new shape)
// "window" keeping the shape it started with (-DDM_TEST_ASPECT_NO_FOLLOW): 1 fails.
#include "../game/aspect.c"
#include <math.h>
#include <stdlib.h>
static int n, fail;
static void check(const char *what, int ok) { n++; fail += !ok; printf("%-100s %s\n", what, ok ? "✓" : "✗"); }
static int told;
static void on_change(void) { told++; }
static int near(double a, double b) { return fabs(a - b) < 1e-4; }
static void reset(void) { atomic_store(&g_now, 0); atomic_store(&g_avail, 0); g_wide = 21.0 / 9; g_follow = 0; atomic_store(&g_view, 16.0 / 9); told = 0; }
int main(void) {
    double r[4]; int k[4];
    k[0] = dm_aspect_parse("21:9", &r[0]); k[1] = dm_aspect_parse("3440x1440", &r[1]); k[2] = dm_aspect_parse("2.39", &r[2]); k[3] = dm_aspect_parse("window", &r[3]);
    check("DM_ASPECT 21:9, 3440x1440, 2.39, window: taken (2.3333, 2.3889, 2.39, -1)",
          k[0] == 1 && k[1] == 1 && k[2] == 1 && k[3] == 1 && near(r[0], 21.0 / 9) && near(r[1], 3440.0 / 1440) && near(r[2], 2.39) && r[3] == -1);
    double z; int off = dm_aspect_parse("0", &z) == 0 && dm_aspect_parse(NULL, &z) == 0 && dm_aspect_parse("", &z) == 0;
    check("0, unset, empty: off (0)", off);
    const char *bad[] = {"16:0", "0.5", "5", "wide", "21:9x"}; int refused = 1;
    for (int i = 0; i < 5; i++) refused &= dm_aspect_parse(bad[i], &z) == -1;
    check("16:0, 0.5, 5 (outside 1-4), wide, 21:9x: refused (-1)", refused);
    check("the factor on 16:9: 21:9 → 1.3125, 32:9 → 2, off → 0",
          dm_aspect_factor(21.0 / 9, 16.0 / 9) == 1.3125f && dm_aspect_factor(32.0 / 9, 16.0 / 9) == 2.0f && dm_aspect_factor(0, 16.0 / 9) == 0);
    dm_aspect_on_change(on_change);
    reset(); unsetenv("DM_ASPECT");
    check("no game offering it: the switch does nothing (0), nothing in force", dm_aspect_toggle() == 0 && dm_aspect_now() == 0 && !dm_aspect_available() && told == 0);
    reset(); setenv("DM_ASPECT", "21:9", 1); dm_aspect_init();
    check("DM_ASPECT=21:9 at boot: offered, on at 2.3333, the front end told once", dm_aspect_available() && near(dm_aspect_now(), 21.0 / 9) && told == 1);
    int t1 = dm_aspect_toggle(); float a1 = dm_aspect_now(); int t2 = dm_aspect_toggle();
    check("the switch: off (0), then on again at 2.3333; told at each", t1 == 1 && a1 == 0 && t2 == 1 && near(dm_aspect_now(), 21.0 / 9) && told == 3);
    reset(); unsetenv("DM_ASPECT"); dm_aspect_init();
    check("DM_ASPECT unset: offered, off; the switch turns on 21:9", dm_aspect_available() && dm_aspect_now() == 0 && dm_aspect_toggle() && near(dm_aspect_now(), 21.0 / 9));
    reset(); dm_aspect_view(1.6); setenv("DM_ASPECT", "window", 1); dm_aspect_init(); float w0 = dm_aspect_now();
    dm_aspect_view(2.4); float w1 = dm_aspect_now();
    check("window: on at the window's shape (1.6), following a resize to 2.4 while on", near(w0, 1.6) && near(w1, 2.4));
    dm_aspect_toggle(); dm_aspect_view(1.9); float w2 = dm_aspect_now(); dm_aspect_toggle();
    check("... off: a resize changes nothing; on again: at the new shape (1.9)", w2 == 0 && near(dm_aspect_now(), 1.9));
    unsetenv("DM_ASPECT");
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
