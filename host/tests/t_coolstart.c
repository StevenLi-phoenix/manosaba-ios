// A run started only when the phone is cool enough (host/coolstart.h, plan item 62f). DM_COOL_START: "nominal" 0, "fair" 1, unset or another word
// none. A phone at serious (2) that cools to fair after 12 s and nominal after 21 s: with nominal the run waits 25 s (looks every 5 s) and starts
// at nominal, with fair 15 s at fair; none: at once; one that stays hot: given up after the 600 s cap, started hot (state 3 at the end). One
// look and on (-DDM_TEST_COOL_ONE_LOOK): 3 fail.
#include "../coolstart.h"
#include <stdio.h>

static int n, fail;
static void check(const char *what, int ok) { n++; fail += !ok; printf("%-100s %s\n", what, ok ? "✓" : "✗"); }
static double g_t; static int g_hot;
static int state(void) { return g_hot ? 3 : g_t < 12 ? 2 : g_t < 21 ? 1 : 0; }
static void nap(double s) { g_t += s; }

int main(void) {
    check("DM_COOL_START: nominal 0, fair 1, unset or another word none", cool_level("nominal") == 0 && cool_level("fair") == 1 && cool_level(NULL) == -1 && cool_level("cold") == -1);
    int at; g_t = 0; g_hot = 0;
    double w = cool_wait(0, state, nap, 5, 600, &at);
    check("serious, fair at 12 s, nominal at 21 s; nominal asked: 25 s, started at nominal", w == 25 && at == 0);
    g_t = 0; w = cool_wait(1, state, nap, 5, 600, &at);
    check("... fair asked: 15 s, started at fair", w == 15 && at == 1);
    g_t = 0; w = cool_wait(-1, state, nap, 5, 600, &at);
    check("none asked: at once", w == 0 && at == 2);
    g_t = 0; g_hot = 1; w = cool_wait(0, state, nap, 5, 600, &at);
    check("a phone that stays hot: given up after the 600 s cap, started hot", w == 600 && at == 3);
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
