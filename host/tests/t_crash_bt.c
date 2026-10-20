// The crash report's backtrace with a corrupt frame chain (crash.c dm_backtrace, plan item 125): two frames on a mapped page, the second pointing
// to a page just unmapped (a corrupt x29). The walk prints both return addresses, notes the unreadable frame and returns; the process goes on.
// Read directly (-DDM_TEST_BT_UNGUARDED, as before): the walk faults on the unmapped frame (the child dies of SIGSEGV / SIGBUS): 2 fail.
#include "../dm.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
void dm_backtrace(uint64_t fp);
static int n, fail;
#define CHECK(c, ...) do { n++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
int main(void) {
    int pf[2]; if (pipe(pf)) return 2;
    pid_t p = fork();
    if (!p) {
        dup2(pf[1], 2); close(pf[0]); close(pf[1]);
        size_t pg = (size_t)getpagesize();
        uint8_t *m = mmap(NULL, 2 * pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        munmap(m + pg, pg);   // the second page: unmapped, above the first
        uint64_t *f0 = (uint64_t *)m, *f1 = (uint64_t *)(m + 64);
        f0[0] = (uint64_t)(uintptr_t)f1; f0[1] = 0x11110000;
        f1[0] = (uint64_t)(uintptr_t)(m + pg + 16); f1[1] = 0x22220000;   // a corrupt x29: the unmapped page
        dm_backtrace((uint64_t)(uintptr_t)f0);
        fprintf(stderr, "walk returned\n"); fflush(stderr); _exit(0);
    }
    close(pf[1]); char out[4096] = {0}; ssize_t k = 0, r; while ((r = read(pf[0], out + k, sizeof out - 1 - (size_t)k)) > 0) k += r;
    int st; waitpid(p, &st, 0);
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0 && strstr(out, "walk returned"), "a corrupt frame pointer does not fault the walk (child %s %d)", WIFEXITED(st) ? "exited" : "signal", WIFEXITED(st) ? WEXITSTATUS(st) : WTERMSIG(st));
    CHECK(strstr(out, "0x11110000") && strstr(out, "0x22220000") && strstr(out, "unreadable"), "both return addresses printed, the unreadable frame noted");
    printf("t_crash_bt: %d checks, %d failed\n", n, fail); return fail != 0;
}
