// Which SetUserInactivityDetectionTimeExtended calls hold Tears of the Kingdom at 30 fps (games/totk/movie_fps.c, plan item 130): only the call that
// returns to main+0x2b36104 (a pre-rendered movie starts); the prologue's real-time cutscene (true from main+0x770f04, false from +0x770ad0, measured
// in the 60 fps product's prologue) and the movie's end (+0x2b36774) do not. Built and run by host/tests (make run). Every cutscene held
// (-DDM_TEST_MOVIE_ANY_CUTSCENE, the oe call alone) fails 3.
#include <stdio.h>
#define MOVIE_FPS_TEST 1
#include "../movie_fps.c"

int main(void) {
    struct { int on; const char *mod; uint64_t off; int want; const char *why; } c[] = {
        {1, "main", 0x2b36104, 30, "true from main+0x2b36104 (a pre-rendered movie starts): 30"},
        {1, "main", 0x770f04, 0, "true from main+0x770f04 (a real-time cutscene starts, the prologue's): no hold"},
        {0, "main", 0x770ad0, 0, "false from main+0x770ad0 (the cutscene ends): released"},
        {0, "main", 0x2b36774, 0, "false from main+0x2b36774 (the movie ends): released"},
        {1, "sdk", 0x2b36104, 0, "the same offset in another module: no hold"},
        {1, NULL, 0, 0, "a caller in no module: no hold"},
    };
    int n = sizeof c / sizeof *c, fail = 0;
    for (int i = 0; i < n; i++) {
        int got = movie_fps(c[i].on, c[i].mod, c[i].off), ok = got == c[i].want; fail += !ok;
        printf("%-84s expected %2d → got %2d %s\n", c[i].why, c[i].want, got, ok ? "✓" : "✗");
    }
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
