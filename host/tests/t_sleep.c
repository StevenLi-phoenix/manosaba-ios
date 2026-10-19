// svc 0x0B SleepThread lasts what was asked for: the mean oversleep of 5 ms and 33.3 ms sleeps stays under 0.2 ms, or under a third of what one plain
// nanosleep oversleeps on this machine right now, and no sleep ends more than 0.05 ms early. One plain nanosleep overslept by about a quarter (1.17 /
// 4.27 ms on 5 / 33.3 ms on an M5 at the default QoS), and by twice that while the system coalesced timers harder (the display asleep, it seems):
// the step loop then still ends within 0.6 / 1.3 ms, but no longer within 0.2.
// Yield back-off: a guest thread yielding back to back for 20 ms makes far fewer than 50,000 yields, 50-500 of them short sleeps, and its loop ends
// within 0.3 ms of the 20 ms; yields with another SVC between them never back off. The back-off is off by default: the test turns it on with
// DM_YIELD_BACKOFF=250 unless set; DM_YIELD_BACKOFF=0 (plain yields, the default in the games) fails the first.
// Timing test: run it on a machine that is not saturated.
#include "../dm.h"
#include <pthread.h>
#include <pthread/qos.h>
#include <time.h>
int svc_thread(int num, uint64_t *regs);
uint32_t svc_thread_main_init(void);
void dm_yield_top(char *out, size_t n);
#include <stdlib.h>
#include <string.h>
void (*dm_test_cv_after_release)(void);   // DM_TEST_HOOKS (t_condvar uses it)
static int n, fail;
#define CHECK(c, ...) do { n++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
static uint64_t now(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }
static void *guest(void *a) {
    (void)a; pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);   // as hle_thread.c starts guest threads (at a lower QoS macOS coalesces timers more)
    svc_thread_main_init();
    static const uint64_t want[] = {5000000, 33333333}; static const int reps[] = {40, 10};
    for (int k = 0; k < 2; k++) {
        double over = 0, early = 0;
        for (int i = 0; i < reps[k]; i++) {
            uint64_t r[32] = {want[k]}, t0 = now(); svc_thread(0x0B, r); double d = (double)(now() - t0) - (double)want[k];
            over += d; if (-d > early) early = -d;
        }
        over /= reps[k];
        double plain = 0; for (int i = 0; i < reps[k] / 2; i++) { uint64_t t0 = now(); struct timespec ts = {0, (long)want[k]}; nanosleep(&ts, 0); plain += (double)(now() - t0) - (double)want[k]; }
        plain /= reps[k] / 2; double lim = plain / 3 > 200000 ? plain / 3 : 200000;
        CHECK(over < lim, "SleepThread(%.1f ms): mean oversleep %.3f ms (under %.3f: 0.2 ms or a third of a plain nanosleep's %.3f)", want[k] / 1e6, over / 1e6, lim / 1e6, plain / 1e6);
        CHECK(early <= 50000, "SleepThread(%.1f ms): never more than 0.05 ms early (earliest %.3f ms)", want[k] / 1e6, early / 1e6);
    }
    // yield back-off
    char top[256]; dm_yield_top(top, sizeof top);   // reset the counts
    uint64_t t0 = now(), yields = 0; while (now() - t0 < 20000000) { uint64_t r[32] = {0}; svc_thread(0x0B, r); yields++; }
    double late = (double)(now() - t0) - 20e6;
    dm_yield_top(top, sizeof top); const char *b = strstr(top, "back-off sleeps "); long sleeps = b ? atol(b + 16) : -1;
    CHECK(yields < 50000, "20 ms of back-to-back yields: %llu yields (fewer than 50,000)", (unsigned long long)yields);
    CHECK(sleeps >= 50 && sleeps <= 500, "... %ld of them short sleeps (50-500)", sleeps);
    CHECK(late < 300000, "... the loop ends %.3f ms after the 20 ms (under 0.3)", late / 1e6);
    { uint64_t q[32] = {0, 0xFFFF8000u}; svc_thread(0x0C, q); }   // another SVC first: the run of yields above ends here
    t0 = now(); while (now() - t0 < 5000000) { uint64_t r[32] = {0}; svc_thread(0x0B, r); uint64_t q[32] = {0, 0xFFFF8000u}; svc_thread(0x0C, q); }
    dm_yield_top(top, sizeof top); b = strstr(top, "back-off sleeps "); sleeps = b ? atol(b + 16) : -1;
    CHECK(sleeps == 0, "5 ms of yields with GetThreadPriority between them: no back-off (%ld sleeps)", sleeps);
    return NULL;
}
int main(void) {
    setenv("DM_YIELD_BACKOFF", "250", 0);   // off by default in the host
    pthread_t t; pthread_create(&t, NULL, guest, NULL); pthread_join(t, NULL);
    printf("t_sleep: %d checks, %d failed\n", n, fail); return fail != 0;
}
