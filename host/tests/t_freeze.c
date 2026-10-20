// The frames-stopped watch (crash.c, plan item 107), with DM_FREEZE_DUMP=1 for speed and no test watchdog (DM_WATCH / DM_WATCH_STALL unset, as in
// the product); each case in a child whose stderr is read back:
//   freeze:     presents for 1 s, none for 3 s, then presents again for 1 s: one "[red] frames stopped" line with a state dump, then "frames came
//               back after 3.x s", and the process goes on (exit 0)
//   twice:      two such freezes: two dumps, numbered 1 and 2
//   boot:       3 s before the first present: no dump (the game has not started drawing)
//   background: presents, then 3 s in the background (guest clock frozen for reason 1) and back with presents: no dump
//   keyboard:   presents, then 3 s with the system keyboard up (reason 2): no dump
// No watch without a test watchdog (-DDM_TEST_FREEZE_NONE, as before): 2 fail.
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../dm.h"
void crash_install(void);
void crash_watch_frame(void);
void crash_dump_now(const char *why);
static _Atomic int g_frozen_reason = -1;
int guest_clock_frozen_for(int reason) { return atomic_load(&g_frozen_reason) == reason; }
static int fails, checks;
#define CHECK(c, ...) do { checks++; if (c) printf("  ✓ "); else { fails++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
static void frames(double s) { for (int i = 0; i < (int)(s * 60); i++) { crash_watch_frame(); usleep(16667); } }
static void child(int kind) {
    unsetenv("DM_WATCH"); unsetenv("DM_WATCH_STALL"); setenv("DM_FREEZE_DUMP", "1", 1); crash_install();
    if (kind == 2) { sleep(3); frames(0.5); _exit(0); }
    frames(1);
    if (kind == 3 || kind == 4) { atomic_store(&g_frozen_reason, kind == 3 ? 1 : 2); sleep(3); atomic_store(&g_frozen_reason, -1); frames(1); _exit(0); }
    sleep(3); frames(1);
    if (kind == 1) { sleep(3); frames(1); }
    _exit(0);
}
static int run(int kind, char *out, size_t n) {
    int pf[2]; if (pipe(pf)) return -1;
    pid_t p = fork(); if (!p) { dup2(pf[1], 2); close(pf[0]); close(pf[1]); child(kind); }
    close(pf[1]); ssize_t k = 0, r; while (k < (ssize_t)n - 1 && (r = read(pf[0], out + k, n - 1 - k)) > 0) k += r; out[k] = 0; close(pf[0]);
    int st; waitpid(p, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}
static int count(const char *s, const char *w) { int c = 0; for (const char *p = s; (p = strstr(p, w)); p += strlen(w)) c++; return c; }
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    static char out[65536]; int code;
    code = run(0, out, sizeof out);
    const char *back = strstr(out, "frames came back after ");
    CHECK(code == 0 && count(out, "[red] frames stopped") == 1 && strstr(out, "state dump (frames stopped)") && back && atof(back + 23) >= 2.9 && atof(back + 23) < 4,
          "freeze: one dump, the return noted (%.1f s), the process goes on (exit %d)", back ? atof(back + 23) : -1, code);
    code = run(1, out, sizeof out);
    CHECK(code == 0 && count(out, "[red] frames stopped") == 2 && strstr(out, "(freeze 1 of this run)") && strstr(out, "(freeze 2 of this run)"), "twice: two dumps, numbered (exit %d)", code);
    code = run(2, out, sizeof out);
    CHECK(code == 0 && !strstr(out, "frames stopped"), "boot: no dump before the first present (exit %d)", code);
    code = run(3, out, sizeof out);
    CHECK(code == 0 && !strstr(out, "frames stopped"), "background: no dump for the time away (exit %d)", code);
    code = run(4, out, sizeof out);
    CHECK(code == 0 && !strstr(out, "frames stopped"), "keyboard: no dump while the system keyboard is up (exit %d)", code);
    printf("t_freeze: %d checks, %d failed\n", checks, fails); return fails != 0;
}
