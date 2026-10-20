// A thread dump while a guest thread holds the stderr lock (hle_thread.c thr_bt, plan item 107: the frames-stopped dump runs in the product): a
// registered guest thread takes stderr's lock (as in the middle of a log line) and keeps it until told to let go; another thread dumps every
// guest thread's backtrace; 200 ms later the guest is told to let go. The dump gathers the guest's state while it is suspended and prints it after
// resuming it, so the guest can let go and the dump ends with its backtrace line. Printing while the guest is stopped
// (-DDM_TEST_BT_PRINT_SUSPENDED, as before) waits for a lock the stopped thread holds: the dump never ends (checked with a 3 s limit; 2 fail).
#include "../dm.h"
#include <dispatch/dispatch.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
uint32_t svc_thread_main_init(void);
void dm_test_thr_bt_all(void);
void (*dm_test_cv_after_release)(void);   // DM_TEST_HOOKS (t_condvar uses it)
static int n, fail;
#define CHECK(c, ...) do { n++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
static dispatch_semaphore_t g_held, g_let_go, g_done;
static uint32_t g_handle;
static void *guest(void *a) {
    (void)a; g_handle = svc_thread_main_init();
    flockfile(stderr); dispatch_semaphore_signal(g_held);
    dispatch_semaphore_wait(g_let_go, DISPATCH_TIME_FOREVER);
    funlockfile(stderr);
    return NULL;
}
static void *dumper(void *a) { (void)a; dm_test_thr_bt_all(); fflush(stderr); dispatch_semaphore_signal(g_done); return NULL; }
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    g_held = dispatch_semaphore_create(0); g_let_go = dispatch_semaphore_create(0); g_done = dispatch_semaphore_create(0);
    int pfd[2]; if (pipe(pfd)) return 2;
    int saved = dup(2); dup2(pfd[1], 2); close(pfd[1]);   // the dump's text, read back below
    pthread_t g, d; pthread_create(&g, NULL, guest, NULL);
    dispatch_semaphore_wait(g_held, DISPATCH_TIME_FOREVER);
    pthread_create(&d, NULL, dumper, NULL);
    usleep(200000); dispatch_semaphore_signal(g_let_go);
    long ended = !dispatch_semaphore_wait(g_done, dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC));
    char buf[16384] = {0}; ssize_t got = 0;
    if (ended) { pthread_join(g, NULL); close(2); got = read(pfd[0], buf, sizeof buf - 1); }
    dup2(saved, 2);
    char want[64]; snprintf(want, sizeof want, "[dm]   %#x backtrace:", g_handle);
    CHECK(ended, "the dump ends although the guest held the stderr lock when it was stopped");
    CHECK(got > 0 && strstr(buf, want), "the guest's backtrace line is printed (\"%s\")", want);
    printf("t_threaddump: %d checks, %d failed\n", n, fail);
    fflush(stdout); _exit(fail != 0);   // a dumper still waiting (the negative control) must not keep the process
}
