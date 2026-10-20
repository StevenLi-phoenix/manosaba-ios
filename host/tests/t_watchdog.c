// The no-progress watchdog (crash.c, DM_WATCH_STALL) and the background (plan item 51): each case runs in a child with a 2 s stall limit.
//   away:   in the background (guest clock frozen for reason 1) for 4 s without progress, then back with progress: not killed (exit 0)
//   stuck:  4 s without progress in the foreground: killed (128 + SIGALRM, a "[red] watchdog" line)
//   after:  4 s in the background, then 3 s without progress after the return: killed (counting starts again at the return)
// Before the fix a DM_TEST_BG run of Tears of the Kingdom was killed 15 s into its background. Negative control: -DDM_TEST_WATCH_NO_BG (the
// background not taken into account) kills "away".
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../dm.h"
void crash_install(void);
static _Atomic int g_bg;
int guest_clock_frozen_for(int reason) { return reason == 1 && atomic_load(&g_bg); }
static int fails, checks;
#define CHECK(c, ...) do { checks++; if (c) printf("  ✓ "); else { fails++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
static void child(int kind) {
    setenv("DM_WATCH_STALL", "2", 1); crash_install();
    crash_watch_pregame_done(); crash_watch_progress(1, "start");
    if (kind == 0) { atomic_store(&g_bg, 1); sleep(4); atomic_store(&g_bg, 0); crash_watch_progress(2, "back"); usleep(500000); _exit(0); }
    if (kind == 1) { sleep(4); _exit(0); }
    atomic_store(&g_bg, 1); sleep(4); atomic_store(&g_bg, 0); sleep(3); _exit(0);
}
// The three cases run at the same time (they mostly sleep: about 6 s in all instead of 12), each child's stderr into its own temporary file
typedef struct { pid_t pid; FILE *f; } kid;
static kid start(int kind) {
    kid k = {-1, tmpfile()}; if (!k.f) return k;
    fflush(stdout); k.pid = fork(); if (!k.pid) { dup2(fileno(k.f), 2); child(kind); }
    return k;
}
static int finish(kid k, char *out, size_t n) {
    out[0] = 0; if (!k.f || k.pid < 0) return -1;
    int st; waitpid(k.pid, &st, 0);
    rewind(k.f); size_t got = fread(out, 1, n - 1, k.f); out[got] = 0; fclose(k.f);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    static char out[65536]; int code; kid k[3];
    for (int i = 0; i < 3; i++) k[i] = start(i);
    code = finish(k[0], out, sizeof out);
    CHECK(code == 0 && !strstr(out, "[red] watchdog"), "away: 4 s in the background without progress, back with progress: not killed (exit %d)", code);
    code = finish(k[1], out, sizeof out);
    CHECK(code == 128 + SIGALRM && strstr(out, "[red] watchdog: no progress"), "stuck: 4 s without progress in the foreground: killed (exit %d)", code);
    code = finish(k[2], out, sizeof out);
    CHECK(code == 128 + SIGALRM && strstr(out, "[red] watchdog: no progress"), "after: no progress after the return: killed (exit %d)", code);
    printf("t_watchdog: %d checks, %d failed\n", checks, fails); return fails != 0;
}
